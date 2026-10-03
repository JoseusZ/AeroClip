// AeroClip - Win32 entry point.
// Native background application: registers Win+V, monitors the clipboard
// via WM_CLIPBOARDUPDATE, and displays the flyout with encrypted history in RAM.

#include "AeroClip.h"
#include "UI/ThemeEngine.h"
#include "Storage/HistoryBuffer.h"
#include "Clipboard/ClipboardMonitor.h"
#include "Core/FlyoutWindow.h"
#include "Core/HotkeyManager.h"
#include "Utils/InputInjector.h"
#include <objbase.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

using namespace AeroClip;

static Storage::HistoryBuffer       g_buffer;
static UI::ThemeEngine              g_theme;
static Clipboard::ClipboardMonitor  g_monitor;
static Core::FlyoutWindow           g_flyout;
static Core::HotkeyManager          g_hotkey;

static HWND g_hostHwnd = nullptr;

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
        if (wp == kHotkeyId) g_flyout.Toggle();
        return 0;
    case MSG_PERFORM_PASTE:
        // Llega tras click en una tarjeta: el flyout ya esta oculto, asi
        // que el foco esta volviendo a la app previa y SendInput llega al
        // destino previsto en lugar de Sigue blo blo blo.
        PerformPasteFromIndex(static_cast<int>(wp));
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
    // GDI+ es necesario para las miniaturas JPEG (Opcion C).
    Gdiplus::GdiplusStartupInput gdiStartupIn{};
    ULONG_PTR gdiToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiToken, &gdiStartupIn, nullptr) != Gdiplus::Ok) {
        return 1;
    }

    if (!CreateHostWindow(hInst)) {
        Gdiplus::GdiplusShutdown(gdiToken);
        return 1;
    }

    if (!g_flyout.Create(hInst, g_buffer, g_theme)) return 2;

    // Actions triggered by the flyout's context menu
    g_flyout.SetContextActions(
        [&](int idx) { g_buffer.Remove(idx); },
        [&](int idx) { g_buffer.TogglePin(idx); },
        [&]()       { g_buffer.RemoveUnpinned(); });

    // Click en una tarjeta -> diferir el pegado al host HWND. Asi el
    // flyout termina de ocultarse, el foco vuelve a la app previa, y
    // SendInput llega al destino correcto sin races con WM_KILLFOCUS.
    g_flyout.SetOnSelect([](int idx) {
        if (g_hostHwnd) ::PostMessage(g_hostHwnd, MSG_PERFORM_PASTE,
                                      static_cast<WPARAM>(idx), 0);
    });

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