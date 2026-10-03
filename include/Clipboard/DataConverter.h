#ifndef AEROCLIP_CLIPBOARD_DATACONVERTER_H
#define AEROCLIP_CLIPBOARD_DATACONVERTER_H

#include "AeroClip.h"

namespace AeroClip::Clipboard {

    class DataConverter {
    public:
        static BOOL ReadUnicodeText(LPCVOID* ppOut, DWORD* pcbOut);
        static BOOL ReadDibImage(LPCVOID* ppOut, DWORD* pcbOut);
        // Fallback: convierte un CF_BITMAP (DDB) a DIB (CF_DIB) en memoria.
        // Print Screen de Windows suele colocar la captura como CF_BITMAP
        // sin companante de CF_DIB; sin fallback la captura se ignora.
        static BOOL ReadBitmapAsDIB(LPCVOID* ppOut, DWORD* pcbOut);
    };

} // namespace AeroClip::Clipboard

#endif // AEROCLIP_CLIPBOARD_DATACONVERTER_H