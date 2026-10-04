#include "UI/ItemRenderer.h"
#include <vssym32.h>
#include <uxtheme.h>
#include <algorithm>
#include <cstring>
#include <objbase.h>
#include <gdiplus.h>

#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "gdiplus.lib")

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

        WCHAR line1[120];
        int n = 0;
        if (text) {
            for (; text[n] && n < 119; ++n) {
                WCHAR ch = text[n];
                if (ch == L'\r' || ch == L'\n') break;
                line1[n] = ch;
            }
        }
        line1[n] = 0;

        HFONT hOldSemibold = nullptr;
        if (te.Current() != Theme::Classic) {
            hOldSemibold = static_cast<HFONT>(::SelectObject(hdc, te.FontSemibold()));
        }

        RECT r1 = tr;
        r1.bottom = (tr.top + tr.bottom) / 2;
        ::DrawTextW(hdc, line1, -1, &r1, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        if (hOldSemibold) {
            ::SelectObject(hdc, hOldSemibold);
        }

        if (text && text[n]) {
            WCHAR line2[160];
            int m = 0;
            for (int i = n; text[i] && m < 159; ++i) {
                WCHAR ch = text[i];
                if (ch == L'\r') continue;
                if (ch == L'\n') break;
                line2[m++] = ch;
            }
            line2[m] = 0;

            HFONT hIt = static_cast<HFONT>(::SelectObject(hdc, te.FontItalic()));
            ::SetTextColor(hdc, p.cardTextMuted);
            RECT r2 = tr;
            r2.top = (tr.top + tr.bottom) / 2;
            ::DrawTextW(hdc, line2, -1, &r2, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            ::SelectObject(hdc, hIt);
        }

        ::SelectObject(hdc, old);
    }

    void ItemRenderer::DrawPinBadge(HDC hdc, const RECT& rc) {
        HBRUSH hb = ::CreateSolidBrush(RGB(255, 192, 0));
        HPEN   hp = ::CreatePen(PS_SOLID, 1, RGB(120, 80, 0));
        HBRUSH ob = static_cast<HBRUSH>(::SelectObject(hdc, hb));
        HPEN   op = static_cast<HPEN>(::SelectObject(hdc, hp));

        int x = rc.right - 24, y = rc.top + 4;
        ::Ellipse(hdc, x, y, x + 10, y + 10);

        ::SelectObject(hdc, op);
        ::SelectObject(hdc, ob);
        ::DeleteObject(hp);
        ::DeleteObject(hb);
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