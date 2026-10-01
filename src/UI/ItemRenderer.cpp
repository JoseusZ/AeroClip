#include "UI/ItemRenderer.h"
#include <vssym32.h>
#include <uxtheme.h>
#include <algorithm>
#include <cstring>

#pragma comment(lib, "uxtheme.lib")

namespace AeroClip::UI {

    RECT ItemRenderer::MoreButtonRect(const RECT& itemRect) {
        const int kBtn = 22;
        int x = itemRect.right - kBtn - 8;
        int y = (itemRect.top + itemRect.bottom - kBtn) / 2;
        return { x, y, x + kBtn, y + kBtn };
    }

    void ItemRenderer::DrawCard(HDC hdc, const RECT& rc, const ThemeEngine& te, BOOL hovered) {
        const auto& p = te.Colors();

        // Regla unica: IsAeroGlass() == TRUE -> pinta Aero (redondeado,
        // paleta Aero, hover con uxtheme BUTTON). Cualquier otro caso
        // (Basic + Classic) -> Rectangle con GetSysColor, respetando
        // personalizacion del usuario.
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

            // Creamos un anillo de 2px alrededor del item para pintar el
            // borde de uxtheme BUTTON en hover. CRITICO: el anillo se
            // COMBINA (RGN_AND) con el clip que ya estuviera activo en el
            // DC (típicamente el ListArea impuesto por PaintItems). Si el
            // item esta al borde inferior del area visible, el anillo se
            // recorta automaticamente al ListArea: el borde inferior del
            // item queda cortado limpiamente en el limite visible, no se
            // pinta fuera del popup.
            HRGN ring = ::CreateRectRgn(rc.left, rc.top, rc.right, rc.bottom);
            HRGN inn  = ::CreateRectRgn(rc.left + 2, rc.top + 2,
                                         rc.right - 2, rc.bottom - 2);
            ::CombineRgn(ring, ring, inn, RGN_DIFF);
            ::DeleteObject(inn);

            // Combinar el ring con el clip actual. ExtSelectClipRgn con
            // RGN_AND interseca ambos. El resultado: solo se pinta donde
            // ambos lo permitan (anillo Y ListArea). El clip original del
            // ListArea queda "encerrado" en el DC; tras pintar, restauramos
            // el clip que habia ANTES para no contaminar el siguiente item.
            HRGN prevClip = ::CreateRectRgn(0, 0, 1, 1);
            BOOL hadPrevClip = ::GetClipRgn(hdc, prevClip) > 0;
            ::ExtSelectClipRgn(hdc, ring, RGN_AND);
            ::DeleteObject(ring);

            ::DrawThemeBackground(th, hdc, BP_PUSHBUTTON, PBS_HOT, &rc2, &rc);

            // Restaurar el clip previo.
            if (hadPrevClip) {
                ::SelectClipRgn(hdc, prevClip);
            } else {
                ::SelectClipRgn(hdc, nullptr);
            }
            ::DeleteObject(prevClip);
        }

