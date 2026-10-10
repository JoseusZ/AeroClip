#include "Storage/HistoryBuffer.h"
#include <dpapi.h>
#include <ShlObj.h>

// LZ4 single-file in-tree (BSD-2, Yann Collet). Solo usamos las dos APIs
// "simple" (LZ4_compress_default / LZ4_decompress_safe). El compresor
// mantiene una pila interna de 16 KB (LZ4_MEMORY_USAGE=14 por defecto), por
// lo que no hay asignacion dinamica.
extern "C" {
#include "lz4.h"
}

#include <objbase.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "Shell32.lib")

namespace AeroClip::Storage {

    struct CriticalSectionLock {
        CRITICAL_SECTION& cs;
        explicit CriticalSectionLock(CRITICAL_SECTION& c) : cs(c) { ::EnterCriticalSection(&cs); }
        ~CriticalSectionLock() { ::LeaveCriticalSection(&cs); }
    };

    // -------------------------------------------------------------------
    // Helpers LZ4 (sin perdida)
    // -------------------------------------------------------------------

    static LPVOID Lz4Compress(const void* inData, int cbIn, int& cbOut) {
        cbOut = 0;
        if (!inData || cbIn <= 0) return nullptr;
        int cap = LZ4_compressBound(cbIn);
        if (cap <= 0) return nullptr;
        LPVOID out = ::HeapAlloc(::GetProcessHeap(), 0, cap);
        if (!out) return nullptr;
        int n = LZ4_compress_default(static_cast<const char*>(inData),
                                      static_cast<char*>(out),
                                      cbIn, cap);
        if (n <= 0) {
            ::HeapFree(::GetProcessHeap(), 0, out);
            return nullptr;
        }
        cbOut = n;
        return out;
    }

    static LPVOID Lz4Decompress(const void* compressed, int cbCompressed,
                                int cbOriginal, int& cbOut) {
        cbOut = 0;
        if (!compressed || cbCompressed <= 0 || cbOriginal <= 0) return nullptr;
        LPVOID out = ::HeapAlloc(::GetProcessHeap(), 0, cbOriginal);
        if (!out) return nullptr;
        int n = LZ4_decompress_safe(static_cast<const char*>(compressed),
                                     static_cast<char*>(out),
                                     cbCompressed, cbOriginal);
        if (n < 0 || n != cbOriginal) {
            ::HeapFree(::GetProcessHeap(), 0, out);
            return nullptr;
        }
        cbOut = n;
        return out;
    }

    // -------------------------------------------------------------------
    // Miniatura: DIB -> JPEG con aspect ratio preservado, max 64x64
    // -------------------------------------------------------------------

    static int GetEncoderClsidJpeg(CLSID* pClsid) {
        UINT num = 0, size = 0;
        Gdiplus::GetImageEncodersSize(&num, &size);
        if (size == 0) return -1;
        std::vector<BYTE> buf(size);
        Gdiplus::ImageCodecInfo* arr = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
        Gdiplus::GetImageEncoders(num, size, arr);
        for (UINT i = 0; i < num; ++i) {
            if (arr[i].MimeType && lstrcmpW(arr[i].MimeType, L"image/jpeg") == 0) {
                *pClsid = arr[i].Clsid;
                return (int)i;
            }
        }
        return -1;
    }

    static std::vector<BYTE> MakeThumbnailJpeg(const BYTE* dib, DWORD cbDib) {
        std::vector<BYTE> out;
        if (!dib || cbDib < sizeof(BITMAPINFOHEADER)) return out;

        BITMAPINFOHEADER* bih = reinterpret_cast<BITMAPINFOHEADER*>(const_cast<BYTE*>(dib));
        if (bih->biWidth <= 0 || bih->biHeight == 0) return out;
        LONG cy = (bih->biHeight > 0) ? bih->biHeight : -bih->biHeight;
        if (bih->biWidth > 8192 || cy > 8192) return out;
        BOOL srcBottomUp = (bih->biHeight > 0);

        // Construct a top-down DIB section (negative biHeight) so that GDI+ does not perform a double flip when wrapping the HBITMAP.
        BITMAPINFO bmi{};
        bmi.bmiHeader = *bih;
        bmi.bmiHeader.biCompression = BI_RGB;
        bmi.bmiHeader.biHeight = -cy;
        bmi.bmiHeader.biSizeImage = 0;

        LPVOID bits = nullptr;
        HBITMAP hbm = ::CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!hbm || !bits) { if (hbm) ::DeleteObject(hbm); return out; }

        //Copy pixels while preserving orientation.
        DWORD headerSize = bih->biSize + bih->biClrUsed * sizeof(RGBQUAD);
        LONG rowBytes = ((bih->biWidth * bih->biBitCount + 31) / 32) * 4;
        SIZE_T imgBytes = (SIZE_T)rowBytes * cy;
        if (headerSize + imgBytes > cbDib) { ::DeleteObject(hbm); return out; }
        const BYTE* src = dib + headerSize;
        if (srcBottomUp) {
            // bottom-up -> top-down: reverse rows
            BYTE* dst = static_cast<BYTE*>(bits);
            for (LONG y = 0; y < cy; ++y) {
                memcpy(dst + y * rowBytes, src + (cy - 1 - y) * rowBytes, rowBytes);
            }
        } else {
         
            memcpy(bits, src, imgBytes);
        }

        
        Gdiplus::Bitmap srcBmp(hbm, nullptr);
        if (srcBmp.GetLastStatus() != Gdiplus::Ok) { ::DeleteObject(hbm); return out; }

