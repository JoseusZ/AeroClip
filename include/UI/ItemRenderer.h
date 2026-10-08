#ifndef AEROCLIP_UI_ITEMRENDERER_H
#define AEROCLIP_UI_ITEMRENDERER_H

#include "AeroClip.h"
#include "Storage/HistoryBuffer.h"
#include "UI/ThemeEngine.h"
#include <windows.h>

namespace AeroClip::UI {

    class ItemRenderer {
    public:
        // Tamano del SLOT cuadrado y FIJO reservado para la miniatura en cada
        // item. Dimensionado para LLENAR el alto del item (74px) sin cambiar
        // el tamano del card. Con kThumbSlotPad=2: 2 + 70 + 2 = 74 (ajuste
        // perfecto, sin aire desperdiciado). La imagen NUNCA puede salir de
        // este recuadro y se escala hacia abajo preservando aspect ratio
        // desde el JPEG guardado a 256px.
        static constexpr int kThumbSlotSize = 70;

        // Padding desde los bordes del item hasta el slot.
        static constexpr int kThumbSlotPad = 2;

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