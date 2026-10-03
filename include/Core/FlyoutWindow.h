#ifndef AEROCLIP_CORE_FLYOUTWINDOW_H
#define AEROCLIP_CORE_FLYOUTWINDOW_H

#include "AeroClip.h"
#include "Storage/HistoryBuffer.h"
#include "UI/ThemeEngine.h"
#include <windows.h>
#include <functional>

namespace AeroClip::Core {

    class FlyoutWindow {
    public:
        FlyoutWindow();
        ~FlyoutWindow();

        BOOL Create(HINSTANCE hInst, Storage::HistoryBuffer& buffer, UI::ThemeEngine& te);
        void Toggle();
        void Show();
        void Hide();

        HWND GetHwnd() const { return m_hwnd; }
        void OnClipboardUpdate();

        void SetContextActions(
            std::function<void(int idx)> onRemove,
            std::function<void(int idx)> onPin,
            std::function<void()>        onClearAll);

        void SetOnSelect(std::function<void(int idx)> onSelect);

                // Repinta la flyout tras un cambio externo al buffer (Remove, pin,
                // clear all). Equivalente a un MSG_CLIPBOARD_UPDATED pero disparado
                // por el llamador.
                void Refresh();

        RECT GetWindowRect() const;

    private:
        Storage::HistoryBuffer* m_buffer = nullptr;
        UI::ThemeEngine*        m_te     = nullptr;
        HWND                    m_hwnd   = nullptr;
        HINSTANCE               m_inst   = nullptr;

        int   m_hoverItem     = -1;
        int   m_moreHover     = -1;
        int   m_scrollY       = 0;
        BOOL  m_trackingMouse = FALSE;
        BOOL  m_aeroExtended  = FALSE;

        static constexpr UINT_PTR kThemeCoalesceTimerId = 0xAC17;
        static constexpr int     kThemeCoalesceMs      = 50;

        int   m_clientW = kFlyoutWidth;
        int   m_clientH = kFlyoutHeight;

        std::function<void(int)> m_actSelect;
        std::function<void(int)> m_actRemove;
        std::function<void(int)> m_actPin;
        std::function<void()>    m_actClearAll;

		//The ttile and borders are drawn by Windows/DWM with native chrome
        static constexpr int kTopMargin    = 14;
        static constexpr int kBottomMargin = 14;
        static constexpr int kSidePadding  = 10;
        static constexpr int kItemHeight   = 74;
        static constexpr int kItemGap      = 6;

        //Actual visual height of the native caption on Windows 7: SM_CYCAPTION returns 0 when DWM composition (Aero) is active.
        static constexpr int kVisualCaptionPx = 28;

        int  ListAreaTop()    const {
            // In Basic, the chrome is non-client (~22px), and kTopMargin=14 already leaves the items with plenty of room.
            if (m_te && m_te->Current() == AeroClip::Theme::Aero)
                return kVisualCaptionPx + kTopMargin;
            return kTopMargin;
        }
        int  ListAreaBottom() const { return m_clientH - kBottomMargin; }
        int  ListAreaHeight() const { return ListAreaBottom() - ListAreaTop(); }
        int  ContentHeight(int n) const {
            return n * kItemHeight + (n > 0 ? (n - 1) * kItemGap : 0);
        }
        int  MaxScrollY(int n) const {
            int m = ContentHeight(n) - ListAreaHeight();
            return m < 0 ? 0 : m;
        }

        RECT ItemRect(int index) const;
        RECT MoreButtonRect(int index) const;
        int  HitTestItem(int x, int y) const;

        void UpdateHover(int x, int y);
        void RecalcScrollBounds();

        void Paint(HDC hdc, const RECT& rcClient);
        void PaintItems(HDC hdc, const RECT& rcClient);
        void PaintScrollbar(HDC hdc, int n, const RECT& rcClient);
        void PaintEmptyState(HDC hdc, const RECT& rcClient);

        void EnableAeroCaption();
        void DisableAeroCaption();
        int  CaptionHeight() const;

        // Recalcula el tamano TOTAL para que el area cliente vuelva a ser
        // exactamente kFlyoutWidth x kFlyoutHeight (lo necesita al cambiar
        // de tema porque el chrome nativo cambia de altura).
        void ReflowForTheme();

        static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    };

} // namespace AeroClip::Core

#endif // AEROCLIP_CORE_FLYOUTWINDOW_H