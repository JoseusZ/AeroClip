#include "Core/HotkeyManager.h"

namespace AeroClip::Core {

    BOOL HotkeyManager::Register(HWND hwnd) {
        if (m_hwnd) return TRUE;
        m_hwnd = hwnd;
        m_id = 1;
        if (!::RegisterHotKey(hwnd, m_id, MOD_WIN | MOD_NOREPEAT, 'V')) {
            m_hwnd = nullptr;
            m_id = -1;
            return FALSE;
        }
        return TRUE;
    }

    void HotkeyManager::Unregister() {
        if (m_hwnd && m_id != -1) {
            ::UnregisterHotKey(m_hwnd, m_id);
        }
        m_hwnd = nullptr;
        m_id = -1;
    }

} // namespace AeroClip::Core