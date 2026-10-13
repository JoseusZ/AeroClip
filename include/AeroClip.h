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
    inline constexpr int kMaxPinned = 4;


#pragma pack(push, 1)
    struct PinnedHeader {
        DWORD    magic;          
        BYTE     version;       
        BYTE     kind;          
        BYTE     reserved[2];  
        DWORD    cbData;         
        DWORD    cbLz4;         
        DWORD    cbStored;       
        FILETIME ftCaptured;    
    };
#pragma pack(pop)
    static_assert(sizeof(PinnedHeader) == 28, "PinnedHeader size mismatch");

    // Byte limit per item. Keeps the worst-case buffer size at
    // ~kHistoryMax * kMaxItemBytes (15 * 8 MB = 120 MB) and prevents
    // 4K screenshots or huge dumps from saturating RAM.
    inline constexpr DWORD kMaxTextBytes = 4 * 1024 * 1024;  // 4 MB text
    inline constexpr DWORD kMaxImageBytes = 8 * 1024 * 1024; // 8 MB DIB

    enum class ItemKind : BYTE {
        Text = 1,
        Image = 2,
    };

    struct HistoryItem {
        ItemKind kind = ItemKind::Text;
        DWORD    cbData   = 0;     // original size (without compression)
        DWORD    cbLz4    = 0;     // compressed LZ4 size (without DPAPI); 0 for text
        DWORD    cbStored = 0;     // size on disk (lz4 + dpapi); 0 for text
        LPVOID   pData    = nullptr;  // text ONLY (HeapAlloc + DPAPI); nullptr for image
        SIZE_T   cbAlloc  = 0;
        BOOL     pinned   = FALSE;
        FILETIME ftCaptured = {};
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
    inline constexpr int  kHotkeyId    = 1;
    // Win+X
    inline constexpr int  kWinXHotkeyId = 2;

    struct ClipboardSession {
        BOOL ok;
        explicit ClipboardSession(HWND h = nullptr) : ok(::OpenClipboard(h)) {}
        ~ClipboardSession() { if (ok) ::CloseClipboard(); }
        ClipboardSession(const ClipboardSession&) = delete;
        ClipboardSession& operator=(const ClipboardSession&) = delete;
    };

} // namespace AeroClip

#endif // AEROCLIP_H