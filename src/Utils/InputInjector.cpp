#include "Utils/InputInjector.h"
#include "Storage/HistoryBuffer.h"
#include <dpapi.h>
extern "C" {
#include "../Storage/lz4.h"
}
#include <cstring>

namespace AeroClip::Utils {

    // El clipboard de Windows es singleton: si ClipboardMonitor tiene
    // abierto el clipboard desde su hilo mientras el usuario hace clic en
    // una tarjeta, OpenClipboard aqui devolvera FALSE y el pegado se
    // perderia. Backoff corto y reintentos acotados.
    static BOOL TryOpenClipboardWithRetry(HWND hwnd, int maxAttempts = 5) {
        for (int i = 0; i < maxAttempts; ++i) {
            if (::OpenClipboard(hwnd)) return TRUE;
            // GetLastError suele ser ERROR_ACCESS_DENIED (5) cuando otro
            // hilo del mismo proceso lo tiene abierto. 10 ms es suficiente
            // para que el monitor termine su ProcessCurrent.
            ::Sleep(10);
        }
        return FALSE;
    }

    static BOOL SendCtrlV() {
        ::Sleep(50);
        INPUT in[4] = {};
        in[0].type = INPUT_KEYBOARD;
        in[0].ki.wVk = VK_CONTROL;

        in[1].type = INPUT_KEYBOARD;
        in[1].ki.wVk = 'V';

        in[2].type = INPUT_KEYBOARD;
        in[2].ki.wVk = 'V';
        in[2].ki.dwFlags = KEYEVENTF_KEYUP;

        in[3].type = INPUT_KEYBOARD;
        in[3].ki.wVk = VK_CONTROL;
        in[3].ki.dwFlags = KEYEVENTF_KEYUP;

        return ::SendInput(4, in, sizeof(INPUT)) == 4;
    }

    static BOOL SetClipboardTextW(LPCWSTR text, size_t lenChars) {
        if (!text || lenChars == 0) return FALSE;
        HGLOBAL h = ::GlobalAlloc(GMEM_MOVEABLE, (lenChars + 1) * sizeof(WCHAR));
        if (!h) return FALSE;

        LPVOID p = ::GlobalLock(h);
        std::memcpy(p, text, lenChars * sizeof(WCHAR));
        static_cast<LPWSTR>(p)[lenChars] = 0;
        ::GlobalUnlock(h);

        if (!::SetClipboardData(CF_UNICODETEXT, h)) {
            ::GlobalFree(h);
            return FALSE;
        }
        return TRUE;
    }

    static BOOL SetClipboardDib(const void* pDib, DWORD cbDib) {
        if (!pDib || cbDib < sizeof(BITMAPINFOHEADER)) return FALSE;
        HGLOBAL h = ::GlobalAlloc(GMEM_MOVEABLE, cbDib);
        if (!h) return FALSE;

        LPVOID p = ::GlobalLock(h);
        std::memcpy(p, pDib, cbDib);
        ::GlobalUnlock(h);

        if (!::SetClipboardData(CF_DIB, h)) {
            ::GlobalFree(h);
            return FALSE;
        }
        return TRUE;
    }

    BOOL InputInjector::PasteText(const AeroClip::HistoryItem& item) {
        DWORD cb = 0;
        LPVOID data = AeroClip::Storage::HistoryBuffer::DecryptCopy(item, &cb);
        if (!data || cb < sizeof(WCHAR)) {
            if (data) {
                ::SecureZeroMemory(data, cb);
                ::HeapFree(::GetProcessHeap(), 0, data);
            }
            return FALSE;
        }

        size_t chars = cb / sizeof(WCHAR);
        if (chars && static_cast<LPCWSTR>(data)[chars - 1] == 0) --chars;

        BOOL ok = FALSE;
        if (TryOpenClipboardWithRetry(nullptr)) {
            ::EmptyClipboard();
            ok = SetClipboardTextW(static_cast<LPCWSTR>(data), chars);
            ::CloseClipboard();
        }

        ::SecureZeroMemory(data, cb);
        ::HeapFree(::GetProcessHeap(), 0, data);

        if (ok) SendCtrlV();
        return ok;
    }

    BOOL InputInjector::PasteImage(const AeroClip::HistoryItem& item) {
        // Opcion C: las imagenes viven en disco (LZ4 + DPAPI). El original
        // descifrado solo existe aqui, en el momento del paste.
        if (item.kind != AeroClip::ItemKind::Image || !item.bHasTemp) {
            return FALSE;
        }

        std::vector<BYTE> compressedDpapi;
        DWORD cbCompressed = 0;
        if (!AeroClip::Storage::HistoryBuffer::LoadImageFromTemp(item, compressedDpapi, &cbCompressed)) {
            return FALSE;
        }

        // 1) DPAPI CryptUnprotectMemory (igual que HistoryBuffer::Protect).
        SIZE_T cbAligned = (cbCompressed + 15) & ~static_cast<SIZE_T>(15);
        LPVOID enc = ::HeapAlloc(::GetProcessHeap(), 0, cbAligned);
        if (!enc) return FALSE;
        if (cbCompressed > 0) memcpy(enc, compressedDpapi.data(), cbCompressed);
        if (cbAligned > cbCompressed) memset((BYTE*)enc + cbCompressed, 0, cbAligned - cbCompressed);

        BOOL dpapiOk = ::CryptUnprotectMemory(enc, static_cast<DWORD>(cbAligned),
                                              CRYPTPROTECTMEMORY_SAME_PROCESS);
        if (!dpapiOk) {
            ::HeapFree(::GetProcessHeap(), 0, enc);
            return FALSE;
        }
        // Ahora enc contiene bytes LZ4.

        // 2) LZ4 descomprimir para obtener el DIB original.
        DWORD cbDib = item.cbData;
        LPVOID dib = ::HeapAlloc(::GetProcessHeap(), 0, cbDib);
        if (!dib) { ::HeapFree(::GetProcessHeap(), 0, enc); return FALSE; }
        int n = LZ4_decompress_safe(static_cast<const char*>(enc),
                                     static_cast<char*>(dib),
                                     (int)item.cbLz4, (int)cbDib);
        ::SecureZeroMemory(enc, cbAligned);
        ::HeapFree(::GetProcessHeap(), 0, enc);
        if (n < 0 || (DWORD)n != cbDib) {
            ::HeapFree(::GetProcessHeap(), 0, dib);
            return FALSE;
        }

        // 3) Pegar al clipboard.
        BOOL ok = FALSE;
        if (TryOpenClipboardWithRetry(nullptr)) {
            ::EmptyClipboard();
            ok = SetClipboardDib(dib, cbDib);
            ::CloseClipboard();
        }

        ::SecureZeroMemory(dib, cbDib);
        ::HeapFree(::GetProcessHeap(), 0, dib);

        if (ok) SendCtrlV();
        return ok;
    }

} // namespace AeroClip::Utils