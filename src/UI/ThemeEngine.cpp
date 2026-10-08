#include "UI/ThemeEngine.h"
#include <vsstyle.h>
#include <vssym32.h>
#include <dwmapi.h>

#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")

namespace AeroClip::UI {

    ThemeEngine::ThemeEngine() {
        Refresh();
    }

    ThemeEngine::~ThemeEngine() {
        DestroyFonts();
        DestroyTheme();
    }

    void ThemeEngine::Refresh() {
        DestroyFonts();
        DestroyTheme();
        DetectTheme();
        BuildPalette();
        BuildFonts();

        if (m_theme != Theme::Classic) {
            m_themeBtn = ::OpenThemeData(nullptr, L"BUTTON");
        }
    }

    void ThemeEngine::DestroyTheme() {
        if (m_themeBtn) {
            ::CloseThemeData(m_themeBtn);
            m_themeBtn = nullptr;
        }
    }

    void ThemeEngine::DestroyFonts() {
        auto Del = [](HFONT& f) {
            if (f) { ::DeleteObject(f); f = nullptr; }
            };
        Del(m_fontRegular);
        Del(m_fontSemibold);
        Del(m_fontItalic);
        Del(m_fontUnderline);
    }

    void ThemeEngine::DetectTheme() {
        m_aeroGlass = FALSE;
        m_theme = Theme::Classic;

        if (::IsAppThemed() && ::IsThemeActive()) {
            m_theme = Theme::Basic;
            BOOL comp = FALSE;
            if (SUCCEEDED(::DwmIsCompositionEnabled(&comp)) && comp) {
                m_theme = Theme::Aero;
                m_aeroGlass = TRUE;
            }
        }
    }
    // Empanadas 
    void ThemeEngine::BuildPalette() {
        if (m_theme == Theme::Classic) {
            m_palette.flyoutBg = ::GetSysColor(COLOR_3DFACE);
            m_palette.headerBg = ::GetSysColor(COLOR_3DFACE);
            m_palette.headerText = ::GetSysColor(COLOR_WINDOWTEXT);
            m_palette.divider = ::GetSysColor(COLOR_BTNSHADOW);
            m_palette.cardBg = ::GetSysColor(COLOR_WINDOW);
            m_palette.cardBorder = ::GetSysColor(COLOR_BTNSHADOW);
            m_palette.cardText = ::GetSysColor(COLOR_WINDOWTEXT);
            m_palette.cardTextMuted = ::GetSysColor(COLOR_WINDOWTEXT);
            m_palette.cardBgHover = ::GetSysColor(COLOR_HIGHLIGHT);
            m_palette.cardBorderHover = ::GetSysColor(COLOR_HIGHLIGHT);
            m_palette.cardTextHover = ::GetSysColor(COLOR_HIGHLIGHTTEXT);
            m_palette.moreBtnHover = ::GetSysColor(COLOR_3DFACE);
            m_palette.moreBtnBorder = ::GetSysColor(COLOR_BTNSHADOW);
            m_palette.moreGlyph = ::GetSysColor(COLOR_WINDOWTEXT);
            m_palette.moreGlyphHover = ::GetSysColor(COLOR_HIGHLIGHTTEXT);
            m_palette.closeBtnHover = ::GetSysColor(COLOR_HIGHLIGHT);
            m_palette.closeGlyph = ::GetSysColor(COLOR_WINDOWTEXT);
            m_palette.closeGlyphHover = ::GetSysColor(COLOR_HIGHLIGHTTEXT);
            m_palette.scrollTrack = ::GetSysColor(COLOR_3DFACE);
            m_palette.scrollThumb = ::GetSysColor(COLOR_BTNSHADOW);
            m_palette.scrollThumbHot = ::GetSysColor(COLOR_HIGHLIGHT);
            m_palette.classicBg = ::GetSysColor(COLOR_3DFACE);
            m_palette.classicText = ::GetSysColor(COLOR_WINDOWTEXT);
            m_palette.classicHoverBg = ::GetSysColor(COLOR_HIGHLIGHT);
            m_palette.classicHoverTx = ::GetSysColor(COLOR_HIGHLIGHTTEXT);
        }
        else {
            m_palette = Palette();
        }
    }

    static HFONT MakeFont(LONG weight, BOOL italic, BOOL underline, LONG heightPx) {
        NONCLIENTMETRICSW ncm = { sizeof(ncm) };
        if (!::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
            return static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
        }
        ncm.lfMessageFont.lfWeight = weight;
        ncm.lfMessageFont.lfItalic = static_cast<BYTE>(italic ? 1 : 0);
        ncm.lfMessageFont.lfUnderline = static_cast<BYTE>(underline ? 1 : 0);
        ncm.lfMessageFont.lfHeight = heightPx;
        ncm.lfMessageFont.lfQuality = CLEARTYPE_QUALITY;
        ncm.lfMessageFont.lfCharSet = DEFAULT_CHARSET;
        return ::CreateFontIndirectW(&ncm.lfMessageFont);
    }

    void ThemeEngine::BuildFonts() {
        HDC screen = ::GetDC(nullptr);
        int logPixY = ::GetDeviceCaps(screen, LOGPIXELSY);
        ::ReleaseDC(nullptr, screen);

        LONG pt9  = -::MulDiv(9,  logPixY, 72) + 1;  // -1 px: 12 -> 11 px @ 96 DPI
        LONG pt12 = -::MulDiv(12, logPixY, 72) + 1;  // -1 px: 16 -> 15 px @ 96 DPI

        m_fontRegular = MakeFont(FW_NORMAL, FALSE, FALSE, pt9);
        m_fontSemibold = MakeFont(FW_SEMIBOLD, FALSE, FALSE, pt9);
        m_fontItalic = MakeFont(FW_NORMAL, TRUE, FALSE, pt12);
        m_fontUnderline = MakeFont(FW_NORMAL, FALSE, TRUE, pt9);
    }

HTHEME ThemeEngine::OpenListViewTheme(HWND hwnd) const {
        if (m_theme == Theme::Classic) return nullptr;
        // If OpenThemeData(NULL) fails on Win7, passing hwnd is valid and avoids
        // cases where the application lacks a registered context.
        return ::OpenThemeData(hwnd ? hwnd : nullptr, L"Explorer::ListView");
    }

    HTHEME ThemeEngine::OpenScrollbarTheme(HWND hwnd) const {
        if (m_theme == Theme::Classic) return nullptr;
        return ::OpenThemeData(hwnd ? hwnd : nullptr, L"Scrollbar");
    }

} // namespace AeroClip::UI