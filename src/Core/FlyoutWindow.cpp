#include "Core/FlyoutWindow.h"
#include "UI/ItemRenderer.h"
#include "UI/Localization.h"
#include "AeroClip.h"
#include <dwmapi.h>
#include <uxtheme.h>
#include <vsstyle.h>
#include <algorithm>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

namespace AeroClip::Core {

    // Approximate height of the native caption (SM_CYSMCAPTION)—the one DWM extends.
    static int GetSystemCaptionHeight() {
        DWORD cx = ::GetSystemMetrics(SM_CYCAPTION);
        if (cx == 0) cx = 26; // reasonable fallback for Win7
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
        DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
        style &= ~(WS_MINIMIZEBOX | WS_MAXIMIZEBOX);

        RECT wanted = { 0, 0, kFlyoutWidth, kFlyoutHeight };
        ::AdjustWindowRectEx(&wanted, style, FALSE, WS_EX_TOOLWINDOW | WS_EX_TOPMOST);
        int totalW = wanted.right  - wanted.left;
        int totalH = wanted.bottom - wanted.top;

        int x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - totalW) / 2;
        int y = mi.rcWork.top  + 80;

        m_hwnd = ::CreateWindowEx(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            kClass, AeroClip::UI::LocFlyoutTitle(),
            style,
            x, y, totalW, totalH,
            nullptr, nullptr, hInst, this);

        if (!m_hwnd) return FALSE;

        ::SetWindowLongPtr(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        if (m_te) m_te->Refresh();
        m_hScroll = ::CreateWindowEx(
            0, L"SCROLLBAR", nullptr,
            SBS_VERT | WS_CHILD | WS_VISIBLE,
            0, 0, kScrollBarWidth, 100,  // initial position; UpdateScrollBar fixes it
            m_hwnd, nullptr, hInst, nullptr);
        if (m_hScroll) {
            // By default, the scrollbars extend to the maximum extent and do not hide when the range is empty.
            ::SetScrollRange(m_hScroll, SB_CTL, 0, 1, TRUE);
        }

        UpdateScrollBar();

        EnableAeroCaption();
        ::DwmFlush();
        return TRUE;
    }

    int FlyoutWindow::CaptionHeight() const {
        return GetSystemCaptionHeight();
    }

