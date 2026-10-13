#ifndef AEROCLIP_CORE_HOTKEYMANAGER_H
#define AEROCLIP_CORE_HOTKEYMANAGER_H

#include "AeroClip.h"
#include <windows.h>

namespace AeroClip::Core {

    // Gestiona multiples hotkeys globales con RegisterHotKey.
    // Pensado para Win7: el shell no reserva Win+X ni Win+V, asi
    // ambos funcionan sin conflicto.
    //
    // Cada hotkey tiene un identificador entero propio (kHotkeyId = 1,
    // kWinXHotkeyId = 2, declarados en AeroClip.h). El HostWndProc
    // discrimina cada WM_HOTKEY por su wParam.
    class HotkeyManager {
    public:
        // Devuelve TRUE si al menos un hotkey pudo registrarse.
        BOOL Register(HWND hwnd);
        void Unregister();
        BOOL IsRegistered() const { return m_hwnd != nullptr; }

        // TRUE si este id corresponde a un hotkey nuestro
        // (asi el caller sabe si debe procesarlo o no).
        BOOL Owns(int id) const {
            return m_hwnd != nullptr
                && (id == kHotkeyId || id == kWinXHotkeyId);
        }

    private:
        HWND m_hwnd     = nullptr;

        // FALSE si RegisterHotKey fallo para esa combinacion concreta
        // (p.ej. otra app ya tiene Win+X reservado).
        BOOL m_flyoutOK = FALSE;
        BOOL m_winxOK   = FALSE;
    };

} // namespace AeroClip::Core

#endif // AEROCLIP_CORE_HOTKEYMANAGER_H