        // Calcular tamano del thumbnail preservando aspect ratio. La imagen
        // original puede ser alargada o apaisada: NUNCA la deformamos a un
        // cuadrado. Guardamos el JPEG con lado maximo kThumbMaxSide = 256
        // (alta resolucion) para que DrawImageThumb pueda escalarlo hacia
        // abajo con buena calidad hacia el slot cuadrado de 64x64 del item.
        const int kThumbMaxSide = 256;
        const int origW = static_cast<int>(srcBmp.GetWidth());
        const int origH = static_cast<int>(srcBmp.GetHeight());
        if (origW <= 0 || origH <= 0) { ::DeleteObject(hbm); return out; }
        const float scale = (static_cast<float>(kThumbMaxSide) / static_cast<float>(origW) <
                             static_cast<float>(kThumbMaxSide) / static_cast<float>(origH))
                            ? (static_cast<float>(kThumbMaxSide) / static_cast<float>(origW))
                            : (static_cast<float>(kThumbMaxSide) / static_cast<float>(origH));
        int thumbW = static_cast<int>(origW * scale + 0.5f);
        int thumbH = static_cast<int>(origH * scale + 0.5f);
        if (thumbW < 1) thumbW = 1;
        if (thumbH < 1) thumbH = 1;

        Gdiplus::Bitmap thumb(thumbW, thumbH, PixelFormat24bppRGB);
        Gdiplus::Graphics g(&thumb);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
        g.Clear(Gdiplus::Color::White);
        g.DrawImage(&srcBmp, 0, 0, thumbW, thumbH);
        ::DeleteObject(hbm);


        CLSID jpegClsid;
        if (GetEncoderClsidJpeg(&jpegClsid) < 0) return out;

        IStream* stream = nullptr;
        if (FAILED(::CreateStreamOnHGlobal(nullptr, TRUE, &stream))) return out;
        struct StreamGuard { IStream* s; ~StreamGuard(){ if(s) s->Release(); } } sg{stream};

        Gdiplus::EncoderParameters enc{};
        enc.Count = 1;
        enc.Parameter[0].Guid = Gdiplus::EncoderQuality;
        enc.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
        enc.Parameter[0].NumberOfValues = 1;
        ULONG q = 45;
        enc.Parameter[0].Value = &q;

        if (thumb.Save(stream, &jpegClsid, &enc) != Gdiplus::Ok) return out;

        LARGE_INTEGER liZero{}; liZero.QuadPart = 0;
        ULARGE_INTEGER pos{};
        if (FAILED(stream->Seek(liZero, STREAM_SEEK_SET, &pos))) return out;

