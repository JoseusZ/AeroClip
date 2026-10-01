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
        // Flag de "primer Paint tras Create". EnableAeroCaption ya hace
        // Refresh() al final de Create, pero por si el primer WM_PAINT
        // llega antes de ese momento, garantizamos que el tema esta re-
        // evaluado con HWND valido y message loop activo. Solo se evalua
        // una vez por sesion del popup.
        BOOL  m_themeResolvedAtPaint = FALSE;

        // Tamano REAL del area cliente (cacheado en WM_SIZE). NO usamos
        // constantes porque el chrome nativo cambia entre temas y entre
        // maquinas y el cliente difiere.
        int   m_clientW = kFlyoutWidth;
        int   m_clientH = kFlyoutHeight;

        std::function<void(int)> m_actSelect;
        std::function<void(int)> m_actRemove;
        std::function<void(int)> m_actPin;
        std::function<void()>    m_actClearAll;

        // Distribucion del AREA CLIENTE (no del total de ventana).
        // El titulo y los bordes los dibuja Windows/DWM con chrome nativo.
        static constexpr int kTopMargin    = 14;
        static constexpr int kBottomMargin = 14;
        static constexpr int kSidePadding  = 10;
        static constexpr int kItemHeight   = 74;
        static constexpr int kItemGap      = 6;

        // Altura VISUAL real del caption nativo en Win7. SM_CYCAPTION devuelve
        // 0 cuando DWM compone (Aero), porque entonces la caption no es no-
        // cliente sino que DWM la pinta DENTRO del cliente via
        // DwmExtendFrameIntoClientArea. En Clasico, con DWM aun presente o
        // segun metricas del sistema, tambien puede ser menor que la altura
        // visual. En Basic el caption es no-cliente y kTopMargin solo basta.
        static constexpr int kVisualCaptionPx = 28;

        int  ListAreaTop()    const {
            // En Basic el chrome es no-cliente (~22px) y kTopMargin=14 ya
            // deja los items holgados. En Aero el caption lo pinta DWM
            // dentro del cliente (DwmExtendFrameIntoClientArea), asi que
            // reservamos kVisualCaptionPx + kTopMargin para que las tarjetas
            // no se solapen con el cristal.
            //
            // En Classic el caption es no-cliente (lo pinta DefWindowProc en
            // WM_NCPAINT, NO consume area cliente). Reservar altura extra
            // en Classic solo crea un hueco vacio arriba que rompe el look:
            // las tarjetas quedan demasiado abajo y se ve una franja gris
            // innecesaria. Por eso Classic == kTopMargin (sin extras).
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