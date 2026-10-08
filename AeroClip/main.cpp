// AeroClip - Win32 entry point.
// Native background application: registers Win+V, monitors the clipboard
// via WM_CLIPBOARDUPDATE, and displays the flyout with encrypted history in RAM.

#include "AeroClip.h"
#include "UI/ThemeEngine.h"
#include "UI/Localization.h"
#include "Storage/HistoryBuffer.h"
#include "Clipboard/ClipboardMonitor.h"
#include "Core/FlyoutWindow.h"
#include "Core/HotkeyManager.h"
#include "Utils/InputInjector.h"
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

static HWND g_hostHwnd = nullptr;

// ---- Balloon / toast compatible con Windows 7 ------------------------
// En Windows 7 la API legacy de Shell_NotifyIcon muestra el balloon
// directamente (no lo enruta por Windows Runtime como Win10/11), asi
// que la tecnica "agregar y borrar inmediatamente" SI funciona: el
// balloon ya fue encolado por el shell antes de que borremos el icono.
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
        // Bombea mensajes para que el shell procese el balloon antes de
        // borrar el icono (en Win7 basta con un par de PeekMessage).
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
// ---------------------------------------------------------------------------

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

        PerformPasteFromIndex(static_cast<int>(wp));
        return 0;
    case WM_TRAYICON:
        // Icono efimero, ignorar cualquier mensaje residual.
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

    Gdiplus::GdiplusStartupInput gdiStartupIn{};
    ULONG_PTR gdiToken = 0;
    if (Gdiplus::GdiplusStartup(&gdiToken, &gdiStartupIn, nullptr) != Gdiplus::Ok) {
        return 1;
    }

    if (!CreateHostWindow(hInst)) {
        Gdiplus::GdiplusShutdown(gdiToken);
        return 1;
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

    // Callback: cuando TogglePin es rechazado por kMaxPinned, mostramos
    // un balloon tip nativo pidiendo al usuario que desancle uno primero.
    g_buffer.SetOnPinLimitExceeded([]() {
        const std::wstring msg = AeroClip::UI::LocPinLimitMessage(kMaxPinned);
        ShowBalloonTip(AeroClip::UI::LocPinLimitTitle(), msg.c_str());
    });

    // Carga los anclados persistentes del arranque anterior. Esto se hace
    // ANTES de iniciar el monitor del portapapeles para que las nuevas
    // capturas que lleguen no se mezclen ni hagan overflow sobre los pinned.
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