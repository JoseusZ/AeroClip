// AeroClip - Common types and global constants
#ifndef AEROCLIP_H
#define AEROCLIP_H

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace AeroClip {

    inline constexpr int kHistoryMax = 10;
    inline constexpr int kFlyoutWidth = 250;
    inline constexpr int kFlyoutHeight = 425;

    // Limite estricto de elementos anclados persistentes.
    // Aeroclip mantiene una "banda de fijados" pequena y deliberada.
    inline constexpr int kMaxPinned = 4;

    // Encabezado del fichero persistente de un item anclado.
    // Ubicacion: %LOCALAPPDATA%\AeroClip\Pinned\<seq>.acp
    // El cuerpo es DPAPI cifrado con clave de usuario (CryptProtectData /
    // CryptUnprotectData), por lo que puede ser descifrado en otro arranque
    // del mismo usuario pero NO por otra cuenta de Windows.
#pragma pack(push, 1)
    struct PinnedHeader {
        DWORD    magic;          // 'ACPB' = AeroClip Pinned Block
        BYTE     version;        // 1
        BYTE     kind;           // ItemKind (Text=1, Image=2)
        BYTE     reserved[2];    // 0
        DWORD    cbData;         // tamano original (sin comprimir)
        DWORD    cbLz4;          // tamano LZ4 comprimido (0 para texto)
        DWORD    cbStored;       // tamano del cuerpo (DPAPI)
        FILETIME ftCaptured;     // timestamp original
    };
#pragma pack(pop)
    static_assert(sizeof(PinnedHeader) == 28, "PinnedHeader size mismatch");

    // Byte limit per item. Keeps the worst-case buffer size at
    // ~kHistoryMax * kMaxItemBytes (15 * 8 MB = 120 MB) and prevents
    // 4K screenshots or huge dumps from saturating RAM.
    inline constexpr DWORD kMaxTextBytes = 4 * 1024 * 1024;  // 4 MB texto
    inline constexpr DWORD kMaxImageBytes = 8 * 1024 * 1024; // 8 MB DIB

    enum class ItemKind : BYTE {
        Text = 1,
        Image = 2,
    };

    struct HistoryItem {
        ItemKind kind = ItemKind::Text;
        DWORD    cbData   = 0;     // tamano original (sin comprimir)
        DWORD    cbLz4    = 0;     // tamano LZ4 comprimido (sin DPAPI); 0 para texto
        DWORD    cbStored = 0;     // tamano en disco (lz4 + dpapi); 0 para texto
        LPVOID   pData    = nullptr;  // SOLO texto (HeapAlloc + DPAPI); nullptr para imagen
        SIZE_T   cbAlloc  = 0;
        BOOL     pinned   = FALSE;
        FILETIME ftCaptured = {};
        // --- NUEVO: persistencia en %TEMP%\AeroClip\<pid>_<seq>.bin ---
        // Solo se usan cuando kind == Image. Para texto permanecen vacios.
        WCHAR    szTempPath[MAX_PATH] = {};
        BOOL     bHasTemp = FALSE;
    };

    enum class Theme : BYTE {
        Classic = 0,
        Basic = 1,
        Aero = 2,
    };

    inline constexpr UINT MSG_CLIPBOARD_UPDATED = WM_USER + 1;
    inline constexpr UINT MSG_PERFORM_PASTE      = WM_USER + 2;
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