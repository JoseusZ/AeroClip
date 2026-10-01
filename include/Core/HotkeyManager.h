#ifndef AEROCLIP_CORE_HOTKEYMANAGER_H
#define AEROCLIP_CORE_HOTKEYMANAGER_H

#include "AeroClip.h"
#include <windows.h>

namespace AeroClip::Core {

    class HotkeyManager {
    public:
        BOOL Register(HWND hwnd);
        void Unregister();
        BOOL IsRegistered() const { return m_hwnd != nullptr; }

    private:
        HWND m_hwnd = nullptr;
        int  m_id = -1;
    };

} // namespace AeroClip::Core

#endif // AEROCLIP_CORE_HOTKEYMANAGER_H