        // El clip ahora es el del ListArea (o ninguno si no habia), por
        // lo que el RoundRect del borde del card se recorta correctamente
        // al area visible. Esto arregla el bug: cuando el ultimo item
        // visible esta parcialmente cortado por el scroll, su borde
        // inferior queda recortado limpiamente en el ListArea en lugar de
        // pintarse fuera del popup.
        HPEN hp = ::CreatePen(PS_SOLID, 1, hovered ? p.cardBorderHover : p.cardBorder);
        HPEN op = static_cast<HPEN>(::SelectObject(hdc, hp));
        HBRUSH nb = static_cast<HBRUSH>(::SelectObject(hdc, ::GetStockObject(NULL_BRUSH)));
        ::RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
        ::SelectObject(hdc, nb);
        ::SelectObject(hdc, op);
        ::DeleteObject(hp);
    }

    void ItemRenderer::DrawTextPlaceholder(HDC hdc, const RECT& rc, const ThemeEngine& te) {
        const auto& p = te.Colors();
        HBRUSH hb = ::CreateSolidBrush(RGB(240, 240, 240));
        HPEN   hp = ::CreatePen(PS_SOLID, 1, p.cardBorder);
        HBRUSH ob = static_cast<HBRUSH>(::SelectObject(hdc, hb));
        HPEN   op = static_cast<HPEN>(::SelectObject(hdc, hp));

        ::RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 4, 4);
        ::SelectObject(hdc, op);
        ::SelectObject(hdc, ob);
        ::DeleteObject(hp);
        ::DeleteObject(hb);

        HFONT old = static_cast<HFONT>(::SelectObject(hdc, te.FontRegular()));
        ::SetBkMode(hdc, TRANSPARENT);
        ::SetTextColor(hdc, p.cardTextMuted);
        RECT tr = rc;
        ::DrawTextW(hdc, L"Abc", -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        ::SelectObject(hdc, old);
    }

    static DWORD ComputePixelOffset(const BITMAPINFOHEADER& bih) {
        DWORD off = bih.biSize;
        if (bih.biCompression == BI_BITFIELDS && bih.biBitCount >= 16 && bih.biBitCount <= 32)
            off += 12;
        if (bih.biBitCount <= 8) {
            DWORD colors = bih.biClrUsed ? bih.biClrUsed : (1u << bih.biBitCount);
            off += colors * sizeof(RGBQUAD);
        }
        return off;
    }

    void ItemRenderer::DrawImageThumb(HDC hdc, const RECT& rc, const BYTE* dib, DWORD cbDib) {
        if (!dib || cbDib < sizeof(BITMAPINFOHEADER)) return;
        const auto* bih = reinterpret_cast<const BITMAPINFOHEADER*>(dib);
        if (bih->biWidth <= 0 || bih->biHeight == 0 || bih->biPlanes != 1 || bih->biBitCount == 0)
            return;

        int srcW = abs(bih->biWidth);
        int srcH = abs(bih->biHeight);

        int dstW = 0, dstH = 0;
        if (srcW >= srcH) {
            dstW = kThumbSize;
            dstH = (std::max)(1, static_cast<int>(srcH * (kThumbSize / static_cast<double>(srcW))));
        }
        else {
            dstH = kThumbSize;
            dstW = (std::max)(1, static_cast<int>(srcW * (kThumbSize / static_cast<double>(srcH))));
        }

        int cx = (rc.left + rc.right - dstW) / 2;
        int cy = (rc.top + rc.bottom - dstH) / 2;

        DWORD pixelOff = ComputePixelOffset(*bih);
        if (pixelOff >= cbDib) return;

#pragma pack(push, 1)
        struct {
            BITMAPINFOHEADER bmiHeader;
            RGBQUAD          bmiColors[256];
        } bi = {};
#pragma pack(pop)

        bi.bmiHeader = *bih;
        const BYTE* extra = dib + bih->biSize;

        if (bih->biCompression == BI_BITFIELDS && bih->biBitCount >= 16 && bih->biBitCount <= 32) {
            if (extra && (cbDib >= bih->biSize + 12)) {
                std::memcpy(&bi.bmiColors[0], extra, 12);
            }
        }
        else if (bih->biBitCount <= 8) {
            DWORD colors = bih->biClrUsed ? bih->biClrUsed : (1u << bih->biBitCount);
            if (colors > 256) colors = 256;
            if (extra && (cbDib - pixelOff >= colors * sizeof(RGBQUAD))) {
                std::memcpy(bi.bmiColors, extra, colors * sizeof(RGBQUAD));
            }
        }

        int oldMode = ::SetStretchBltMode(hdc, HALFTONE);
        POINT oldOrg;
        ::SetBrushOrgEx(hdc, 0, 0, &oldOrg);

        ::StretchDIBits(hdc, cx, cy, dstW, dstH, 0, 0, srcW, srcH,
            dib + pixelOff, reinterpret_cast<const BITMAPINFO*>(&bi), DIB_RGB_COLORS, SRCCOPY);

        ::SetBrushOrgEx(hdc, oldOrg.x, oldOrg.y, nullptr);
        ::SetStretchBltMode(hdc, oldMode);
    }

    void ItemRenderer::DrawTextLabel(HDC hdc, const RECT& rc, LPCWSTR text,
        const ThemeEngine& te, BOOL hovered) {
        const auto& p = te.Colors();
        HFONT hf = (te.Current() == Theme::Classic && hovered)
            ? te.FontUnderline()
            : te.FontRegular();
        HFONT old = static_cast<HFONT>(::SelectObject(hdc, hf));
        ::SetBkMode(hdc, TRANSPARENT);
        ::SetTextColor(hdc, hovered ? p.cardTextHover : p.cardText);

        RECT tr = rc;
        tr.left += kThumbSize + 16;
        tr.right -= 40;
        tr.top += 4;
        tr.bottom -= 4;

        WCHAR line1[80];
        int n = 0;
        if (text) {
            for (; text[n] && n < 79; ++n) {
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
            WCHAR line2[100];
            int m = 0;
            for (int i = n; text[i] && m < 99; ++i) {
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
        // Mismo criterio que DrawCard / PaintItems / PaintScrollbar:
        // IsAeroGlass() == TRUE -> pinta Aero (redondeado + paleta);
        // cualquier otro caso -> Rectangle con GetSysColor.
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
            HBRUSH hb = ::CreateSolidBrush(hovered ? p.moreBtnHover : p.cardBg);
            HPEN   hp = ::CreatePen(PS_SOLID, 1, hovered ? p.moreBtnBorder : p.cardBorder);
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
        ::SetTextColor(hdc, hovered ? p.moreGlyphHover : p.moreGlyph);

        RECT tr = rc;
        tr.top -= 1;
        ::DrawTextW(hdc, L"\u2026", -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        ::SelectObject(hdc, old);
    }

    void ItemRenderer::DrawItem(HDC hdc, const RECT& rc,
        const AeroClip::Storage::RenderItem& cached, BOOL pinned, BOOL hovered,
        const ThemeEngine& te) {
        DrawCard(hdc, rc, te, hovered);

        RECT thumbRc = {
            rc.left + 6,
            (rc.top + rc.bottom - kThumbSize) / 2,
            rc.left + 6 + kThumbSize,
            (rc.top + rc.bottom - kThumbSize) / 2 + kThumbSize
        };

        if (!cached.image.empty()) {
            DrawImageThumb(hdc, thumbRc, cached.image.data(), static_cast<DWORD>(cached.image.size()));
        }
        else {
            DrawTextPlaceholder(hdc, thumbRc, te);
        }

        if (!cached.text.empty()) {
            DrawTextLabel(hdc, rc, cached.text.c_str(), te, hovered);
        }

        if (pinned) {
            DrawPinBadge(hdc, rc);
        }
    }

} // namespace AeroClip::UI