        BYTE buf2[4096];
        ULONG cbRead = 0;
        while (SUCCEEDED(stream->Read(buf2, sizeof(buf2), &cbRead)) && cbRead > 0) {
            out.insert(out.end(), buf2, buf2 + cbRead);
        }
        return out;
    }

    HistoryBuffer::HistoryBuffer() {
        ::InitializeCriticalSection(&m_cs);
        ::InitializeCriticalSection(&m_cacheCs);

        WCHAR tmp[MAX_PATH] = {};
        ::GetTempPathW(MAX_PATH, tmp);
        ::wcscat_s(tmp, L"AeroClip\\");
        ::CreateDirectoryW(tmp, nullptr);
        ::wcscpy_s(m_tempDir, MAX_PATH, tmp);

        m_seq = 0;
        m_renderReady = FALSE;

        InitPinDir();
        CleanupOrphanTemps();
    }

    HistoryBuffer::~HistoryBuffer() {
        {
            CriticalSectionLock lock(m_cs);
            for (auto& it : m_items) {
                DestroyItem(it);
            }
            m_items.clear();
        }

        CleanupByCurrentPid();
        ::DeleteCriticalSection(&m_cs);
        ::DeleteCriticalSection(&m_cacheCs);
    }

    LPVOID HistoryBuffer::Protect(const void* inData, DWORD cbData, SIZE_T& cbOut) {
        if (!inData || cbData == 0) { cbOut = 0; return nullptr; }

        SIZE_T cbAligned = (cbData + 15) & ~static_cast<SIZE_T>(15);
        LPVOID buf = ::HeapAlloc(::GetProcessHeap(), HEAP_ZERO_MEMORY, cbAligned);
        if (!buf) { cbOut = 0; return nullptr; }

        memcpy(buf, inData, cbData);

        if (!::CryptProtectMemory(buf, static_cast<DWORD>(cbAligned), CRYPTPROTECTMEMORY_SAME_PROCESS)) {
            ::SecureZeroMemory(buf, cbAligned);
            ::HeapFree(::GetProcessHeap(), 0, buf);
            cbOut = 0;
            return nullptr;
        }
        cbOut = cbAligned;
        return buf;
    }

    void HistoryBuffer::DestroyItem(HistoryItem& it) {
        // If it has a temporary file and is NOT pinned, it is deleted.
        if (it.bHasTemp && it.szTempPath[0] && !it.pinned) {
            ::DeleteFileW(it.szTempPath);
        }
        if (it.pData && it.cbAlloc) {
            ::SecureZeroMemory(it.pData, it.cbAlloc);
            ::HeapFree(::GetProcessHeap(), 0, it.pData);
        }
        it.pData = nullptr;
        it.cbData = 0;
        it.cbAlloc = 0;
        it.cbLz4 = 0;
        it.cbStored = 0;
        it.bHasTemp = FALSE;
        it.szTempPath[0] = 0;
    }

    LPVOID HistoryBuffer::DecryptCopyInternal(const HistoryItem& item, DWORD* pcbOut) {
        if (pcbOut) *pcbOut = 0;
        if (!item.pData || !item.cbAlloc || !item.cbData) return nullptr;

        LPVOID buf = ::HeapAlloc(::GetProcessHeap(), 0, item.cbAlloc);
        if (!buf) return nullptr;
        memcpy(buf, item.pData, item.cbAlloc);

        if (!::CryptUnprotectMemory(buf, static_cast<DWORD>(item.cbAlloc), CRYPTPROTECTMEMORY_SAME_PROCESS)) {
            ::SecureZeroMemory(buf, item.cbAlloc);
            ::HeapFree(::GetProcessHeap(), 0, buf);
            return nullptr;
        }
        if (pcbOut) *pcbOut = item.cbData;
        return buf;
    }

    LPVOID HistoryBuffer::DecryptCopy(const HistoryItem& item, DWORD* pcbOut) {
        return DecryptCopyInternal(item, pcbOut);
    }

    BOOL HistoryBuffer::IsDuplicate(const HistoryItem& item, ItemKind kind, const void* pData, DWORD cbData) {
        if (item.kind != kind || item.cbData != cbData) return FALSE;

        // Image: comparison requires reading from disk + CryptUnprotectMemory + LZ4.
        if (item.kind == ItemKind::Image && item.bHasTemp && item.cbLz4 > 0) {
            std::vector<BYTE> storedDpapi;
            DWORD cbOriginal = 0;
            if (!LoadImageFromTemp(item, storedDpapi, &cbOriginal)) return FALSE;

            SIZE_T cbAligned = ((SIZE_T)storedDpapi.size() + 15) & ~(SIZE_T)15;
            LPVOID lz4 = ::HeapAlloc(::GetProcessHeap(),
                                     HEAP_ZERO_MEMORY, cbAligned);
            if (!lz4) return FALSE;
            if (!storedDpapi.empty())
                memcpy(lz4, storedDpapi.data(), storedDpapi.size());
            if (!::CryptUnprotectMemory(lz4, (DWORD)cbAligned,
                                        CRYPTPROTECTMEMORY_SAME_PROCESS)) {
                ::SecureZeroMemory(lz4, cbAligned);
                ::HeapFree(::GetProcessHeap(), 0, lz4);
                return FALSE;
            }
            int cbDec = 0;
            LPVOID dec = Lz4Decompress(lz4, (int)item.cbLz4,
                                        (int)cbOriginal, cbDec);
            ::SecureZeroMemory(lz4, cbAligned);
            ::HeapFree(::GetProcessHeap(), 0, lz4);
            if (!dec) return FALSE;
            BOOL equal = (cbDec == (int)cbData) && (memcmp(dec, pData, cbData) == 0);
            ::SecureZeroMemory(dec, cbDec);
            ::HeapFree(::GetProcessHeap(), 0, dec);
            return equal;
        }

        // Texto: comparacion in-RAM (item.pData esta cifrado con DPAPI).
        DWORD cbDec = 0;
        LPVOID dec = DecryptCopyInternal(item, &cbDec);
        if (!dec) return FALSE;
        BOOL equal = (memcmp(dec, pData, cbData) == 0);
        ::SecureZeroMemory(dec, cbDec);
        ::HeapFree(::GetProcessHeap(), 0, dec);
        return equal;
    }

    int HistoryBuffer::Add(ItemKind kind, const void* pData, DWORD cbData) {
        if (!pData || cbData == 0) return -1;

        // Size guard: prevents a 50 MB DIB or a 20 MB dump
        // from bloating the process's RAM. Maintains the Win7 philosophy (I hope, lol)
        // native: 15 items, limited footprint.
        DWORD cbLimit = (kind == ItemKind::Image) ? kMaxImageBytes : kMaxTextBytes;
        if (cbData > cbLimit) return -1;

        CriticalSectionLock lock(m_cs);

        if (!m_items.empty() && IsDuplicate(m_items[0], kind, pData, cbData)) {
            return -1;
        }

        if (static_cast<int>(m_items.size()) >= kHistoryMax) {
            int dropIdx = -1;
            for (int i = static_cast<int>(m_items.size()) - 1; i >= 0; --i) {
                if (!m_items[i].pinned) {
                    dropIdx = i;
                    break;
                }
            }
            if (dropIdx < 0) {
                return -1;
            }
            DestroyItem(m_items[dropIdx]);
            m_items.erase(m_items.begin() + dropIdx);
        }

        HistoryItem it;
        it.kind = kind;
        it.cbData = cbData;
        it.pinned = FALSE;
        ::GetSystemTimeAsFileTime(&it.ftCaptured);

        if (kind == ItemKind::Image) {
            // LZ4 (lossless)
            int cbLz4 = 0;
            LPVOID lz4 = Lz4Compress(pData, (int)cbData, cbLz4);
            if (!lz4) return -1;

            // DPAPI over the LZ4 block
            SIZE_T cbEnc = 0;
            LPVOID enc = Protect(lz4, (DWORD)cbLz4, cbEnc);
            ::SecureZeroMemory(lz4, cbLz4);
            ::HeapFree(::GetProcessHeap(), 0, lz4);
            if (!enc) return -1;

            // Persists to disk. pData does NOT remain in RAM (pData = nullptr).
            WCHAR path[MAX_PATH] = {};
            if (!PersistImageToTemp(enc, (DWORD)cbEnc, cbData,
                                     path, MAX_PATH)) {
                ::SecureZeroMemory(enc, cbEnc);
                ::HeapFree(::GetProcessHeap(), 0, enc);
                return -1;
            }
            ::SecureZeroMemory(enc, cbEnc);
            ::HeapFree(::GetProcessHeap(), 0, enc);

            it.cbLz4 = (DWORD)cbLz4;
            it.cbStored = (DWORD)cbEnc;
            it.pData = nullptr;
            it.cbAlloc = 0;
            ::wcscpy_s(it.szTempPath, MAX_PATH, path);
            it.bHasTemp = TRUE;
        }
        else {
            // Encrypted text in RAM
            SIZE_T cbAlloc = 0;
            LPVOID enc = Protect(pData, cbData, cbAlloc);
            if (!enc) return -1;
            it.pData = enc;
            it.cbAlloc = cbAlloc;
        }

        m_items.insert(m_items.begin(), it);
        return 0;
    }

    BOOL HistoryBuffer::Remove(int index) {
        BOOL wasPinned = FALSE;
        HistoryItem snapshot;
        {
            CriticalSectionLock lock(m_cs);
            if (index < 0 || index >= static_cast<int>(m_items.size())) return FALSE;
            wasPinned = m_items[index].pinned;
            snapshot = m_items[index];
            DestroyItem(m_items[index]);
            m_items.erase(m_items.begin() + index);
        }
        // Si era un anclado persistido, tambien borramos su .acp.
        if (wasPinned) {
            DeletePinnedFromDisk(snapshot);
        }
        return TRUE;
    }

    int HistoryBuffer::RemoveUnpinned() {
        CriticalSectionLock lock(m_cs);
        int removed = 0;
        for (size_t i = 0; i < m_items.size();) {
            if (!m_items[i].pinned) {
                DestroyItem(m_items[i]);
                m_items.erase(m_items.begin() + i);
                ++removed;
            }
            else {
                ++i;
            }
        }
        return removed;
    }

    BOOL HistoryBuffer::TogglePin(int index) {
        // Etapa 1: tomar lock, validar y mutar el flag. Liberar el lock
        // antes de cualquier IO de disco o callback para evitar reentrancy.
        BOOL goingPinned = FALSE;
        BOOL rejected = FALSE;
        std::function<void()> cbCopy;
        HistoryItem snapshot;
        {
            CriticalSectionLock lock(m_cs);
            if (index < 0 || index >= static_cast<int>(m_items.size())) return FALSE;
            goingPinned = !m_items[index].pinned;

            // Limite estricto: si vamos a PIN y ya hay kMaxPinned, rechazamos.
            if (goingPinned) {
                int cur = 0;
                for (const auto& it : m_items) if (it.pinned) ++cur;
                if (cur >= kMaxPinned) {
                    rejected = TRUE;
                    cbCopy = m_onPinLimitExceeded;
                }
            }
            if (!rejected) {
                m_items[index].pinned = goingPinned;
                snapshot = m_items[index];
            }
        } // lock liberado aqui

        if (rejected) {
            if (cbCopy) cbCopy();
            return FALSE;
        }

        // Etapa 2: persistencia fuera del lock.
        if (snapshot.pinned) {
            SavePinnedToDisk(snapshot);
        } else {
            DeletePinnedFromDisk(snapshot);
        }
        return TRUE;
    }

    int HistoryBuffer::Count() const {
        CriticalSectionLock lock(m_cs);
        return static_cast<int>(m_items.size());
    }

    HistoryItem HistoryBuffer::Get(int index) const {
        CriticalSectionLock lock(m_cs);
        if (index < 0 || index >= static_cast<int>(m_items.size())) return HistoryItem{};
        return m_items[index];
    }

    BOOL HistoryBuffer::IsPinned(int index) const {
        CriticalSectionLock lock(m_cs);
        if (index < 0 || index >= static_cast<int>(m_items.size())) return FALSE;
        return m_items[index].pinned;
    }

    BOOL HistoryBuffer::IsSameAsLast(const void* pData, DWORD cbData) const {
        if (!pData || cbData == 0) return FALSE;
        CriticalSectionLock lock(m_cs);
        if (m_items.empty()) return FALSE;
        return IsDuplicate(m_items[0], m_items[0].kind, pData, cbData);
    }

    // -------------------------------------------------------------------