    void FlyoutWindow::ReflowForTheme() {
        if (!m_hwnd) return;
        DWORD style = static_cast<DWORD>(::GetWindowLongPtr(m_hwnd, GWL_STYLE));
        DWORD ex    = static_cast<DWORD>(::GetWindowLongPtr(m_hwnd, GWL_EXSTYLE));
        RECT wanted = { 0, 0, kFlyoutWidth, kFlyoutHeight };
        ::AdjustWindowRectEx(&wanted, style, FALSE, ex);
        int totalW = wanted.right  - wanted.left;
        int totalH = wanted.bottom - wanted.top;
        ::SetWindowPos(m_hwnd, nullptr, 0, 0, totalW, totalH,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        // After the reflow, GetClientRect will be re-entered via WM_SIZE and update m_clientW/H
    }

    void FlyoutWindow::EnableAeroCaption() {
        m_aeroExtended = FALSE;
        if (!m_te || !m_hwnd) return;

        BOOL comp = FALSE;
        if (FAILED(::DwmIsCompositionEnabled(&comp)) || !comp) return;
        if (!m_te->IsAeroGlass()) return;
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
        return { kSidePadding, y, ListAreaRight() - kSidePadding, y + kItemHeight };
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
        if (x < kSidePadding || x > ListAreaRight() - kSidePadding) return -1;

        return idx;
    }

    void FlyoutWindow::UpdateHover(int x, int y) {
        int oldItem = m_hoverItem;
        int oldMore = m_moreHover;

        m_hoverItem = -1;
        m_moreHover = -1;

        int item = HitTestItem(x, y);
        if (item >= 0) {
            RECT rcMore = MoreButtonRect(item);
            BOOL overMore = (x >= rcMore.left && x < rcMore.right &&
                              y >= rcMore.top  && y < rcMore.bottom);
            if (overMore) {

                m_moreHover = item;
            }
            else {
                m_hoverItem = item;
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
        UpdateScrollBar();
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
        UpdateScrollBar();
        // Ensures that the thumbnails are calculated for the current state of the buffer
        if (m_buffer) m_buffer->PrepareRender();
        ::ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        ::SetForegroundWindow(m_hwnd);
        ::InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    void FlyoutWindow::Hide() {
        if (!m_hwnd) return;
        ::ShowWindow(m_hwnd, SW_HIDE);
    }

    void FlyoutWindow::OnClipboardUpdate() {
        // Regenerates the thumbnail cache (includes decompressing LZ4 and building JPEGs for images)
        if (m_buffer) m_buffer->PrepareRender();
        if (m_hwnd && ::IsWindowVisible(m_hwnd)) {
            ::InvalidateRect(m_hwnd, nullptr, FALSE);
        }
    }

    void FlyoutWindow::Refresh() {
        // Llamado por main.cpp tras Remove / TogglePin / RemoveUnpinned para que la flyout repinte inmediatamente
        RecalcScrollBounds();
        if (m_buffer) m_buffer->PrepareRender();
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
        RECT rc{ kSidePadding, ListAreaTop(), ListAreaRight() - kSidePadding, ListAreaBottom() };
        ::DrawTextW(hdc, AeroClip::UI::LocEmptyState(),
            -1, &rc, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        (void)rcClient;
        ::SelectObject(hdc, old);
    }

    static void BuildRenderItemForPaint_DELETED(
            const AeroClip::HistoryItem& hi,
            Storage::RenderItem& out) {
        out.text.clear();
        out.thumbnailJpeg.clear();
  
        (void)hi;
        (void)out;
    }

    void FlyoutWindow::PaintItems(HDC hdc, const RECT& rcClient) {
        int n = m_buffer ? m_buffer->Count() : 0;
        int top = ListAreaTop();
        int bot = ListAreaBottom();
        ::SetBkMode(hdc, TRANSPARENT);
        if (n == 0) { PaintEmptyState(hdc, rcClient); return; }

        // Clip to the list area to avoid painting outside (leaving room for the native scrollbar).
        HRGN rgn = ::CreateRectRgn(kSidePadding, top,
                                    ListAreaRight() - kSidePadding, bot);
        ::SelectClipRgn(hdc, rgn);
        ::DeleteObject(rgn);

        for (int i = 0; i < n; ++i) {
            RECT rc = ItemRect(i);
            if (rc.bottom <= top || rc.top >= bot) continue;
            BOOL hovered = (m_hoverItem == i);
            BOOL moreH = (m_moreHover == i);

			const auto item = m_buffer->Get(i);   // copy for value 
            BOOL pinned = item.pinned;

            // Native background per theme:
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
                // Margin required by uxtheme to render the gradient/border.
                RECT rcDraw = rc;
                ::DrawThemeBackground(hThemeList, hdc, partId, stateId, &rcDraw, nullptr);
            }
            else {
                // Classic / Basic: GetSysColor preserves user customization.
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

            // Actual content (text/image) using native APIs

                        const Storage::RenderItem& ri = m_buffer->GetCached(i);
                        UI::ItemRenderer::DrawItem(hdc, rc, ri, pinned, hovered, *m_te);
            UI::ItemRenderer::DrawMoreButton(hdc, MoreButtonRect(i), *m_te, moreH);
            HRGN reapply = ::CreateRectRgn(kSidePadding, top,
                                            ListAreaRight() - kSidePadding, bot);
            ::SelectClipRgn(hdc, reapply);
            ::DeleteObject(reapply);
        }

        ::SelectClipRgn(hdc, nullptr);
    }

    void FlyoutWindow::UpdateScrollBar() {
        if (!m_hwnd || !m_hScroll) return;
        int n = m_buffer ? m_buffer->Count() : 0;

        // Position the control—and it depends on the subject matter—so as not to clash with it.
        
        int sbX = m_clientW - kScrollBarWidth;
        int sbY = 0;
        if (m_te && m_te->Current() == AeroClip::Theme::Aero) {
            // Beneath the title that DWM displays within the client area.
            sbY = CaptionHeight();
        }
        int sbH = m_clientH - sbY;
        if (sbH < 16) sbH = 16; 
        ::MoveWindow(m_hScroll, sbX, sbY, kScrollBarWidth, sbH, TRUE);

        if (n == 0) {
            
            ::ShowWindow(m_hScroll, SW_HIDE);
            return;
        }
        ::ShowWindow(m_hScroll, SW_SHOW);

        // SCROLLBAR native of Win32 
        int content = ContentHeight(n);
        int visible = ListAreaHeight();
        SCROLLINFO si = {};
        si.cbSize = sizeof(si);
        si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
        si.nMin   = 0;
        si.nMax   = (content > 0) ? (content - 1) : 0;
        si.nPage  = (UINT)visible;
        si.nPos   = m_scrollY;
        ::SetScrollInfo(m_hScroll, SB_CTL, &si, TRUE);
    }

    void FlyoutWindow::Paint(HDC hdc, const RECT& rcClient) {
        const auto& p = m_te->Colors();

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
        // The scrollbar is drawn by the OS (WS_VSCROLL with the active theme's chrome).
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

            return 1;

        case WM_SIZE: {

                            self->m_clientW = LOWORD(lp);
                            self->m_clientH = HIWORD(lp);
                            self->UpdateScrollBar();
                            return 0;
                        }

                case WM_VSCROLL: {

                            int n = self->m_buffer ? self->m_buffer->Count() : 0;
                            int maxScroll = self->MaxScrollY(n);
                            int newY = self->m_scrollY;
                            int code = LOWORD(wp);
                            int posThumb = HIWORD(wp);
                            switch (code) {
                            case SB_LINEUP:        newY -= (self->kItemHeight + self->kItemGap); break;
                            case SB_LINEDOWN:      newY += (self->kItemHeight + self->kItemGap); break;
                            case SB_PAGEUP:        newY -= self->ListAreaHeight(); break;
                            case SB_PAGEDOWN:      newY += self->ListAreaHeight(); break;
                            case SB_THUMBPOSITION: newY = posThumb; break;
                            case SB_THUMBTRACK:    newY = posThumb; break;
                            case SB_TOP:           newY = 0; break;
                            case SB_BOTTOM:        newY = maxScroll; break;
                            default: return 0;
                            }
                            if (newY < 0) newY = 0;
                            if (newY > maxScroll) newY = maxScroll;
                            if (newY != self->m_scrollY) {
                                self->m_scrollY = newY;
                                SCROLLINFO si = {};
                                si.cbSize = sizeof(si);
                                si.fMask  = SIF_POS;
                                si.nPos   = newY;
                                ::SetScrollInfo(self->m_hScroll, SB_CTL, &si, TRUE);
                                ::InvalidateRect(hwnd, nullptr, FALSE);
                            }
                            return 0;
                        }

                case MSG_CLIPBOARD_UPDATED: {
                    self->RecalcScrollBounds();
                    if (self->m_buffer) self->m_buffer->PrepareRender();
                    ::InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }

        case WM_NCHITTEST: {

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
                    const wchar_t* lblDelete  = AeroClip::UI::LocMenuDelete();
                    const wchar_t* lblClear   = AeroClip::UI::LocMenuClearAll();
                    const bool pinned = self->m_buffer && self->m_buffer->Get(item).pinned;
                    const wchar_t* lblPin = pinned ? AeroClip::UI::LocMenuUnpin()
                                                   : AeroClip::UI::LocMenuPin();
                    ::AppendMenu(hMenu, MF_STRING, 1, lblDelete);
                    ::AppendMenu(hMenu, MF_STRING, 2, lblPin);
                    ::AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
                    ::AppendMenu(hMenu, MF_STRING, 3, lblClear);

                    if (pinned)
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
                const wchar_t* lblDelete  = AeroClip::UI::LocMenuDelete();
                const wchar_t* lblClear   = AeroClip::UI::LocMenuClearAll();
                const bool pinned = self->m_buffer && self->m_buffer->Get(item).pinned;
                const wchar_t* lblPin = pinned ? AeroClip::UI::LocMenuUnpin()
                                               : AeroClip::UI::LocMenuPin();
                ::AppendMenu(hMenu, MF_STRING, 1, lblDelete);
                ::AppendMenu(hMenu, MF_STRING, 2, lblPin);
                ::AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
                ::AppendMenu(hMenu, MF_STRING, 3, lblClear);

                if (pinned)
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
            // Synchronize the native scrollbar (move the thumb visually).
            SCROLLINFO si = {};
            si.cbSize = sizeof(si);
            si.fMask  = SIF_POS;
            si.nPos   = self->m_scrollY;
            ::SetScrollInfo(self->m_hScroll, SB_CTL, &si, TRUE);
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = ::BeginPaint(hwnd, &ps);

            RECT rc;
            ::GetClientRect(hwnd, &rc);

            // Double buffer to eliminate flickering.
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
            // The native X button sends WM_CLOSE
            self->Hide();
            return 0;

        case WM_ACTIVATEAPP:
        case WM_KILLFOCUS:
            if (msg == WM_KILLFOCUS || wp == FALSE) self->Hide();
            return 0;

        case WM_DWMCOMPOSITIONCHANGED:
        case WM_THEMECHANGED: {

            ::SetTimer(hwnd, FlyoutWindow::kThemeCoalesceTimerId,
                        FlyoutWindow::kThemeCoalesceMs, nullptr);
            return 0;
        }

        case WM_TIMER: {
            if (wp == FlyoutWindow::kThemeCoalesceTimerId) {
                ::KillTimer(hwnd, FlyoutWindow::kThemeCoalesceTimerId);
                if (self->m_te) self->m_te->Refresh();
                self->DisableAeroCaption();
                ::DwmFlush();
                ::SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE |
                    SWP_NOSIZE | SWP_FRAMECHANGED);
                self->ReflowForTheme();
                self->EnableAeroCaption();
                ::InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;
        }

        case WM_NCDESTROY:
        case WM_DESTROY:
            return 0;
        }
        return ::DefWindowProc(hwnd, msg, wp, lp);
    }

} // namespace AeroClip::Core