#include "UI/ItemRenderer.h"
#include <vssym32.h>
#include <uxtheme.h>
#include <algorithm>
#include <cstring>
#include <objbase.h>
#include <gdiplus.h>

#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "gdiplus.lib")

// Estructuras ICONDIR/ICONDIRENTRY del formato .ico. No vienen declaradas en
// <windows.h>, asi que las definimos aqui, alineadas a 1 byte tal y como
// aparecen en el fichero (formato oficial de Microsoft).
#pragma pack(push, 1)
struct IconDirEntry {
    BYTE bWidth;
    BYTE bHeight;
    BYTE bColorCount;
    BYTE bReserved;
    WORD wPlanes;
    WORD wBitsPerPixel;
    DWORD dwBytesInRes;
    WORD wResId;
    WORD wReserved;
};
struct IconDir {
    WORD idReserved;
    WORD idType;
    WORD idCount;
    IconDirEntry idEntries[1]; // se accede como FAM; idCount valida los limites
};
#pragma pack(pop)

namespace AeroClip::UI {

    RECT ItemRenderer::MoreButtonRect(const RECT& itemRect) {
        const int kBtn = 22;
        int x = itemRect.right - kBtn - 8;
        int y = (itemRect.top + itemRect.bottom - kBtn) / 2;
        return { x, y, x + kBtn, y + kBtn };
    }

    void ItemRenderer::DrawCard(HDC hdc, const RECT& rc, const ThemeEngine& te, BOOL hovered) {
        const auto& p = te.Colors();

        if (!te.IsAeroGlass()) {
            HBRUSH hb = ::CreateSolidBrush(hovered ? p.cardBgHover : p.cardBg);
            ::FillRect(hdc, &rc, hb);
            ::DeleteObject(hb);

            HPEN dark = ::CreatePen(PS_SOLID, 1, ::GetSysColor(COLOR_BTNSHADOW));
            HPEN lit = ::CreatePen(PS_SOLID, 1, ::GetSysColor(COLOR_3DHIGHLIGHT));
            if (hovered) std::swap(dark, lit);

            HPEN op = static_cast<HPEN>(::SelectObject(hdc, dark));
            HBRUSH nb = static_cast<HBRUSH>(::SelectObject(hdc, ::GetStockObject(NULL_BRUSH)));
            ::Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
            ::SelectObject(hdc, nb);
            ::SelectObject(hdc, op);
            ::DeleteObject(dark);
            ::DeleteObject(lit);
            return;
        }

        HBRUSH hb = ::CreateSolidBrush(hovered ? p.cardBgHover : p.cardBg);
        ::FillRect(hdc, &rc, hb);
        ::DeleteObject(hb);

        if (hovered && te.GetButtonTheme()) {
            RECT rc2 = rc;
            ::InflateRect(&rc2, 1, 1);
            HTHEME th = te.GetButtonTheme();


            HRGN ring = ::CreateRectRgn(rc.left, rc.top, rc.right, rc.bottom);
            HRGN inn  = ::CreateRectRgn(rc.left + 2, rc.top + 2,
                                         rc.right - 2, rc.bottom - 2);
            ::CombineRgn(ring, ring, inn, RGN_DIFF);
            ::DeleteObject(inn);

            HRGN prevClip = ::CreateRectRgn(0, 0, 1, 1);
            BOOL hadPrevClip = ::GetClipRgn(hdc, prevClip) > 0;
            ::ExtSelectClipRgn(hdc, ring, RGN_AND);
            ::DeleteObject(ring);

            ::DrawThemeBackground(th, hdc, BP_PUSHBUTTON, PBS_HOT, &rc2, &rc);

            // Restore the clip price
            if (hadPrevClip) {
                ::SelectClipRgn(hdc, prevClip);
            } else {
                ::SelectClipRgn(hdc, nullptr);
            }
            ::DeleteObject(prevClip);
        }

        HPEN hp = ::CreatePen(PS_SOLID, 1, hovered ? p.cardBorderHover : p.cardBorder);
        HPEN op = static_cast<HPEN>(::SelectObject(hdc, hp));
        HBRUSH nb = static_cast<HBRUSH>(::SelectObject(hdc, ::GetStockObject(NULL_BRUSH)));
        ::RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
        ::SelectObject(hdc, nb);
        ::SelectObject(hdc, op);
        ::DeleteObject(hp);
    }

