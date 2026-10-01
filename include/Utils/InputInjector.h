#ifndef AEROCLIP_UTILS_INPUTINJECTOR_H
#define AEROCLIP_UTILS_INPUTINJECTOR_H

#include "AeroClip.h"

namespace AeroClip::Utils {

    class InputInjector {
    public:
        static BOOL PasteText(const AeroClip::HistoryItem& item);
        static BOOL PasteImage(const AeroClip::HistoryItem& item);
    };

} // namespace AeroClip::Utils

#endif // AEROCLIP_UTILS_INPUTINJECTOR_H