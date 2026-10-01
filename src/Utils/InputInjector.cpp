#include "Utils/InputInjector.h"
#include "Storage/HistoryBuffer.h"
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
        DWORD cb = 0;
        LPVOID data = AeroClip::Storage::HistoryBuffer::DecryptCopy(item, &cb);
        if (!data || cb == 0) {
            if (data) {
                ::SecureZeroMemory(data, cb);
                ::HeapFree(::GetProcessHeap(), 0, data);
            }
            return FALSE;
        }

        BOOL ok = FALSE;
        if (TryOpenClipboardWithRetry(nullptr)) {
            ::EmptyClipboard();
            ok = SetClipboardDib(data, cb);
            ::CloseClipboard();
        }

        ::SecureZeroMemory(data, cb);
        ::HeapFree(::GetProcessHeap(), 0, data);

        if (ok) SendCtrlV();
        return ok;
    }

} // namespace AeroClip::Utils