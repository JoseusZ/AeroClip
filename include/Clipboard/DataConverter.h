#ifndef AEROCLIP_CLIPBOARD_DATACONVERTER_H
#define AEROCLIP_CLIPBOARD_DATACONVERTER_H

#include "AeroClip.h"

namespace AeroClip::Clipboard {

    // Fallback: converts a CF_BITMAP (DDB) to DIB (CF_DIB) in memory.
    // Windows Print Screen typically places the capture as CF_BITMAP
    // without a corresponding CF_DIB; without a fallback, the capture is ignored.
    class DataConverter {
    public:
        static BOOL ReadUnicodeText(LPCVOID* ppOut, DWORD* pcbOut);
        static BOOL ReadDibImage(LPCVOID* ppOut, DWORD* pcbOut);
        static BOOL ReadBitmapAsDIB(LPCVOID* ppOut, DWORD* pcbOut);
    };

} // namespace AeroClip::Clipboard

#endif // AEROCLIP_CLIPBOARD_DATACONVERTER_H