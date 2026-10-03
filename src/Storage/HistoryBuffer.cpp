#include "Storage/HistoryBuffer.h"
#include <dpapi.h>

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
    // Miniatura: DIB -> 48x48 grayscale -> JPEG calidad 50 (~1.5 KB)
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

        // 1) Construir HBITMAP desde DIB usando CreateDIBSection -> siempre
        //    soporta top-down y bottom-up sin condiciones raras de paleta.
        BITMAPINFO bmi{};
        bmi.bmiHeader = *bih;
        bmi.bmiHeader.biCompression = BI_RGB;
        bmi.bmiHeader.biHeight = cy;     // forzar top-down positivo
        bmi.bmiHeader.biSizeImage = 0;

        LPVOID bits = nullptr;
        HBITMAP hbm = ::CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!hbm || !bits) { if (hbm) ::DeleteObject(hbm); return out; }

        // 2) Copiar pixeles del DIB original al DIB top-down (saltamos la
        //    cabecera y la paleta; el offset de pixeles empieza justo despues).
        DWORD headerSize = bih->biSize + bih->biClrUsed * sizeof(RGBQUAD);
        LONG rowBytes = ((bih->biWidth * bih->biBitCount + 31) / 32) * 4;
        SIZE_T imgBytes = (SIZE_T)rowBytes * cy;
        if (headerSize + imgBytes > cbDib) { ::DeleteObject(hbm); return out; }
        const BYTE* src = dib + headerSize;
        if (bih->biHeight > 0) {
            // bottom-up: copiar filas en orden inverso a top-down
            BYTE* dst = static_cast<BYTE*>(bits);
            for (LONG y = 0; y < cy; ++y) {
                memcpy(dst + y * rowBytes, src + (cy - 1 - y) * rowBytes, rowBytes);
            }
        } else {
            // top-down: copiar tal cual
            memcpy(bits, src, imgBytes);
        }

        // 3) HBITMAP -> Gdiplus::Bitmap
        Gdiplus::Bitmap srcBmp(hbm, nullptr);
        if (srcBmp.GetLastStatus() != Gdiplus::Ok) { ::DeleteObject(hbm); return out; }

        // 4) Render en 48x48 con interpolacion alta.
        const int kSide = 48;
        Gdiplus::Bitmap thumb(kSide, kSide, PixelFormat24bppRGB);
        Gdiplus::Graphics g(&thumb);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
        g.Clear(Gdiplus::Color::White);
        g.DrawImage(&srcBmp, 0, 0, kSide, kSide);
        ::DeleteObject(hbm);

        // 5) Convertir a escala de grises (BT.601 luma).
        Gdiplus::Bitmap gray(kSide, kSide, PixelFormat24bppRGB);
        Gdiplus::Graphics gg(&gray);
        gg.Clear(Gdiplus::Color::White);
        for (int y = 0; y < kSide; ++y) {
            for (int x = 0; x < kSide; ++x) {
                Gdiplus::Color c;
                thumb.GetPixel(x, y, &c);
                BYTE r = c.GetR(), gC = c.GetG(), b = c.GetB();
                BYTE l = static_cast<BYTE>((299 * r + 587 * gC + 114 * b) / 1000);
                gray.SetPixel(x, y, Gdiplus::Color(255, l, l, l));
            }
        }

        // 6) Encode a JPEG q50.
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
        ULONG q = 50;
        enc.Parameter[0].Value = &q;

        if (gray.Save(stream, &jpegClsid, &enc) != Gdiplus::Ok) return out;

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

        // Sweep de huerfanos de cualquier PID anterior. Es seguro borrar
        // todos los *.bin en m_tempDir al iniciar: cada archivo lleva
        // <pid>_<seq>.bin y nuestro PID es unico, asi que cualquier archivo
        // que no sea nuestro es huerfano de un proceso muerto.
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
        // Sweep final por PID propio (incluye anclados: al terminar el
        // proceso se borra incluso lo anclado, porque la persistencia
        // entre sesiones no es un requisito).
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
        // Si tiene archivo temporal y NO esta anclado, borrarlo.
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

        // Imagen: comparacion exige leer del disco + descomprimir.
        if (item.kind == ItemKind::Image && item.bHasTemp) {
            std::vector<BYTE> compressed;
            DWORD cbOriginal = 0;
            if (!LoadImageFromTemp(item, compressed, &cbOriginal)) return FALSE;
            int cbDec = 0;
            LPVOID dec = Lz4Decompress(compressed.data(), (int)compressed.size(),
                                        (int)cbOriginal, cbDec);
            compressed.clear();
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

        // Guardia de tamano: evita que un DIB de 50 MB o un volcado de
        // 20 MB inche la RAM del proceso. Mantenemos la filosofia Win7
        // native: 15 items, footprint acotado.
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
            // 1) LZ4 (sin perdida)
            int cbLz4 = 0;
            LPVOID lz4 = Lz4Compress(pData, (int)cbData, cbLz4);
            if (!lz4) return -1;

            // 2) DPAPI sobre el bloque LZ4
            SIZE_T cbEnc = 0;
            LPVOID enc = Protect(lz4, (DWORD)cbLz4, cbEnc);
            ::SecureZeroMemory(lz4, cbLz4);
            ::HeapFree(::GetProcessHeap(), 0, lz4);
            if (!enc) return -1;

            // 3) Persiste en disco. pData NO queda en RAM (pData = nullptr).
            WCHAR path[MAX_PATH] = {};
            if (!PersistImageToTemp(enc, (DWORD)cbEnc, cbData,
                                     path, MAX_PATH)) {
                ::SecureZeroMemory(enc, cbEnc);
                ::HeapFree(::GetProcessHeap(), 0, enc);
                return -1;
            }
            ::SecureZeroMemory(enc, cbEnc);
            ::HeapFree(::GetProcessHeap(), 0, enc);

            it.cbStored = (DWORD)cbEnc;
            it.pData = nullptr;
            it.cbAlloc = 0;
            ::wcscpy_s(it.szTempPath, MAX_PATH, path);
            it.bHasTemp = TRUE;
        }
        else {
            // Texto: en RAM cifrado (sin cambio respecto a v1).
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
        CriticalSectionLock lock(m_cs);
        if (index < 0 || index >= static_cast<int>(m_items.size())) return FALSE;
        DestroyItem(m_items[index]);
        m_items.erase(m_items.begin() + index);
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
        CriticalSectionLock lock(m_cs);
        if (index < 0 || index >= static_cast<int>(m_items.size())) return FALSE;
        m_items[index].pinned = !m_items[index].pinned;
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
    // Disk backing: persistencia y lectura del bloque LZ4+DPAPI en disco
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
    // Cleanup: huerfanos (cualquier PID) + por PID propio (en shutdown)
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
    // Cache de miniaturas (JPEG, baja calidad, ~1.5 KB)
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

    // Preparar cache para todos los items visibles. Para texto: copia el
    // descifrado. Para imagen: lee de disco + descomprime + escala a 48x48
    // + JPEG q50 (grises). El detalle de la miniatura se implementa en
    // el siguiente todo (thumbnail-jpeg); por ahora el campo se deja
    // vacio para imagenes.
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
                        // Texto: copiar bytes descifrados a wstring (UTF-8
                        // no es valido en Windows clipboard como wide).
                        // Se asume wchar_t en el clipboard.
                        const wchar_t* wsz = static_cast<const wchar_t*>(dec);
                        size_t cw = cbDec / sizeof(wchar_t);
                        // truncar a primer nulo
                        size_t cch = wcsnlen(wsz, cw);
                        newCache[i].text.assign(wsz, cch);
                        ::SecureZeroMemory(dec, cbDec);
                        ::HeapFree(::GetProcessHeap(), 0, dec);
                    }
                }
                else if (it.kind == ItemKind::Image) {
                    newCache[i].text.clear();
                    if (it.bHasTemp) {
                        std::vector<BYTE> compressed;
                        DWORD cbOriginal = 0;
                        if (LoadImageFromTemp(it, compressed, &cbOriginal)) {
                            int cbDec = 0;
                            LPVOID dib = Lz4Decompress(compressed.data(),
                                                       (int)compressed.size(),
                                                       (int)cbOriginal, cbDec);
                            compressed.clear();
                            if (dib) {
                                newCache[i].thumbnailJpeg =
                                    MakeThumbnailJpeg((BYTE*)dib, (DWORD)cbDec);
                                ::SecureZeroMemory(dib, cbDec);
                                ::HeapFree(::GetProcessHeap(), 0, dib);
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

} // namespace AeroClip::Storage