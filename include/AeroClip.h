// AeroClip - Tipos comunes y constantes globales
#ifndef AEROCLIP_H
#define AEROCLIP_H

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace AeroClip {

    inline constexpr int kHistoryMax = 10;
    inline constexpr int kFlyoutWidth = 250;
    inline constexpr int kFlyoutHeight = 425;

    // Techo de bytes por item. Mantiene el peor caso del buffer en
    // ~kHistoryMax * kMaxItemBytes (15 * 8 MB = 120 MB) y evita que
    // capturas de pantalla 4K o volcados enormes saturen la RAM.
    inline constexpr DWORD kMaxTextBytes = 4 * 1024 * 1024;  // 4 MB texto
    inline constexpr DWORD kMaxImageBytes = 8 * 1024 * 1024; // 8 MB DIB

    enum class ItemKind : BYTE {
        Text = 1,
        Image = 2,
    };

    struct HistoryItem {
        ItemKind kind = ItemKind::Text;
        DWORD    cbData = 0;
        LPVOID   pData = nullptr;
        SIZE_T   cbAlloc = 0;
        BOOL     pinned = FALSE;
        FILETIME ftCaptured = {};
    };

    enum class Theme : BYTE {
        Classic = 0,
        Basic = 1,
        Aero = 2,
    };

    inline constexpr UINT MSG_CLIPBOARD_UPDATED = WM_USER + 1;
    inline constexpr int  kHotkeyId = 1;

    struct ClipboardSession {
        BOOL ok;
        explicit ClipboardSession(HWND h = nullptr) : ok(::OpenClipboard(h)) {}
        ~ClipboardSession() { if (ok) ::CloseClipboard(); }
        ClipboardSession(const ClipboardSession&) = delete;
        ClipboardSession& operator=(const ClipboardSession&) = delete;
    };

} // namespace AeroClip

#endif // AEROCLIP_H