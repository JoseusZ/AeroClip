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

        LPCWSTR src = static_cast<LPCWSTR>(::GlobalLock(h));
        if (!src) return FALSE;

        // FIX dedup texto: GlobalSize devuelve el tamano del bloque (puede
        // estar redondeado al alza y contener bytes basura tras el \0), por
        // lo que dos copias identicas de "awd" daban cbData distintos y
        // esquivaban el IsDuplicate por la guarda `cbData != cbData`.
        // Usamos la longitud real del string terminado en \0.
        SIZE_T chars = 0;
        while (src[chars] != 0) ++chars;
        SIZE_T sizeBytes = (chars + 1) * sizeof(WCHAR);
        if (sizeBytes == 0) { ::GlobalUnlock(h); return FALSE; }

        LPVOID dst = ::HeapAlloc(::GetProcessHeap(), 0, sizeBytes);
        if (dst) {
            memcpy(dst, src, sizeBytes);
            *ppOut = dst;
            *pcbOut = static_cast<DWORD>(sizeBytes);
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

    BOOL DataConverter::ReadBitmapAsDIB(LPCVOID* ppOut, DWORD* pcbOut) {
        if (!ppOut || !pcbOut) return FALSE;
        *ppOut = nullptr;
        *pcbOut = 0;

        if (!::IsClipboardFormatAvailable(CF_BITMAP)) return FALSE;

        ClipboardSession cs(nullptr);
        if (!cs.ok) return FALSE;

        HANDLE hh = ::GetClipboardData(CF_BITMAP);
                HBITMAP hbmp = reinterpret_cast<HBITMAP>(hh);
        if (!hbmp) return FALSE;

        // 1) Tamano del DDB.
        BITMAP bmp{};
        if (::GetObject(hbmp, sizeof(bmp), &bmp) == 0) return FALSE;
        if (bmp.bmWidth <= 0 || bmp.bmHeight <= 0 || bmp.bmPlanes != 1) return FALSE;
        LONG w = bmp.bmWidth;
        LONG h = bmp.bmHeight;
        WORD bpp = (bmp.bmBitsPixel == 32 || bmp.bmBitsPixel == 16) ? bmp.bmBitsPixel : 24;

        // 2) Reservar DIB destino (24/32 bpp BI_RGB, top-down positivo -> se
        //    reescribe abajo como bottom-up para que el resto del flujo lo
        //    siga tratando como CF_DIB normal).
        BITMAPINFOHEADER bih{};
        bih.biSize = sizeof(bih);
        bih.biWidth = w;
        bih.biHeight = h;            // bottom-up
        bih.biPlanes = 1;
        bih.biBitCount = bpp;
        bih.biCompression = BI_RGB;
        bih.biSizeImage = 0;

        HDC hdc = ::GetDC(nullptr);
        if (!hdc) return FALSE;

        BITMAPINFO bi{};
        bi.bmiHeader = bih;

        LPVOID dibBits = nullptr;
        HBITMAP hdib = ::CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &dibBits, nullptr, 0);
        ::ReleaseDC(nullptr, hdc);
        if (!hdib || !dibBits) { if (hdib) ::DeleteObject(hdib); return FALSE; }

        // 3) DDB -> DIB. GetDIBits con DIB_RGB_COLORS rellena dibBits con el
        //    formato pedido.
        HDC hdc2 = ::CreateCompatibleDC(nullptr);
        if (!hdc2) { ::DeleteObject(hdib); return FALSE; }
        HGDIOBJ prev = ::SelectObject(hdc2, hbmp);
        int scanned = ::GetDIBits(hdc2, hbmp, 0, (UINT)h, dibBits, &bi, DIB_RGB_COLORS);
        ::SelectObject(hdc2, prev);
        ::DeleteDC(hdc2);
        if (scanned == 0) { ::DeleteObject(hdib); return FALSE; }

        // 4) Empaquetar BITMAPINFOHEADER + pixeles en un buffer contiguo
        //    ANTES de liberar dibBits (DeleteObject lo liberaria).
        LONG rowBytes = ((w * bpp + 31) / 32) * 4;
        SIZE_T imgBytes = (SIZE_T)rowBytes * h;
        SIZE_T total = sizeof(BITMAPINFOHEADER) + imgBytes;
        if (total > 0xFFFFFFFFu) { ::DeleteObject(hdib); return FALSE; }

        LPVOID out = ::HeapAlloc(::GetProcessHeap(), 0, total);
        if (!out) { ::DeleteObject(hdib); return FALSE; }
        memcpy(out, &bih, sizeof(bih));
        memcpy((BYTE*)out + sizeof(bih), dibBits, imgBytes);
        ::DeleteObject(hdib);

        *ppOut = out;
        *pcbOut = (DWORD)total;
        return TRUE;
    }

} // namespace AeroClip::Clipboard