#ifndef AEROCLIP_UI_WINXMENU_H
#define AEROCLIP_UI_WINXMENU_H

// AeroClip - Menu Win+X estilo Windows 7 nativo.
//
// Este menu reproduce los 16 items del antiguo menu contextual "Power User"
// de Windows 8, pero dibujado por comctl32 v6 de Windows 7: fondo claro,
// fuente Tahoma 8pt, separadores planos, sin bordes tematizados. Es la
// misma apariencia del menu contextual del boton Inicio en Win7.
//
// Se activa con el hotk global
//
//     Win + X        (registrado por HotkeyManager)
//
// El menu solo se construye cuando el usuario lo invoca: en reposo no
// consume RAM adicional mas alla de esta clase.
//
// Coste: ~150-250 KB transitorios al pulsar Win+X (HMENU + HWND del
// popup), liberados al cerrarse. +0 bytes en estado idle.

#include "AeroClip.h"
#include <windows.h>

namespace AeroClip::UI {

	class WinXMenu {
	public:
		WinXMenu() = default;
		~WinXMenu();

		// Muestra el menu anclado arriba del boton Inicio. Llamar en
		// respuesta a WM_HOTKEY (wParam == kWinXHotkeyId). Bloquea
		// hasta que el usuario selecciona o cancela; no abre ventanas
		// persistentes.
		//
		// Crea perezosa-mente un "anchor" top-level oculto (clase
		// AeroClipWinXAnchor) que se usa como owner del popup y como
		// ventana de SetForegroundWindow. Esto es CRITICO en Win7:
		// si pasamos como owner una ventana del propio proceso que
		// tiene WS_EX_TOPMOST o captura clicks (como la FlyoutWindow),
		// la regla de "owned-window destruction cascade" ata el ciclo
		// de vida del menu al de esa ventana, y el menu deja de
		// detectar clicks externos (solo se cierra cuando se destruye
		// el owner). Un anchor dedicado sin estado resuelve eso.
		void Show();

	private:
		// Construye el menu raiz (incluyendo submenu Apagar) usando
		// CreatePopupMenu/AppendMenu. Devuelve el HMENU que el caller
		// debe destruir con DestroyMenu.
		HMENU BuildMenu();

		// Ejecuta el comando elegido. Mapea IDs a binarios nativos Win7.
		// Las acciones son todas ShellExecute/CreateProcess: no hay
		// estado persistente entre llamadas.
		void DispatchCommand(int cmd);

		// Helpers para acciones especificas:
		void LaunchCpl(LPCWSTR cpl);                  // appwiz.cpl, etc.
		void LaunchMsc(LPCWSTR msc);                  // eventvwr.msc, etc.
		void LaunchShellProcess(LPCWSTR cmdLine);     // explorer.exe, powershell.exe, ...
		void LaunchPowerShell(BOOL elevated);
		void RunDialog();                            // menu Ejecutar
		void ShowDesktopToggle();
		void LockWorkstation();
		void SignOut();
		void Suspend();
		void Shutdown(BOOL reboot);

		// IDs de menu. Deben permanecer estables dentro del binario.
		enum Cmd : int {
			// Bloque 1 (sin separador)
			CMD_APPS_FEATURES   = 1001,
			CMD_POWER_OPTIONS   = 1002,
			CMD_EVENT_VIEWER    = 1003,
			CMD_SYSTEM          = 1004,
			CMD_DEVICE_MGR      = 1005,
			CMD_NET_CONNECTIONS = 1006,
			CMD_DISK_MGMT       = 1007,
			CMD_COMPUTER_MGT    = 1008,
			CMD_POWERSHELL      = 1009,
			CMD_POWERSHELL_ADM  = 1010,
			// Separador + Bloque 2
			CMD_TASKMGR         = 1101,
			CMD_CONTROL_PANEL   = 1102,
			CMD_EXPLORER        = 1103,
			CMD_SEARCH          = 1104,
			CMD_RUN             = 1105,
			// Separador + Bloque 3
			CMD_POWEROFF_LOCK   = 1201,  // submenu
			CMD_DESKTOP         = 1202,
			// IDs del submenu Apagar
			CMD_PM_LOCK         = 2001,
			CMD_PM_SIGNOUT      = 2002,
			CMD_PM_SUSPEND      = 2003,
			CMD_PM_RESTART      = 2004,
			CMD_PM_SHUTDOWN     = 2005,
		};

		// Top-level invisible que se usa como owner de los popups y
		// como destino de SetForegroundWindow. Se crea en Show() la
		// primera vez y se destruye en el destructor. Ver nota en
		// Show() sobre por que no usamos la FlyoutWindow.
		HWND m_hAnchor = nullptr;
	};

} // namespace AeroClip::UI

#endif // AEROCLIP_UI_WINXMENU_H
