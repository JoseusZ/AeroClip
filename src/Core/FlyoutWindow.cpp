#include "Core/FlyoutWindow.h"
#include "UI/ItemRenderer.h"
#include <dwmapi.h>
#include <uxtheme.h>
#include <vsstyle.h>
#include <algorithm>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

namespace AeroClip::Core {

    // Altura aprox. de la caption nativa (SM_CYSMCAPTION) — la que DWM extiende.
    static int GetSystemCaptionHeight() {
        DWORD cx = ::GetSystemMetrics(SM_CYCAPTION);
        if (cx == 0) cx = 26; // fallback razonable para Win7
        return static_cast<int>(cx);
    }

    FlyoutWindow::FlyoutWindow() = default;

    FlyoutWindow::~FlyoutWindow() {
        if (m_hwnd) ::DestroyWindow(m_hwnd);
    }

    BOOL FlyoutWindow::Create(HINSTANCE hInst, Storage::HistoryBuffer& buffer, UI::ThemeEngine& te) {
        m_inst = hInst;
        m_buffer = &buffer;
        m_te = &te;

        static const wchar_t* kClass = L"AeroClip_Flyout";
        WNDCLASSEX wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_DROPSHADOW;
        wc.lpfnWndProc = &FlyoutWindow::WndProc;
        wc.hInstance = hInst;
        wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = kClass;
        wc.hIconSm = wc.hIcon = nullptr;

        if (!::RegisterClassEx(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return FALSE;

        POINT pt;
        ::GetCursorPos(&pt);
        HMONITOR mon = ::MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        ::GetMonitorInfo(mon, &mi);

        // Calculamos el rectángulo total deseado para que el área cliente sea
        // exactamente kFlyoutWidth x kFlyoutHeight.
        //
        // Sin WS_THICKFRAME: en Win7 Classic el borde redimensionable es un
        // 3D elevado grueso que se mete en la esquina superior izquierda y
        // tapa parte del texto "Historial" del caption nativo. Usando solo
        // WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU obtenemos un caption
        // nativo limpio (X de cierre + titulo visibles) con borde fino de
        // 1 px, que en Classic y Aero se ve como chrome nativo del sistema.
        // El popup no es redimensionable por el usuario; el contenido tiene
        // scroll interno que ya gestiona el historial.
        DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
        // Quitamos maximize/minimize, dejamos solo close.
        style &= ~(WS_MINIMIZEBOX | WS_MAXIMIZEBOX);

        RECT wanted = { 0, 0, kFlyoutWidth, kFlyoutHeight };
        ::AdjustWindowRectEx(&wanted, style, FALSE, WS_EX_TOOLWINDOW | WS_EX_TOPMOST);
        int totalW = wanted.right  - wanted.left;
        int totalH = wanted.bottom - wanted.top;

        int x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - totalW) / 2;
        int y = mi.rcWork.top  + 80;

        m_hwnd = ::CreateWindowEx(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            kClass, L"Historial",
            style,
            x, y, totalW, totalH,
            nullptr, nullptr, hInst, this);

        if (!m_hwnd) return FALSE;

        ::SetWindowLongPtr(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        EnableAeroCaption();
        return TRUE;
    }

    int FlyoutWindow::CaptionHeight() const {
        return GetSystemCaptionHeight();
    }

    void FlyoutWindow::ReflowForTheme() {
        if (!m_hwnd) return;
        // Recalcula el tamano total de ventana para que el area cliente
        // siga siendo exactamente kFlyoutWidth x kFlyoutHeight aunque el
        // chrome nativo cambie (Aero glass vs Classic Classic). Sin esto,
        // al togglear tema el contenido queda desfasado.
        DWORD style = static_cast<DWORD>(::GetWindowLongPtr(m_hwnd, GWL_STYLE));
        DWORD ex    = static_cast<DWORD>(::GetWindowLongPtr(m_hwnd, GWL_EXSTYLE));
        RECT wanted = { 0, 0, kFlyoutWidth, kFlyoutHeight };
        ::AdjustWindowRectEx(&wanted, style, FALSE, ex);
        int totalW = wanted.right  - wanted.left;
        int totalH = wanted.bottom - wanted.top;
        ::SetWindowPos(m_hwnd, nullptr, 0, 0, totalW, totalH,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        // Tras el reflow, GetClientRect reentrara por WM_SIZE y actualizara
        // m_clientW/H.
    }

    void FlyoutWindow::EnableAeroCaption() {
        // SIEMPRE empezamos con FALSE. Solo se pone TRUE si la extension
        // tiene exito. Asi m_aeroExtended nunca refleja un valor obsoleto.
        m_aeroExtended = FALSE;
        if (!m_te || !m_hwnd) return;

        // Re-evaluar el tema AQUI (con HWND valido y message loop activo).
        // Esto soluciona el caso donde el constructor global de ThemeEngine
        // (global estatica en main.cpp) se ejecuta antes de que el sistema
        // tenga su contexto de visual styles / DWM listo, lo que dejaba
        // m_theme mal cacheado (tipicamente Classic en maquinas Aero). No-
    // Clipboard / no-portapapeles: solo UI.
        m_te->Refresh();

        BOOL comp = FALSE;
        if (FAILED(::DwmIsCompositionEnabled(&comp)) || !comp) return;
        if (!m_te->IsAeroGlass()) return;

        // Extendemos el cristal SOLO a la caption nativa. El resto del
        // area cliente lo pintamos nosotros sobre fondo solido.
        MARGINS m = { 0, 0, CaptionHeight(), 0 };
        if (SUCCEEDED(::DwmExtendFrameIntoClientArea(m_hwnd, &m))) {
            m_aeroExtended = TRUE;
        }
    }

    void FlyoutWindow::DisableAeroCaption() {
        if (!m_aeroExtended) return;
        MARGINS m = { 0, 0, 0, 0 };
        ::DwmExtendFrameIntoClientArea(m_hwnd, &m);
        m_aeroExtended = FALSE;
    }

    RECT FlyoutWindow::ItemRect(int index) const {
        int y = ListAreaTop() + index * (kItemHeight + kItemGap) - m_scrollY;
        return { kSidePadding, y, m_clientW - kSidePadding, y + kItemHeight };
    }

    RECT FlyoutWindow::MoreButtonRect(int index) const {
        return UI::ItemRenderer::MoreButtonRect(ItemRect(index));
    }

    int FlyoutWindow::HitTestItem(int x, int y) const {
        int n = m_buffer ? m_buffer->Count() : 0;
        if (n == 0) return -1;
        int top = ListAreaTop();
        int bot = ListAreaBottom();
        if (y < top || y >= bot) return -1;

        int rel = (y - top + m_scrollY);
        int idx = rel / (kItemHeight + kItemGap);
        if (idx < 0 || idx >= n) return -1;

        int relInside = rel - idx * (kItemHeight + kItemGap);
        if (relInside >= kItemHeight) return -1;
        if (x < kSidePadding || x > m_clientW - kSidePadding) return -1;

        return idx;
    }

    void FlyoutWindow::UpdateHover(int x, int y) {
        int oldItem = m_hoverItem;
        int oldMore = m_moreHover;

        m_hoverItem = -1;
        m_moreHover = -1;

        int item = HitTestItem(x, y);
        if (item >= 0) {
            m_hoverItem = item;
            RECT rcMore = MoreButtonRect(item);
            if (x >= rcMore.left && x < rcMore.right && y >= rcMore.top && y < rcMore.bottom) {
                m_moreHover = item;
            }
        }

        if (oldItem != m_hoverItem || oldMore != m_moreHover) {
            ::InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }

    void FlyoutWindow::RecalcScrollBounds() {
        int n = m_buffer ? m_buffer->Count() : 0;
        int max = MaxScrollY(n);
        if (m_scrollY > max) m_scrollY = max;
        if (m_scrollY < 0)   m_scrollY = 0;
    }

    void FlyoutWindow::Toggle() {
        if (!m_hwnd) return;
        if (::IsWindowVisible(m_hwnd)) Hide();
        else                           Show();
    }

    void FlyoutWindow::Show() {
        if (!m_hwnd) return;
        m_scrollY = 0;
        RecalcScrollBounds();
        ::ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        ::SetForegroundWindow(m_hwnd);
        ::InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    void FlyoutWindow::Hide() {
        if (!m_hwnd) return;
        ::ShowWindow(m_hwnd, SW_HIDE);
    }

    void FlyoutWindow::OnClipboardUpdate() {
        if (m_hwnd && ::IsWindowVisible(m_hwnd)) {
            ::InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }

    RECT FlyoutWindow::GetWindowRect() const {
        RECT r{ 0, 0, 0, 0 };
        if (m_hwnd) ::GetWindowRect(m_hwnd, &r);
        return r;
    }

    void FlyoutWindow::SetContextActions(
        std::function<void(int)> onRemove,
        std::function<void(int)> onPin,
        std::function<void()>    onClearAll) {
        m_actRemove = std::move(onRemove);
        m_actPin = std::move(onPin);
        m_actClearAll = std::move(onClearAll);
    }

    void FlyoutWindow::SetOnSelect(std::function<void(int)> onSelect) {
        m_actSelect = std::move(onSelect);
    }

    void FlyoutWindow::PaintEmptyState(HDC hdc, const RECT& rcClient) {
        HFONT old = (HFONT)::SelectObject(hdc, m_te->FontItalic());
        const auto& p = m_te->Colors();
        ::SetBkMode(hdc, TRANSPARENT);
        ::SetTextColor(hdc, p.cardTextMuted);
        RECT rc{ kSidePadding, ListAreaTop(), m_clientW - kSidePadding, ListAreaBottom() };
        ::DrawTextW(hdc, L"Tu historial esta vacio.\nCopia algo para empezar.",
            -1, &rc, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        ::SelectObject(hdc, old);
    }

    static void BuildRenderItemForPaint(
            const AeroClip::HistoryItem& hi,
            Storage::RenderItem& out) {
        out.text.clear();
        out.image.clear();

        if (!hi.pData || !hi.cbData) return;
        DWORD cb = 0;
        LPVOID data = Storage::HistoryBuffer::DecryptCopy(hi, &cb);
        if (!data || cb == 0) return;

        if (cb >= sizeof(BITMAPINFOHEADER)) {
            const auto* bih = reinterpret_cast<const BITMAPINFOHEADER*>(data);
            if (bih->biSize == sizeof(BITMAPINFOHEADER) && bih->biWidth > 0 && bih->biHeight != 0) {
                out.image.assign(static_cast<const BYTE*>(data), static_cast<const BYTE*>(data) + cb);
            }
            else {
                out.text.assign(static_cast<const wchar_t*>(data), cb / sizeof(wchar_t));
            }
        }
        else {
            out.text.assign(static_cast<const wchar_t*>(data), cb / sizeof(wchar_t));
        }
        ::SecureZeroMemory(data, cb);
        ::HeapFree(::GetProcessHeap(), 0, data);
    }

    void FlyoutWindow::PaintItems(HDC hdc, const RECT& rcClient) {
        int n = m_buffer ? m_buffer->Count() : 0;
        int top = ListAreaTop();
        int bot = ListAreaBottom();
        ::SetBkMode(hdc, TRANSPARENT);
        if (n == 0) { PaintEmptyState(hdc, rcClient); return; }

        // Clip al area de lista para no pintar fuera.
        HRGN rgn = ::CreateRectRgn(kSidePadding, top,
                                    m_clientW - kSidePadding, bot);
        ::SelectClipRgn(hdc, rgn);
        ::DeleteObject(rgn);

        for (int i = 0; i < n; ++i) {
            RECT rc = ItemRect(i);
            if (rc.bottom <= top || rc.top >= bot) continue;
            BOOL hovered = (m_hoverItem == i) || (m_moreHover == i);
            BOOL moreH = (m_moreHover == i);

            const auto item = m_buffer->Get(i);   // copia por valor
            BOOL pinned = item.pinned;

            // Background nativo segun tema:
            HTHEME hThemeList = nullptr;
            int partId = 0, stateId = 0;
            BOOL useUxTheme = FALSE;
            if (m_te && m_te->IsAeroGlass()) {
                hThemeList = m_te->OpenListViewTheme();
                if (hThemeList) {
                    useUxTheme = TRUE;
                    partId = LVP_LISTITEM;
                    if (hovered) stateId = LISS_HOT;
                    else if (i == 0 && !pinned) stateId = LISS_SELECTED;
                    else stateId = LISS_NORMAL;
                }
            }
            if (useUxTheme) {
                // Margen que uxtheme necesita para pintar el degradado/border.
                RECT rcDraw = rc;
                ::DrawThemeBackground(hThemeList, hdc, partId, stateId, &rcDraw, nullptr);
            }
            else {
                // Clasico / Basic: GetSysColor mantiene la personalizacion del usuario.
                const auto& p = m_te->Colors();
                COLORREF fill = hovered ? p.cardBgHover : p.cardBg;
                COLORREF line = p.cardBorder;
                HBRUSH hb = ::CreateSolidBrush(fill);
                HPEN hp = ::CreatePen(PS_SOLID, 1, line);
                HBRUSH ob = (HBRUSH)::SelectObject(hdc, hb);
                HPEN   op = (HPEN)::SelectObject(hdc, hp);
                ::RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
                ::SelectObject(hdc, op);
                ::SelectObject(hdc, ob);
                ::DeleteObject(hp);
                ::DeleteObject(hb);
            }

            // Contenido real (texto/imagen) usando las APIs nativas
            // (clases uxtheme, fuentes Segoe UI del sistema).
            Storage::RenderItem ri;
            BuildRenderItemForPaint(item, ri);
            UI::ItemRenderer::DrawItem(hdc, rc, ri, pinned, hovered, *m_te);

            if (hovered) {
                UI::ItemRenderer::DrawMoreButton(hdc, MoreButtonRect(i), *m_te, moreH);
            }

            // RESTAURAR el clip del ListArea tras cada item. ItemRenderer::
            // DrawCard hace SelectClipRgn(ring) cuando hay hover para
            // pintar el borde de uxtheme BUTTON; ese ring REEMPLAZA (no
            // combina) el clip actual. Si el item esta al borde inferior y
            // su rc se extiende fuera del ListArea, el borde de hover se
            // pintaria fuera del area visible. Reaplicar el clip aqui
            // garantiza que cualquier pintado posterior respete el limite.
            HRGN reapply = ::CreateRectRgn(kSidePadding, top,
                                            m_clientW - kSidePadding, bot);
            ::SelectClipRgn(hdc, reapply);
            ::DeleteObject(reapply);
        }

        ::SelectClipRgn(hdc, nullptr);
    }

    void FlyoutWindow::PaintScrollbar(HDC hdc, int n, const RECT& rcClient) {
        if (n == 0) return;
        int contentH = ContentHeight(n);
        int viewH = ListAreaHeight();
        if (contentH <= viewH) return;

        const auto& p = m_te->Colors();
        int sbX = m_clientW - 6;
        int sbTop = ListAreaTop();
        int sbBot = ListAreaBottom();
        int sbH = sbBot - sbTop;
        int maxScroll = std::max(1, contentH - viewH);
        int thumbH = std::max(20, (int)(sbH * (double)viewH / contentH));
        int thumbY = sbTop + (int)((sbH - thumbH) * (double)m_scrollY / maxScroll);

        // Pista y pulgar como tema nativo de scrollbar si hay tema disponible.
        if (m_te && m_te->IsAeroGlass()) {
            if (HTHEME hScr = m_te->OpenScrollbarTheme()) {
                RECT trackRc{ sbX, sbTop, sbX + 4, sbBot };
                RECT thumbRc{ sbX, thumbY, sbX + 4, thumbY + thumbH };
                ::DrawThemeBackground(hScr, hdc, SBP_UPPERTRACKVERT, SCRBS_NORMAL, &trackRc, nullptr);
                int thumbState = (m_hoverItem >= 0) ? SCRBS_HOT : SCRBS_NORMAL;
                ::DrawThemeBackground(hScr, hdc, SBP_THUMBBTNVERT, thumbState, &thumbRc, nullptr);
            }
            return;
        }

        // Clasico / Basic: pintar con GetSysColor respetando personalizacion.
        RECT track{ sbX, sbTop, sbX + 4, sbBot };
        HBRUSH hb = ::CreateSolidBrush(::GetSysColor(COLOR_SCROLLBAR));
        ::FillRect(hdc, &track, hb);
        ::DeleteObject(hb);

        RECT thumb{ sbX, thumbY, sbX + 4, thumbY + thumbH };
        HBRUSH ht = ::CreateSolidBrush(::GetSysColor(COLOR_3DSHADOW));
        ::FillRect(hdc, &thumb, ht);
        ::DeleteObject(ht);
    }

    void FlyoutWindow::Paint(HDC hdc, const RECT& rcClient) {
        const auto& p = m_te->Colors();

        // En Aero dejamos SIN pintar la franja superior donde DWM extiende
        // el cristal de la caption nativa. Si pintamos ahi con FillRect,
        // aparece una franja blanca opaca que tapa el glass (bug visible).
        // En Classic/Basic pintamos toda el area (el caption nativo se
        // pinta fuera del cliente por DefWindowProc, no hay cristal).
        if (m_aeroExtended) {
            RECT rcBelow = rcClient;
            rcBelow.top = CaptionHeight();
            HBRUSH hb = ::CreateSolidBrush(p.flyoutBg);
            ::FillRect(hdc, &rcBelow, hb);
            ::DeleteObject(hb);
        }
        else {
            HBRUSH hb = ::CreateSolidBrush(p.flyoutBg);
            ::FillRect(hdc, &rcClient, hb);
            ::DeleteObject(hb);
        }

        PaintItems(hdc, rcClient);
        PaintScrollbar(hdc, m_buffer ? m_buffer->Count() : 0, rcClient);
    }

    LRESULT CALLBACK FlyoutWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        FlyoutWindow* self = reinterpret_cast<FlyoutWindow*>(::GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (!self && msg == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCT*>(lp);
            self = reinterpret_cast<FlyoutWindow*>(cs->lpCreateParams);
            if (self) self->m_hwnd = hwnd;
            ::SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return ::DefWindowProc(hwnd, msg, wp, lp);

        switch (msg) {
        case WM_ERASEBKGND:
            // Devolvemos 1 para evitar que GDI pinte fondo encima de DWM/Aero.
            return 1;

        case WM_SIZE: {
            // Cacheamos el tamano REAL del area cliente. Despues de un
            // WM_THEMECHANGED/WM_DWMCOMPOSITIONCHANGED con ReflowForTheme()
            // llegara aqui y refrescaremos m_clientW/H.
            self->m_clientW = LOWORD(lp);
            self->m_clientH = HIWORD(lp);
            return 0;
        }

        case WM_NCHITTEST: {
            // Permitimos redimensionar con el native chrome.
            return ::DefWindowProc(hwnd, msg, wp, lp);
        }

        case WM_MOUSEMOVE: {
            int x = LOWORD(lp), y = HIWORD(lp);
            if (!self->m_trackingMouse) {
                TRACKMOUSEEVENT tme = { sizeof(tme) };
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                ::TrackMouseEvent(&tme);
                self->m_trackingMouse = TRUE;
            }
            self->UpdateHover(x, y);
            return 0;
        }

        case WM_MOUSELEAVE: {
            self->m_trackingMouse = FALSE;
            BOOL changed = (self->m_hoverItem != -1) || (self->m_moreHover != -1);
            self->m_hoverItem = -1;
            self->m_moreHover = -1;
            if (changed) ::InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int x = LOWORD(lp), y = HIWORD(lp);
            int item = self->HitTestItem(x, y);
            if (item >= 0) {
                RECT rcMore = self->MoreButtonRect(item);
                if (x >= rcMore.left && x < rcMore.right && y >= rcMore.top && y < rcMore.bottom) {
                    HMENU hMenu = ::CreatePopupMenu();
                    ::AppendMenu(hMenu, MF_STRING, 1, L"Eliminar");
                    ::AppendMenu(hMenu, MF_STRING, 2, L"Anclar");
                    ::AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
                    ::AppendMenu(hMenu, MF_STRING, 3, L"Borrar todo");

                    if (self->m_buffer && self->m_buffer->Get(item).pinned)
                        ::CheckMenuItem(hMenu, 2, MF_BYCOMMAND | MF_CHECKED);

                    POINT pt{ x, y };
                    ::ClientToScreen(hwnd, &pt);
                    int cmd = ::TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
                    ::DestroyMenu(hMenu);

                    if (self->m_actRemove && cmd == 1) self->m_actRemove(item);
                    else if (self->m_actPin && cmd == 2) self->m_actPin(item);
                    else if (self->m_actClearAll && cmd == 3) self->m_actClearAll();
                }
                else {
                    self->Hide();
                    if (self->m_actSelect) self->m_actSelect(item);
                }
            }
            return 0;
        }

        case WM_RBUTTONUP: {
            int x = LOWORD(lp), y = HIWORD(lp);
            int item = self->HitTestItem(x, y);
            if (item >= 0) {
                HMENU hMenu = ::CreatePopupMenu();
                ::AppendMenu(hMenu, MF_STRING, 1, L"Eliminar");
                ::AppendMenu(hMenu, MF_STRING, 2, L"Anclar");
                ::AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
                ::AppendMenu(hMenu, MF_STRING, 3, L"Borrar todo");

                if (self->m_buffer && self->m_buffer->Get(item).pinned)
                    ::CheckMenuItem(hMenu, 2, MF_BYCOMMAND | MF_CHECKED);

                POINT pt{ x, y };
                ::ClientToScreen(hwnd, &pt);
                int cmd = ::TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
                ::DestroyMenu(hMenu);

                if (cmd == 1 && self->m_actRemove) self->m_actRemove(item);
                else if (cmd == 2 && self->m_actPin) self->m_actPin(item);
                else if (cmd == 3 && self->m_actClearAll) self->m_actClearAll();
            }
            return 0;
        }

        case WM_MOUSEWHEEL: {
            int n = self->m_buffer ? self->m_buffer->Count() : 0;
            int maxScroll = self->MaxScrollY(n);
            if (maxScroll <= 0) return 0;
            int delta = (int)(short)HIWORD(wp);
            int step = (self->kItemHeight + self->kItemGap) / 2;
            self->m_scrollY -= delta / WHEEL_DELTA * step;
            if (self->m_scrollY < 0) self->m_scrollY = 0;
            if (self->m_scrollY > maxScroll) self->m_scrollY = maxScroll;
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = ::BeginPaint(hwnd, &ps);

            RECT rc;
            ::GetClientRect(hwnd, &rc);

            // Re-evaluar el tema UNA SOLA VEZ en el primer Paint tras Create.
            // Esto cubre el caso en que el constructor global de ThemeEngine
            // (global estatica en main.cpp) se ejecuta antes de que DWM /
            // visual styles estuvieran listos para este proceso, lo que
            // dejaba m_theme mal cacheado (Classic en maquinas Aero). NO
            // toca clipboard ni hotkey: solo UI.
            if (!self->m_themeResolvedAtPaint && self->m_te) {
                self->m_te->Refresh();
                self->m_themeResolvedAtPaint = TRUE;
            }

            // Double buffer para eliminar flickering.
            HDC mem = ::CreateCompatibleDC(hdc);
            HBITMAP bmp = ::CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            HGDIOBJ oldBmp = ::SelectObject(mem, bmp);
            self->Paint(mem, rc);
            ::BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            ::SelectObject(mem, oldBmp);
            ::DeleteObject(bmp);
            ::DeleteDC(mem);
            ::EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_CLOSE:
            // El boton X nativo envia WM_CLOSE — cerramos el popup.
            self->Hide();
            return 0;

        case WM_ACTIVATEAPP:
        case WM_KILLFOCUS:
            if (msg == WM_KILLFOCUS || wp == FALSE) self->Hide();
            return 0;

        case WM_DWMCOMPOSITIONCHANGED:
        case WM_THEMECHANGED: {
            if (self->m_te) self->m_te->Refresh();
            // Tras refrescar tema el chrome nativo puede tener otro tamano;
            // forzamos reflow para que el area cliente siga siendo 360x520.
            self->ReflowForTheme();
            // SIEMPRE resincronizamos el caption Aero con el estado REAL
            // de DWM despues de Refresh (cubre el escenario Classic <-> Aero
            // donde el orden de los mensajes no esta garantizado).
            self->DisableAeroCaption();
            self->EnableAeroCaption();
            // Tras un cambio de tema permitimos que el siguiente Paint
            // vuelva a re-evaluar (cubre transitorios entre los mensajes).
            self->m_themeResolvedAtPaint = FALSE;
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_NCDESTROY:
        case WM_DESTROY:
            return 0;
        }
        return ::DefWindowProc(hwnd, msg, wp, lp);
    }

} // namespace AeroClip::Core