    void ItemRenderer::DrawImageThumb(HDC hdc, const RECT& rc, const BYTE* jpeg, DWORD cbJpeg) {
        if (!jpeg || cbJpeg == 0) return;

        // Volcar los bytes JPEG en un IStream y pedirle a GDI+ que los
        // decodifique. Asi nos ahorramos re-parsear el DIB manualmente.
        HGLOBAL hglob = ::GlobalAlloc(GMEM_MOVEABLE, cbJpeg);
        if (!hglob) return;
        void* pMem = ::GlobalLock(hglob);
        if (!pMem) { ::GlobalFree(hglob); return; }
        std::memcpy(pMem, jpeg, cbJpeg);
        ::GlobalUnlock(hglob);

        IStream* stream = nullptr;
        if (FAILED(::CreateStreamOnHGlobal(hglob, TRUE, &stream))) {
            ::GlobalFree(hglob);
            return;
        }
        // CreateStreamOnHGlobal con TRUE libera el HGLOBAL cuando se libera
        // el stream -> no hacemos GlobalFree aqui.

        Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromStream(stream);
        stream->Release();
        if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok) {
            delete bmp;
            return;
        }

        int dstW = rc.right - rc.left;
        int dstH = rc.bottom - rc.top;
        if (dstW <= 0 || dstH <= 0) { delete bmp; return; }

        Gdiplus::Graphics g(hdc);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
        g.DrawImage(bmp, rc.left, rc.top, dstW, dstH);

