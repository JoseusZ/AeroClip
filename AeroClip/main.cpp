// AeroClip - Win32 entry point.
// Native background application: registers Win+V, monitors the clipboard
// via WM_CLIPBOARDUPDATE, and displays the flyout with encrypted history in RAM.

#include "AeroClip.h"
#include "UI/ThemeEngine.h"
#include "Storage/HistoryBuffer.h"
#include "Clipboard/ClipboardMonitor.h"
#include "Core/FlyoutWindow.h"
#include "Core/HotkeyManager.h"

using namespace AeroClip;

static Storage::HistoryBuffer       g_buffer;
static UI::ThemeEngine              g_theme;
static Clipboard::ClipboardMonitor  g_monitor;
static Core::FlyoutWindow           g_flyout;
static Core::HotkeyManager          g_hotkey;

static HWND g_hostHwnd = nullptr;

LRESULT CALLBACK HostWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_HOTKEY:
        if (wp == kHotkeyId) g_flyout.Toggle();
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
    if (!CreateHostWindow(hInst)) return 1;

    if (!g_flyout.Create(hInst, g_buffer, g_theme)) return 2;

    // Actions triggered by the flyout's context menu
    g_flyout.SetContextActions(
        [&](int idx) { g_buffer.Remove(idx); },
        [&](int idx) { g_buffer.TogglePin(idx); },
        [&]()       { g_buffer.RemoveUnpinned(); });

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
    return 0;
}