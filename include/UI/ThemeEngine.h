#ifndef AEROCLIP_UI_THEMEENGINE_H
#define AEROCLIP_UI_THEMEENGINE_H

#include "AeroClip.h"
#include <uxtheme.h>

namespace AeroClip::UI {

    struct Palette {
		// flyout background
        COLORREF flyoutBg = RGB(245, 246, 247);
        COLORREF headerBg = RGB(232, 232, 232);
        COLORREF headerText = RGB(32, 32, 32);
        COLORREF divider = RGB(208, 208, 208);

		// Item card
        COLORREF cardBg = RGB(255, 255, 255);
        COLORREF cardBorder = RGB(225, 225, 225);
        COLORREF cardText = RGB(32, 32, 32);
        COLORREF cardTextMuted = RGB(110, 110, 110);

        // Item on hover (Aero/Basic)
        COLORREF cardBgHover = RGB(245, 248, 252);
        COLORREF cardBorderHover = RGB(120, 180, 235);
        COLORREF cardTextHover = RGB(0, 60, 116);

        // Buttom [...]
        COLORREF moreBtnHover = RGB(252, 228, 232);
        COLORREF moreBtnBorder = RGB(232, 17, 35);
        COLORREF moreGlyph = RGB(80, 80, 80);
        COLORREF moreGlyphHover = RGB(176, 0, 16);

        // Close button [X]
        COLORREF closeBtnHover = RGB(232, 17, 35);
        COLORREF closeGlyph = RGB(96, 96, 96);
        COLORREF closeGlyphHover = RGB(255, 255, 255);

        // Scrollbar
        COLORREF scrollTrack = RGB(240, 240, 240);
        COLORREF scrollThumb = RGB(192, 192, 192);
        COLORREF scrollThumbHot = RGB(120, 135, 156);

        // Classic
        COLORREF classicBg = RGB(212, 208, 200);
        COLORREF classicText = RGB(0, 0, 0);
        COLORREF classicHoverBg = RGB(178, 191, 210);
        COLORREF classicHoverTx = RGB(0, 0, 0);
    };

    class ThemeEngine {
    public:
        ThemeEngine();
        ~ThemeEngine();

        void   Refresh();
        Theme  Current() const { return m_theme; }
        BOOL   IsAeroGlass() const { return m_theme == Theme::Aero; }

        const Palette& Colors() const { return m_palette; }

        HFONT  FontRegular() const { return m_fontRegular; }
        HFONT  FontSemibold() const { return m_fontSemibold; }
        HFONT  FontItalic()  const { return m_fontItalic; }
        HFONT  FontUnderline() const { return m_fontUnderline; }

        HTHEME GetButtonTheme() const { return m_themeBtn; }
        // Devuelve handles uxtheme nativos (open por ventana para que uxtheme
        // refresque correctamente al cambiar tema o composicion DWM).
        HTHEME OpenListViewTheme(HWND hwnd = nullptr) const;
        HTHEME OpenScrollbarTheme(HWND hwnd = nullptr) const;

    private:
        Theme   m_theme = Theme::Basic;
        // Internal DWM state cache in DetectTheme()
       
        BOOL    m_aeroGlass = FALSE;
        Palette m_palette;

        HFONT   m_fontRegular = nullptr;
        HFONT   m_fontSemibold = nullptr;
        HFONT   m_fontItalic = nullptr;
        HFONT   m_fontUnderline = nullptr;

        HTHEME  m_themeBtn = nullptr;

        void DetectTheme();
        void BuildPalette();
        void BuildFonts();
        void DestroyFonts();
        void DestroyTheme();
    };

} // namespace AeroClip::UI

#endif // AEROCLIP_UI_THEMEENGINE_H