// Disk backing: persistence and reading of the LZ4+DPAPI block on disk
// -------------------------------------------------------------------

    BOOL HistoryBuffer::PersistImageToTemp(const void* pCompressed, DWORD cbCompressed,
                                            DWORD cbOriginal,
                                            WCHAR* outPath, DWORD cchOutPath) {
        if (!pCompressed || cbCompressed == 0 || !outPath || cchOutPath == 0) return FALSE;
        if (m_tempDir[0] == 0) return FALSE;

        const DWORD pid = ::GetCurrentProcessId();
        ::swprintf_s(outPath, cchOutPath, L"%s%lu_%lu.bin",
                     m_tempDir, pid, m_seq++);

        HANDLE h = ::CreateFileW(outPath, GENERIC_WRITE, 0, nullptr,
                                 CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_TEMPORARY |
                                 FILE_FLAG_SEQUENTIAL_SCAN,
                                 nullptr);
        if (h == INVALID_HANDLE_VALUE) return FALSE;

        BOOL ok = TRUE;
        DWORD cbWrote = 0;

        // Header: [cbOriginal][cbCompressed]
        struct Header { DWORD cbOriginal; DWORD cbCompressed; } hdr{ cbOriginal, cbCompressed };
        if (!::WriteFile(h, &hdr, sizeof(hdr), &cbWrote, nullptr) || cbWrote != sizeof(hdr))
            ok = FALSE;

        if (ok) {
            const BYTE* p = static_cast<const BYTE*>(pCompressed);
            while (ok && cbCompressed > 0) {
                DWORD chunk = (cbCompressed > 1u << 16) ? (1u << 16) : cbCompressed;
                if (!::WriteFile(h, p, chunk, &cbWrote, nullptr) || cbWrote != chunk)
                    ok = FALSE;
                else { p += chunk; cbCompressed -= chunk; }
            }
        }

        ::FlushFileBuffers(h);
        ::CloseHandle(h);

        if (!ok) {
            ::DeleteFileW(outPath);
            outPath[0] = 0;
            return FALSE;
        }
        return TRUE;
    }

    BOOL HistoryBuffer::LoadImageFromTemp(const HistoryItem& item,
                                           std::vector<BYTE>& outCompressed,
                                           DWORD* pcbOriginal) {
        if (pcbOriginal) *pcbOriginal = 0;
        outCompressed.clear();
        if (!item.bHasTemp || !item.szTempPath[0]) return FALSE;

        HANDLE h = ::CreateFileW(item.szTempPath, GENERIC_READ,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING,
                                 FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (h == INVALID_HANDLE_VALUE) return FALSE;

        struct Header { DWORD cbOriginal; DWORD cbStored; } hdr{};
        DWORD cbRead = 0;
        BOOL ok = ::ReadFile(h, &hdr, sizeof(hdr), &cbRead, nullptr)
                  && cbRead == sizeof(hdr);
        if (!ok || hdr.cbOriginal == 0 || hdr.cbStored == 0 ||
            hdr.cbOriginal > kMaxImageBytes || hdr.cbStored > (kMaxImageBytes * 2)) {
            ::CloseHandle(h);
            return FALSE;
        }

        outCompressed.resize(hdr.cbStored);
        BYTE* p = outCompressed.data();
        DWORD remaining = hdr.cbStored;
        while (ok && remaining > 0) {
            DWORD chunk = (remaining > 1u << 16) ? (1u << 16) : remaining;
            if (!::ReadFile(h, p, chunk, &cbRead, nullptr) || cbRead != chunk)
                ok = FALSE;
            else { p += chunk; remaining -= chunk; }
        }
        ::CloseHandle(h);
        if (!ok) { outCompressed.clear(); return FALSE; }

        if (pcbOriginal) *pcbOriginal = hdr.cbOriginal;
        return TRUE;
    }

    void HistoryBuffer::DeleteImageTemp(const HistoryItem& item) {
        if (item.bHasTemp && item.szTempPath[0]) {
            ::DeleteFileW(item.szTempPath);
        }
    }

    // -------------------------------------------------------------------
    // Cleanup: orphans (any PID) + by own PID (during shutdown)
    // -------------------------------------------------------------------

    static void DeleteMatching(const WCHAR* dir, const WCHAR* pattern) {
        WCHAR fullPattern[MAX_PATH];
        ::swprintf_s(fullPattern, MAX_PATH, L"%s%s", dir, pattern);
        WIN32_FIND_DATAW fd{};
        HANDLE h = ::FindFirstFileW(fullPattern, &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            WCHAR full[MAX_PATH];
            ::swprintf_s(full, MAX_PATH, L"%s%s", dir, fd.cFileName);
            ::DeleteFileW(full);
        } while (::FindNextFileW(h, &fd));
        ::FindClose(h);
    }

    void HistoryBuffer::CleanupOrphanTemps() {
        CriticalSectionLock lock(m_cs);
        if (m_tempDir[0] == 0) return;
        DeleteMatching(m_tempDir, L"*.bin");
    }

    void HistoryBuffer::CleanupByCurrentPid() {
        CriticalSectionLock lock(m_cs);
        if (m_tempDir[0] == 0) return;
        const DWORD pid = ::GetCurrentProcessId();
        WCHAR pat[64];
        ::swprintf_s(pat, 64, L"%lu_*.bin", pid);
        DeleteMatching(m_tempDir, pat);
    }

    // -------------------------------------------------------------------
// Thumbnail cache (JPEG, low quality, ~1.5 KB)
// -------------------------------------------------------------------

    void HistoryBuffer::ReleaseRender() {
        CriticalSectionLock lock(m_cacheCs);
        m_cache.clear();
        m_renderReady = FALSE;
    }

    BOOL HistoryBuffer::IsRenderReady() const {
        CriticalSectionLock lock(m_cacheCs);
        return m_renderReady;
    }

    const RenderItem& HistoryBuffer::GetCached(int index) const {
        CriticalSectionLock lock(m_cacheCs);
        if (index < 0 || index >= (int)m_cache.size()) {
            static const RenderItem kEmpty{};
            return kEmpty;
        }
        return m_cache[index];
    }

    void HistoryBuffer::PrepareRender() {
        std::vector<RenderItem> newCache;
        {
            CriticalSectionLock lock(m_cs);
            newCache.resize(m_items.size());
            for (size_t i = 0; i < m_items.size(); ++i) {
                const HistoryItem& it = m_items[i];
                if (it.kind == ItemKind::Text && it.pData) {
                    DWORD cbDec = 0;
                    LPVOID dec = DecryptCopyInternal(it, &cbDec);
                    if (dec) {

                        const wchar_t* wsz = static_cast<const wchar_t*>(dec);
                        size_t cw = cbDec / sizeof(wchar_t);
                        size_t cch = wcsnlen(wsz, cw);
                        newCache[i].text.assign(wsz, cch);
                        ::SecureZeroMemory(dec, cbDec);
                        ::HeapFree(::GetProcessHeap(), 0, dec);
                    }
                }
                else if (it.kind == ItemKind::Image) {
                    newCache[i].text.clear();
                    if (it.bHasTemp && it.cbLz4 > 0) {
                        std::vector<BYTE> storedDpapi;
                        DWORD cbOriginal = 0;
                        if (LoadImageFromTemp(it, storedDpapi, &cbOriginal)) {
                           
                            SIZE_T cbAligned = ((SIZE_T)storedDpapi.size() + 15) & ~(SIZE_T)15;
                            LPVOID lz4 = ::HeapAlloc(::GetProcessHeap(),
                                                     HEAP_ZERO_MEMORY, cbAligned);
                            if (lz4) {
                                if (!storedDpapi.empty())
                                    memcpy(lz4, storedDpapi.data(), storedDpapi.size());
                                if (::CryptUnprotectMemory(lz4,
                                                            (DWORD)cbAligned,
                                                            CRYPTPROTECTMEMORY_SAME_PROCESS)) {

                                    int cbDib = 0;
                                    LPVOID dib = Lz4Decompress(lz4,
                                                               (int)it.cbLz4,
                                                               (int)cbOriginal, cbDib);
                                    if (dib) {
                                        newCache[i].thumbnailJpeg =
                                            MakeThumbnailJpeg((BYTE*)dib, (DWORD)cbDib);
                                        ::SecureZeroMemory(dib, cbDib);
                                        ::HeapFree(::GetProcessHeap(), 0, dib);
                                    }
                                }
                                ::SecureZeroMemory(lz4, cbAligned);
                                ::HeapFree(::GetProcessHeap(), 0, lz4);
                            }
                        }
                    }
                }
            }
        }
        {
            CriticalSectionLock lock(m_cacheCs);
            m_cache = std::move(newCache);
            m_renderReady = TRUE;
        }
    }

    // -------------------------------------------------------------------
    // Persistencia de anclados (NUEVO, no rompe nada existente)
    //
    // Idea: cada item anclado se serializa a %LOCALAPPDATA%\AeroClip\Pinned\
    //   <seq>.acp con un PinnedHeader de 28 bytes + cuerpo cifrado con
    //   CryptProtectData (DPAPI usuario). Asi sobrevive a cierres, reinicios
    //   y apagados del PC, y solo el mismo usuario de Windows puede
    //   descifrarlo.
    //
    // La representacion en RAM sigue siendo la misma: textos en it.pData
    //   con CryptProtectMemory (in-process), imagenes en %TEMP%\AeroClip\.
    // En el momento del pin/unpin convertimos entre las dos formas.
    // -------------------------------------------------------------------

    static BOOL CreateDirectoryTreeW(const WCHAR* path) {
        if (!path || !*path) return FALSE;
        WCHAR copy[MAX_PATH];
        ::wcscpy_s(copy, MAX_PATH, path);
        for (WCHAR* p = copy; *p; ++p) {
            if ((*p == L'\\' || *p == L'/') && p != copy) {
                *p = 0;
                if (!::CreateDirectoryW(copy, nullptr)) {
                    DWORD e = ::GetLastError();
                    if (e != ERROR_ALREADY_EXISTS && e != ERROR_ACCESS_DENIED) {
                        // seguir intentando; puede que ya exista
                    }
                }
                *p = L'\\';
            }
        }
        if (!::CreateDirectoryW(path, nullptr)) {
            DWORD e = ::GetLastError();
            if (e != ERROR_ALREADY_EXISTS) return FALSE;
        }
        return TRUE;
    }

    void HistoryBuffer::InitPinDir() {
        WCHAR localAppData[MAX_PATH] = {};
        if (FAILED(::SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr,
                                       SHGFP_TYPE_CURRENT, localAppData))) {
            m_pinDir[0] = 0;
            return;
        }
        ::swprintf_s(m_pinDir, MAX_PATH, L"%s\\AeroClip\\Pinned\\", localAppData);
        CreateDirectoryTreeW(m_pinDir);
        m_pinSeq = 0;
    }

    void HistoryBuffer::SetOnPinLimitExceeded(std::function<void()> cb) {
        m_onPinLimitExceeded = std::move(cb);
    }

    int HistoryBuffer::PinnedCount() const {
        CriticalSectionLock lock(m_cs);
        int n = 0;
        for (const auto& it : m_items) if (it.pinned) ++n;
        return n;
    }

    void HistoryBuffer::GetPinnedFilePath(const HistoryItem& it, WCHAR* out, DWORD cch) const {
        if (!out || cch == 0) return;
        // El nombre usa ftCaptured para mantener orden estable y uniqueness.
        ULONGLONG ts = (static_cast<ULONGLONG>(it.ftCaptured.dwHighDateTime) << 32)
                     | it.ftCaptured.dwLowDateTime;
        ::swprintf_s(out, cch, L"%s%016llx_%u.acp",
                     m_pinDir, ts, ::GetCurrentProcessId());
    }

    BOOL HistoryBuffer::SavePinnedToDisk(const HistoryItem& it) {
        if (m_pinDir[0] == 0) return FALSE;
        if (it.kind != ItemKind::Text && it.kind != ItemKind::Image) return FALSE;

        // 1. Sacar el cuerpo en plano: para texto el wchar_t descifrado, para
        //    imagen los bytes LZ4 descifrados (lo que iba dentro del DPAPI
        //    in-process del temp file).
        std::vector<BYTE> plain;
        DWORD cbOriginal = it.cbData;
        DWORD cbLz4 = 0;

        if (it.kind == ItemKind::Text) {
            if (!it.pData || !it.cbAlloc) return FALSE;
            DWORD cbDec = 0;
            LPVOID dec = DecryptCopyInternal(it, &cbDec);
            if (!dec) return FALSE;
            plain.assign(static_cast<BYTE*>(dec), static_cast<BYTE*>(dec) + cbDec);
            ::SecureZeroMemory(dec, cbDec);
            ::HeapFree(::GetProcessHeap(), 0, dec);
        }
        else { // Image
            if (!it.bHasTemp || !it.szTempPath[0] || it.cbLz4 == 0) return FALSE;
            std::vector<BYTE> storedDpapi;
            DWORD cbOrig = 0;
            if (!LoadImageFromTemp(it, storedDpapi, &cbOrig)) return FALSE;
            SIZE_T cbAligned = ((SIZE_T)storedDpapi.size() + 15) & ~(SIZE_T)15;
            LPVOID lz4 = ::HeapAlloc(::GetProcessHeap(), HEAP_ZERO_MEMORY, cbAligned);
            if (!lz4) return FALSE;
            if (!storedDpapi.empty())
                memcpy(lz4, storedDpapi.data(), storedDpapi.size());
            if (!::CryptUnprotectMemory(lz4, (DWORD)cbAligned,
                                         CRYPTPROTECTMEMORY_SAME_PROCESS)) {
                ::SecureZeroMemory(lz4, cbAligned);
                ::HeapFree(::GetProcessHeap(), 0, lz4);
                return FALSE;
            }
            plain.assign((BYTE*)lz4, (BYTE*)lz4 + it.cbLz4);
            ::SecureZeroMemory(lz4, cbAligned);
            ::HeapFree(::GetProcessHeap(), 0, lz4);
            cbLz4 = it.cbLz4;
            cbOriginal = cbOrig;
        }

        // 2. Cifrar el cuerpo con DPAPI cross-process (clave de usuario).
        DATA_BLOB in{};
        in.pbData = plain.empty() ? nullptr : plain.data();
        in.cbData = (DWORD)plain.size();
        DATA_BLOB out{};
        if (!::CryptProtectData(&in, L"AeroClipPinned", nullptr, nullptr, nullptr,
                                 CRYPTPROTECT_UI_FORBIDDEN, &out)) {
            ::SecureZeroMemory(plain.data(), plain.size());
            return FALSE;
        }
        ::SecureZeroMemory(plain.data(), plain.size());

        // 3. Construir path y escribir.
        WCHAR path[MAX_PATH] = {};
        GetPinnedFilePath(it, path, MAX_PATH);

        HANDLE h = ::CreateFileW(path, GENERIC_WRITE, 0, nullptr,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            ::LocalFree(out.pbData);
            return FALSE;
        }

        PinnedHeader hdr{};
        hdr.magic = 0x42435041; // 'ACPB' little-endian
        hdr.version = 1;
        hdr.kind = static_cast<BYTE>(it.kind);
        hdr.cbData = cbOriginal;
        hdr.cbLz4 = cbLz4;
        hdr.cbStored = out.cbData;
        hdr.ftCaptured = it.ftCaptured;

        DWORD cbWrote = 0;
        BOOL ok = ::WriteFile(h, &hdr, sizeof(hdr), &cbWrote, nullptr)
                  && cbWrote == sizeof(hdr);

        const BYTE* p = out.pbData;
        DWORD remaining = out.cbData;
        while (ok && remaining > 0) {
            DWORD chunk = (remaining > (1u << 16)) ? (1u << 16) : remaining;
            if (!::WriteFile(h, p, chunk, &cbWrote, nullptr) || cbWrote != chunk)
                ok = FALSE;
            else { p += chunk; remaining -= chunk; }
        }
        ::FlushFileBuffers(h);
        ::CloseHandle(h);
        ::LocalFree(out.pbData);

        if (!ok) {
            ::DeleteFileW(path);
            return FALSE;
        }
        return TRUE;
    }

    void HistoryBuffer::DeletePinnedFromDisk(const HistoryItem& it) {
        if (m_pinDir[0] == 0) return;
        WCHAR path[MAX_PATH] = {};
        GetPinnedFilePath(it, path, MAX_PATH);
        // Puede que no exista (p. ej. si el item nunca llego a guardarse).
        ::DeleteFileW(path);
        // Tambien borrar cualquier variant del mismo timestamp+PID (defensa).
        WCHAR pattern[MAX_PATH];
        ULONGLONG ts = (static_cast<ULONGLONG>(it.ftCaptured.dwHighDateTime) << 32)
                     | it.ftCaptured.dwLowDateTime;
        ::swprintf_s(pattern, MAX_PATH, L"%s%016llx_*.acp", m_pinDir, ts);
        WIN32_FIND_DATAW fd{};
        HANDLE h = ::FindFirstFileW(pattern, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                WCHAR full[MAX_PATH];
                ::swprintf_s(full, MAX_PATH, L"%s%s", m_pinDir, fd.cFileName);
                ::DeleteFileW(full);
            } while (::FindNextFileW(h, &fd));
            ::FindClose(h);
        }
    }

    BOOL HistoryBuffer::LoadPinnedFromFile(const WCHAR* path, HistoryItem& out) {
        HANDLE h = ::CreateFileW(path, GENERIC_READ,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return FALSE;

        PinnedHeader hdr{};
        DWORD cbRead = 0;
        if (!::ReadFile(h, &hdr, sizeof(hdr), &cbRead, nullptr)
            || cbRead != sizeof(hdr)) {
            ::CloseHandle(h); return FALSE;
        }
        if (hdr.magic != 0x42435041 || hdr.version != 1) {
            ::CloseHandle(h); return FALSE;
        }
        if (hdr.kind != static_cast<BYTE>(ItemKind::Text)
         && hdr.kind != static_cast<BYTE>(ItemKind::Image)) {
            ::CloseHandle(h); return FALSE;
        }
        if (hdr.cbStored == 0 || hdr.cbData == 0
         || hdr.cbData > kMaxImageBytes) {
            ::CloseHandle(h); return FALSE;
        }

        std::vector<BYTE> body(hdr.cbStored);
        BYTE* p = body.data();
        DWORD remaining = hdr.cbStored;
        BOOL ok = TRUE;
        while (ok && remaining > 0) {
            DWORD chunk = (remaining > (1u << 16)) ? (1u << 16) : remaining;
            if (!::ReadFile(h, p, chunk, &cbRead, nullptr) || cbRead != chunk)
                ok = FALSE;
            else { p += chunk; remaining -= chunk; }
        }
        ::CloseHandle(h);
        if (!ok) return FALSE;

        // Descifrar con DPAPI cross-process.
        DATA_BLOB in{};
        in.pbData = body.data();
        in.cbData = hdr.cbStored;
        DATA_BLOB plain{};
        if (!::CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr,
                                   CRYPTPROTECT_UI_FORBIDDEN, &plain)) {
            return FALSE;
        }

        // Construir HistoryItem en la representacion in-RAM.
        out = HistoryItem{};
        out.kind = static_cast<ItemKind>(hdr.kind);
        out.cbData = hdr.cbData;
        out.cbLz4 = hdr.cbLz4;
        out.cbStored = hdr.cbStored;
        out.pinned = TRUE;
        out.ftCaptured = hdr.ftCaptured;

        if (out.kind == ItemKind::Text) {
            if (plain.cbData != hdr.cbData) {
                ::LocalFree(plain.pbData);
                return FALSE;
            }
            SIZE_T cbAligned = ((SIZE_T)plain.cbData + 15) & ~(SIZE_T)15;
            LPVOID enc = ::HeapAlloc(::GetProcessHeap(), HEAP_ZERO_MEMORY, cbAligned);
            if (!enc) { ::LocalFree(plain.pbData); return FALSE; }
            memcpy(enc, plain.pbData, plain.cbData);
            ::SecureZeroMemory(plain.pbData, plain.cbData);
            ::LocalFree(plain.pbData);
            if (!::CryptProtectMemory(enc, (DWORD)cbAligned,
                                       CRYPTPROTECTMEMORY_SAME_PROCESS)) {
                ::SecureZeroMemory(enc, cbAligned);
                ::HeapFree(::GetProcessHeap(), 0, enc);
                return FALSE;
            }
            out.pData = enc;
            out.cbAlloc = cbAligned;
        }
        else { // Image
            if (plain.cbData != hdr.cbLz4) {
                ::SecureZeroMemory(plain.pbData, plain.cbData);
                ::LocalFree(plain.pbData);
                return FALSE;
            }
            // Re-cifrar con CryptProtectMemory SAME_PROCESS y persistir a
            // un nuevo temp file bajo el PID actual.
            SIZE_T cbAligned = ((SIZE_T)plain.cbData + 15) & ~(SIZE_T)15;
            LPVOID enc = ::HeapAlloc(::GetProcessHeap(), HEAP_ZERO_MEMORY, cbAligned);
            if (!enc) {
                ::SecureZeroMemory(plain.pbData, plain.cbData);
                ::LocalFree(plain.pbData);
                return FALSE;
            }
            memcpy(enc, plain.pbData, plain.cbData);
            ::SecureZeroMemory(plain.pbData, plain.cbData);
            ::LocalFree(plain.pbData);
            if (!::CryptProtectMemory(enc, (DWORD)cbAligned,
                                       CRYPTPROTECTMEMORY_SAME_PROCESS)) {
                ::SecureZeroMemory(enc, cbAligned);
                ::HeapFree(::GetProcessHeap(), 0, enc);
                return FALSE;
            }
            WCHAR path[MAX_PATH] = {};
            if (!PersistImageToTemp(enc, (DWORD)cbAligned, out.cbData,
                                     path, MAX_PATH)) {
                ::SecureZeroMemory(enc, cbAligned);
                ::HeapFree(::GetProcessHeap(), 0, enc);
                return FALSE;
            }
            ::SecureZeroMemory(enc, cbAligned);
            ::HeapFree(::GetProcessHeap(), 0, enc);
            ::wcscpy_s(out.szTempPath, MAX_PATH, path);
            out.bHasTemp = TRUE;
        }
        return TRUE;
    }

    int HistoryBuffer::LoadPinnedFromDisk() {
        if (m_pinDir[0] == 0) return 0;
        CriticalSectionLock lock(m_cs);

        WCHAR pattern[MAX_PATH];
        ::swprintf_s(pattern, MAX_PATH, L"%s*.acp", m_pinDir);
        WIN32_FIND_DATAW fd{};
        HANDLE h = ::FindFirstFileW(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE) return 0;

        int loaded = 0;
        do {
            WCHAR full[MAX_PATH];
            ::swprintf_s(full, MAX_PATH, L"%s%s", m_pinDir, fd.cFileName);
            HistoryItem it;
            if (LoadPinnedFromFile(full, it)) {
                // Insertar al principio para que se vean arriba como pinned.
                m_items.insert(m_items.begin(), it);
                ++loaded;
            }
            // Si falla, dejamos el .acp en sitio; el siguiente TogglePin
            // lo sobreescribira con datos buenos.
        } while (::FindNextFileW(h, &fd));
        ::FindClose(h);
        return loaded;
    }

} // namespace AeroClip::Storage