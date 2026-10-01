#ifndef AEROCLIP_CLIPBOARD_DATACONVERTER_H
#define AEROCLIP_CLIPBOARD_DATACONVERTER_H

#include "AeroClip.h"

namespace AeroClip::Clipboard {

    class DataConverter {
    public:
        static BOOL ReadUnicodeText(LPCVOID* ppOut, DWORD* pcbOut);
        static BOOL ReadDibImage(LPCVOID* ppOut, DWORD* pcbOut);
    };

} // namespace AeroClip::Clipboard

#endif // AEROCLIP_CLIPBOARD_DATACONVERTER_H