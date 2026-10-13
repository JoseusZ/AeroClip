#include "AeroClip.h"
#include "Version.h"
#include "UI/ThemeEngine.h"
#include "UI/Localization.h"
#include "Storage/HistoryBuffer.h"
#include "Clipboard/ClipboardMonitor.h"
#include "Core/FlyoutWindow.h"
#include "Core/HotkeyManager.h"
#include "UI/WinXMenu.h"
#include "Utils/InputInjector.h"
#include "Utils/AutoStart.h"
#include <objbase.h>
#include <gdiplus.h>
#include <shellapi.h>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")

using namespace AeroClip;

static Storage::HistoryBuffer       g_buffer;
static UI::ThemeEngine              g_theme;
static Clipboard::ClipboardMonitor  g_monitor;
static Core::FlyoutWindow           g_flyout;
static Core::HotkeyManager          g_hotkey;
static UI::WinXMenu                 g_winx;

static HWND g_hostHwnd = nullptr;

// ---- Balloon / toast 
static constexpr UINT WM_TRAYICON = WM_USER + 10;

static void ShowBalloonTip(LPCWSTR title, LPCWSTR text) {
    if (!g_hostHwnd) return;
    NOTIFYICONDATAW nid = {};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = g_hostHwnd;
    nid.uID              = 1;
    nid.uFlags           = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_INFO;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon            = ::LoadIconW(nullptr, IDI_APPLICATION);
    ::wcscpy_s(nid.szTip, L"AeroClip");
    ::wcscpy_s(nid.szInfoTitle, title);
    ::wcscpy_s(nid.szInfo, text);
    nid.dwInfoFlags      = NIIF_WARNING;
    if (::Shell_NotifyIconW(NIM_ADD, &nid)) {
        
        MSG msg;
        for (int i = 0; i < 5; i++) {
            if (::PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                ::TranslateMessage(&msg);
                ::DispatchMessage(&msg);
            }
            ::Sleep(600);
        }
        ::Shell_NotifyIconW(NIM_DELETE, &nid);
    }
}

static void PerformPasteFromIndex(int idx) {
        if (idx < 0 || idx >= g_buffer.Count()) return;
        const auto& it = g_buffer.Get(idx);
        if (it.kind == AeroClip::ItemKind::Image)
            Utils::InputInjector::PasteImage(it);
        else
            Utils::InputInjector::PasteText(it);
    }

LRESULT CALLBACK HostWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_HOTKEY:
        if (wp == kHotkeyId)
            g_flyout.Toggle();
        else if (wp == kWinXHotkeyId)
            g_winx.Show();
        return 0;
    case MSG_PERFORM_PASTE:

        PerformPasteFromIndex(static_cast<int>(wp));
        return 0;
    case WM_TRAYICON:

        return 0;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProc(hwnd, msg, wp, lp);
}

static BOOL CreateHostWindow(HINSTANCE hInst) {
    WNDCLASSEX wc = { sizeof(wc) };
    wc.lpfnWndProc   = HostWndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"AeroClipHost";
    if (!::RegisterClassEx(&wc)) {
        if (::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return FALSE;
    }
    g_hostHwnd = ::CreateWindowEx(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0,
                                  HWND_MESSAGE, nullptr, hInst, nullptr);
    return g_hostHwnd != nullptr;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {

    ::OutputDebugStringW((AeroClip::FullVersionW() + L" - arrancando\r\n").c_str());

    Gdiplus::GdiplusStartupInput gdiStartupIn{};
    ULONG_PTR gdiToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiToken, &gdiStartupIn, nullptr) != Gdiplus::Ok) {
        return 1;
    }

    if (!CreateHostWindow(hInst)) {
        Gdiplus::GdiplusShutdown(gdiToken);
        return 1;
    }

    // First-run auto-start: writes HKCU\Software\Microsoft\Windows\

    if (!AeroClip::Utils::AutoStart::RegisterOnFirstRun()) {
        ::OutputDebugStringW(L"AeroClip: AutoStart registration failed (no-op)\n");
    }

    if (!g_flyout.Create(hInst, g_buffer, g_theme)) {
        return 2;
    }

    // Actions triggered by the flyout's context menu
    g_flyout.SetContextActions(
        [&](int idx) { g_buffer.Remove(idx);      g_flyout.Refresh(); },
        [&](int idx) { g_buffer.TogglePin(idx);   g_flyout.Refresh(); },
        [&]()       { g_buffer.RemoveUnpinned();  g_flyout.Refresh(); });

    g_flyout.SetOnSelect([](int idx) {
        if (g_hostHwnd) ::PostMessage(g_hostHwnd, MSG_PERFORM_PASTE,
                                      static_cast<WPARAM>(idx), 0);
    });

    g_buffer.SetOnPinLimitExceeded([]() {
        const std::wstring msg = AeroClip::UI::LocPinLimitMessage(kMaxPinned);
        ShowBalloonTip(AeroClip::UI::LocPinLimitTitle(), msg.c_str());
    });

    
    int loaded = g_buffer.LoadPinnedFromDisk();
    if (loaded > 0) {
        g_flyout.Refresh();
    }

    // Registers Win+V against the host (HostWndProc handles WM_HOTKEY)
    if (!g_hotkey.Register(g_hostHwnd)) {
        ::OutputDebugStringW(L"AeroClip: RegisterHotKey failed (Win+V ya estaba tomado)\n");
    }

    // Clipboard monitor: sends MSG_CLIPBOARD_UPDATED to the flyout (PostMessage)
    g_monitor.Start(g_buffer, g_flyout.GetHwnd());

    MSG msg;
    while (::GetMessage(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
    }

    g_hotkey.Unregister();
    g_monitor.Stop();
    Gdiplus::GdiplusShutdown(gdiToken);
    return 0;
}