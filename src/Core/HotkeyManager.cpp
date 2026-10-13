#include "Core/HotkeyManager.h"

namespace AeroClip::Core {

    BOOL HotkeyManager::Register(HWND hwnd) {
        if (m_hwnd) return TRUE;
        m_hwnd = hwnd;

        // Win+V
        m_flyoutOK = ::RegisterHotKey(hwnd, kHotkeyId,
                                      MOD_WIN | MOD_NOREPEAT, 'V');

        // Win+X 
        m_winxOK = ::RegisterHotKey(hwnd, kWinXHotkeyId,
                                    MOD_WIN | MOD_NOREPEAT, 'X');
        if (m_winxOK) {
            ::OutputDebugStringW(L"AeroClip: Win+X registrado OK\n");
        } else {
            ::OutputDebugStringW(L"AeroClip: Win+X OCUPADO por otra app\n");
        }

        // If NONE was registered -> FALSE. If at least one -> TRUE
        if (!m_flyoutOK && !m_winxOK) {
            m_hwnd = nullptr;
            m_flyoutOK = FALSE;
            m_winxOK = FALSE;
            return FALSE;
        }
        return TRUE;
    }

    void HotkeyManager::Unregister() {
        if (m_hwnd) {
            if (m_flyoutOK) ::UnregisterHotKey(m_hwnd, kHotkeyId);
            if (m_winxOK)   ::UnregisterHotKey(m_hwnd, kWinXHotkeyId);
        }
        m_hwnd     = nullptr;
        m_flyoutOK = FALSE;
        m_winxOK   = FALSE;
    }

} // namespace AeroClip::Core