        delete bmp;
    }

    void ItemRenderer::DrawTextLabel(HDC hdc, const RECT& rc, LPCWSTR text,
        const ThemeEngine& te, BOOL hovered, BOOL hasThumb) {
        const auto& p = te.Colors();
        HFONT hf = (te.Current() == Theme::Classic && hovered)
            ? te.FontUnderline()
            : te.FontRegular();
        HFONT old = static_cast<HFONT>(::SelectObject(hdc, hf));
        ::SetBkMode(hdc, TRANSPARENT);
        ::SetTextColor(hdc, hovered ? p.cardTextHover : p.cardText);

        // Cuando hay miniatura (item de imagen con texto), reservamos
        // kThumbSize + 16 a la izquierda para no chocar con ella.
        // Cuando NO hay miniatura (item de texto puro), el texto usa todo el
        // ancho del item - solo reservamos el espacio del boton "more" a la
        // derecha y un pequeno margen.
        RECT tr = rc;
        if (hasThumb) {
            tr.left += kThumbSize + 16;
        } else {
            tr.left += 10;
        }
        tr.right -= 40;
        tr.top += 4;
        tr.bottom -= 4;

        if (!text || !*text) {
            ::SelectObject(hdc, old);
            return;
        }

        // Wrap manual: DT_WORDBREAK de GDI solo corta en espacios/CRLF, por
        // lo que URLs largas (sin espacios) se truncan con "..." en lugar de
        // envolverse. Medimos cada caracter con GetTextExtentPoint32W y
        // cortamos en el ULTIMO punto de quiebre valido (espacio, /, ?, &,
        // =, -, _) que cabe en maxWidth. Preservamos "://" para que el
        // esquema del URL no se separe. Si no hay punto de quiebre, cortamos
        // al final del ancho disponible. DT_END_ELLIPSIS agrega "..." solo
        // a la ultima linea visible si queda texto por dibujar.
        TEXTMETRIC tm = {};
        ::GetTextMetrics(hdc, &tm);
        int lineHeight = tm.tmHeight > 0 ? tm.tmHeight : 14;
        int maxWidth = tr.right - tr.left;
        if (maxWidth <= 0) {
            ::SelectObject(hdc, old);
            return;
        }
        int maxLines = (tr.bottom - tr.top) / lineHeight;
        if (maxLines < 1) maxLines = 1;

        auto isSpace = [](WCHAR ch) -> bool {
            return ch == L' ' || ch == L'\t';
        };
        auto isBreakable = [isSpace](WCHAR ch, WCHAR prev) -> bool {
            if (isSpace(ch)) return true;
            // No romper dentro de "://" (esquema de URL)
            if (ch == L'/' && prev == L':') return false;
            return ch == L'/' || ch == L'?' || ch == L'&' ||
                   ch == L'=' || ch == L'-' || ch == L'_';
        };

        LPCWSTR cursor = text;
        int lineIdx = 0;

        while (*cursor && lineIdx < maxLines) {
            LPCWSTR lastBreak = nullptr;
            LPCWSTR bestEnd = cursor;
            int currentWidth = 0;
            WCHAR localPrev = (cursor > text) ? *(cursor - 1) : L'\0';

            for (LPCWSTR q = cursor; *q; ++q) {
                SIZE sz = {};
                ::GetTextExtentPoint32W(hdc, q, 1, &sz);
                currentWidth += sz.cx;

                if (currentWidth > maxWidth) {
                    break;
                }
                bestEnd = q + 1;
                if (isBreakable(*q, localPrev)) {
                    lastBreak = q + 1;
                }
                localPrev = *q;
            }

            // Si el texto completo cabe, no romper prematuramente en un
            // espacio intermedio. Solo cortamos si realmente hay overflow.
            LPCWSTR drawEnd = (lastBreak && *(bestEnd) != L'\0') ? lastBreak : bestEnd;

            // Quitar espacios al inicio (por si el cursor quedo en uno).
            LPCWSTR drawStart = cursor;
            while (drawStart < drawEnd && isSpace(*drawStart)) {
                drawStart++;
            }

            int drawLen = (int)(drawEnd - drawStart);
            // Quitar espacios al final.
            while (drawLen > 0 && isSpace(drawStart[drawLen - 1])) {
                drawLen--;
            }

            bool isLastLine = (lineIdx == maxLines - 1) || (*drawEnd == L'\0');
            bool hasMore = (*drawEnd != L'\0');

            RECT lr = { tr.left, tr.top + lineIdx * lineHeight,
                        tr.right, tr.top + (lineIdx + 1) * lineHeight };
            ::DrawTextW(hdc, drawStart, drawLen, &lr,
                DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX |
                (isLastLine && hasMore ? DT_END_ELLIPSIS : 0));

            cursor = drawEnd;
            lineIdx++;
        }

        ::SelectObject(hdc, old);
    }

    void ItemRenderer::DrawPinBadge(HDC hdc, const RECT& rc) {
        // Icono nativo "tachuela" azul de Windows 7, recurso #5100 en
        // imageres.dll (el mismo que aparece en "Desanclar este programa de
        // la barra de tareas"). Se pinta cuando el elemento esta anclado al
        // historial. Carga especificamente la variante 16x16 8bpp del grupo
        // de iconos del recurso.
        HMODULE hRes = ::LoadLibraryExW(L"imageres.dll", nullptr,
                                        LOAD_LIBRARY_AS_DATAFILE);
        if (!hRes) return;

        HICON hIcon = nullptr;

                // Busca la entrada concreta 16x16 8bpp dentro del grupo de iconos.
                HRSRC hGrpInfo = ::FindResourceW(hRes, MAKEINTRESOURCE(5100), RT_GROUP_ICON);
                if (hGrpInfo) {
                    HGLOBAL hGrpData = ::LoadResource(hRes, hGrpInfo);
                    const IconDir* pDir = static_cast<const IconDir*>(::LockResource(hGrpData));
                    if (pDir) {
                        const IconDirEntry* pEntry = nullptr;
                        for (WORD i = 0; i < pDir->idCount && i < 12; ++i) {
                            const IconDirEntry& e = pDir->idEntries[i];
                            if (e.bWidth == 16 && e.bHeight == 16 && e.wBitsPerPixel == 8) {
                                pEntry = &e;
                                break;
                            }
                        }
                        if (pEntry) {
                            HRSRC hIcoInfo = ::FindResourceW(hRes,
                                MAKEINTRESOURCE(pEntry->wResId), RT_ICON);
                            if (hIcoInfo) {
                                DWORD dwSize = ::SizeofResource(hRes, hIcoInfo);
                                HGLOBAL hIcoData = ::LoadResource(hRes, hIcoInfo);
                                void* pIcoData = ::LockResource(hIcoData);
                                if (pIcoData) {
                                    hIcon = ::CreateIconFromResourceEx(
                                        reinterpret_cast<PBYTE>(pIcoData), dwSize,
                                        TRUE, 0x00030000,
                                        16, 16, LR_DEFAULTCOLOR);
                                }
                            }
                        }
                    }
                }

                // Fallback: si por algun motivo la entrada 16x16 8bpp no existe,
                // deja que Windows elija la mejor variante disponible.
                if (!hIcon) {
                    hIcon = static_cast<HICON>(::LoadImageW(
                        hRes, MAKEINTRESOURCE(5100),
                        IMAGE_ICON, 16, 16, 0));
                }

                if (hIcon) {
                    int x = rc.right - 22;
                    int y = rc.top + 2;
                    ::DrawIconEx(hdc, x, y, hIcon, 16, 16, 0, nullptr, DI_NORMAL);
                    ::DestroyIcon(hIcon);
                }

                ::FreeLibrary(hRes);
            }

    void ItemRenderer::DrawMoreButton(HDC hdc, const RECT& rc,
        const ThemeEngine& te, BOOL hovered) {
        const auto& p = te.Colors();

        if (!te.IsAeroGlass()) {
            HBRUSH hb = ::CreateSolidBrush(hovered ? p.cardBgHover : p.cardBg);
            ::FillRect(hdc, &rc, hb);
            ::DeleteObject(hb);

            HPEN dark = ::CreatePen(PS_SOLID, 1, ::GetSysColor(COLOR_BTNSHADOW));
            HPEN op = static_cast<HPEN>(::SelectObject(hdc, dark));
            HBRUSH nb = static_cast<HBRUSH>(::SelectObject(hdc, ::GetStockObject(NULL_BRUSH)));
            ::Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
            ::SelectObject(hdc, nb);
            ::SelectObject(hdc, op);
            ::DeleteObject(dark);
        }
        else {

            HBRUSH hb = ::CreateSolidBrush(hovered ? p.cardBgHover : p.cardBg);
            HPEN   hp = ::CreatePen(PS_SOLID, 1, hovered ? p.cardBorderHover : p.cardBorder);
            HBRUSH ob = static_cast<HBRUSH>(::SelectObject(hdc, hb));
            HPEN   op = static_cast<HPEN>(::SelectObject(hdc, hp));

            ::RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 4, 4);
            ::SelectObject(hdc, op);
            ::SelectObject(hdc, ob);
            ::DeleteObject(hp);
            ::DeleteObject(hb);
        }

        HFONT old = static_cast<HFONT>(::SelectObject(hdc, te.FontSemibold()));
        ::SetBkMode(hdc, TRANSPARENT);
        ::SetTextColor(hdc, hovered ? p.cardTextHover : p.moreGlyph);

        RECT tr = rc;
        tr.top -= 1;
        ::DrawTextW(hdc, L"\u2026", -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        ::SelectObject(hdc, old);
    }

    void ItemRenderer::DrawItem(HDC hdc, const RECT& rc,
        const AeroClip::Storage::RenderItem& cached, BOOL pinned, BOOL hovered,
        const ThemeEngine& te) {
        DrawCard(hdc, rc, te, hovered);

        BOOL hasThumb = !cached.thumbnailJpeg.empty();

        RECT thumbRc = {
            rc.left + 6,
            (rc.top + rc.bottom - kThumbSize) / 2,
            rc.left + 6 + kThumbSize,
            (rc.top + rc.bottom - kThumbSize) / 2 + kThumbSize
        };

        // Solo se dibuja el contenedor de la imagen cuando hay JPEG cacheada;
        // para items de texto ya NO se muestra el placeholder "Abc" - el texto
        // ocupa todo el ancho disponible del item.
        if (hasThumb) {
            DrawImageThumb(hdc, thumbRc, cached.thumbnailJpeg.data(),
                           static_cast<DWORD>(cached.thumbnailJpeg.size()));
        }

        if (!cached.text.empty()) {
            DrawTextLabel(hdc, rc, cached.text.c_str(), te, hovered, hasThumb);
        }

        if (pinned) {
            DrawPinBadge(hdc, rc);
        }
    }

} // namespace AeroClip::UI