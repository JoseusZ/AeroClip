#include "Clipboard/DataConverter.h"

namespace AeroClip::Clipboard {

    BOOL DataConverter::ReadUnicodeText(LPCVOID* ppOut, DWORD* pcbOut) {
        if (!ppOut || !pcbOut) return FALSE;
        *ppOut = nullptr;
        *pcbOut = 0;

        if (!::IsClipboardFormatAvailable(CF_UNICODETEXT)) return FALSE;

        ClipboardSession cs(nullptr);
        if (!cs.ok) return FALSE;

        HANDLE h = ::GetClipboardData(CF_UNICODETEXT);
        if (!h) return FALSE;

        SIZE_T size = ::GlobalSize(h);
        if (size == 0) return FALSE;

        LPVOID src = ::GlobalLock(h);
        if (!src) return FALSE;

        LPVOID dst = ::HeapAlloc(::GetProcessHeap(), 0, size);
        if (dst) {
            memcpy(dst, src, size);
            *ppOut = dst;
            *pcbOut = static_cast<DWORD>(size);
        }

        ::GlobalUnlock(h);
        return (dst != nullptr);
    }

    BOOL DataConverter::ReadDibImage(LPCVOID* ppOut, DWORD* pcbOut) {
        if (!ppOut || !pcbOut) return FALSE;
        *ppOut = nullptr;
        *pcbOut = 0;

        if (!::IsClipboardFormatAvailable(CF_DIB)) return FALSE;

        ClipboardSession cs(nullptr);
        if (!cs.ok) return FALSE;

        HANDLE h = ::GetClipboardData(CF_DIB);
        if (!h) return FALSE;

        SIZE_T size = ::GlobalSize(h);
        if (size == 0) return FALSE;

        LPVOID src = ::GlobalLock(h);
        if (!src) return FALSE;

        LPVOID dst = ::HeapAlloc(::GetProcessHeap(), 0, size);
        if (dst) {
            memcpy(dst, src, size);
            *ppOut = dst;
            *pcbOut = static_cast<DWORD>(size);
        }

        ::GlobalUnlock(h);
        return (dst != nullptr);
    }

} // namespace AeroClip::Clipboard