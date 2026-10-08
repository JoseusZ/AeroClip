#ifndef AEROCLIP_UI_ITEMRENDERER_H
#define AEROCLIP_UI_ITEMRENDERER_H

#include "AeroClip.h"
#include "Storage/HistoryBuffer.h"
#include "UI/ThemeEngine.h"
#include <windows.h>

namespace AeroClip::UI {

    class ItemRenderer {
    public:
        // Ancho máximo del slot (para fotos 16:9). El alto se calcula
        // dinamicamente como kItemHeight - 2*kThumbSlotPad.
        static constexpr int kThumbSlotSize = 114;

        // Margen superior/inferior alrededor de la imagen.
        static constexpr int kThumbSlotPad = 5;

        // Separacion horizontal entre el borde derecho del slot y el texto.
        static constexpr int kThumbGap = 8;

        static RECT MoreButtonRect(const RECT& itemRect);
        static void DrawCard(HDC hdc, const RECT& rc, const ThemeEngine& te, BOOL hovered);

        // Dibuja la imagen DENTRO del rectangulo "thumbSlot" (cuadrado y
        // fijo, kThumbSlotSize x kThumbSlotSize). Escala hacia ABAJO desde
        // el tamano del JPEG guardado (que es de alta resolucion, max 256 en
        // su lado mas largo) preservando aspect ratio. La imagen dibujada
        // puede ser menor que el slot si no es 1:1 (letterbox). Devuelve
        // el RECT real donde quedo dibujada.
        static RECT DrawImageThumb(HDC hdc, const RECT& thumbSlot,
            const BYTE* jpeg, DWORD cbJpeg);

        static void DrawTextLabel(HDC hdc, const RECT& rc, LPCWSTR text,
            const ThemeEngine& te, BOOL hovered, BOOL hasThumb,
            int thumbRight = 0);

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