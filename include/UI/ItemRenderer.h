#ifndef AEROCLIP_UI_ITEMRENDERER_H
#define AEROCLIP_UI_ITEMRENDERER_H

#include "AeroClip.h"
#include "Storage/HistoryBuffer.h"
#include "UI/ThemeEngine.h"
#include <windows.h>

namespace AeroClip::UI {

    class ItemRenderer {
    public:
        static constexpr int kThumbSize = 48;

        static RECT MoreButtonRect(const RECT& itemRect);
        static void DrawCard(HDC hdc, const RECT& rc, const ThemeEngine& te, BOOL hovered);
        static void DrawTextPlaceholder(HDC hdc, const RECT& rc, const ThemeEngine& te);
        static void DrawImageThumb(HDC hdc, const RECT& rc, const BYTE* dib, DWORD cbDib);
        static void DrawTextLabel(HDC hdc, const RECT& rc, LPCWSTR text, const ThemeEngine& te, BOOL hovered);
        static void DrawPinBadge(HDC hdc, const RECT& rc);
        static void DrawMoreButton(HDC hdc, const RECT& rc, const ThemeEngine& te, BOOL hovered);

        static void DrawItem(
            HDC hdc,
            const RECT& rc,
            const AeroClip::Storage::RenderItem& cached,
            BOOL pinned,
            BOOL hovered,
            const ThemeEngine& te);
    };

} // namespace AeroClip::UI

#endif // AEROCLIP_UI_ITEMRENDERER_H