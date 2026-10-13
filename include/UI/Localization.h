// AeroClip - Sistema de localizacion.
// Detecta automaticamente el idioma del sistema operativo (ingles o
// espanol) y expone funciones Loc*() que devuelven la cadena adecuada.
// El idioma se evalua UNA sola vez en el primer acceso (magic static,
// thread-safe en C++11) usando GetUserDefaultUILanguage().

#pragma once

#include <windows.h>
#include <wchar.h>
#include <string>

namespace AeroClip::UI {

	enum class Language {
		English,
		Spanish
	};

	// Detecta el idioma de la UI del sistema. Consideramos espanol
	// cualquier idioma cuyo PRIMARYLANGID sea LANG_SPANISH (0x0A),
	// independientemente del sublan (es-ES, es-MX, es-AR, etc.).
	// El resto cae a ingles como idioma por defecto.
	inline Language DetectSystemLanguage() {
		LANGID lid = ::GetUserDefaultUILanguage();
		return (PRIMARYLANGID(lid) == LANG_SPANISH)
			? Language::Spanish
			: Language::English;
	}

	// Cachea el idioma detectado en el primer uso.
	inline Language CurrentLanguage() {
		static const Language lang = DetectSystemLanguage();
		return lang;
	}

	inline const wchar_t* LocAppName() {
		return L"AeroClip";
	}

	// Titulo de la ventana del flyout (visible al usuario).
	inline const wchar_t* LocFlyoutTitle() {
		return CurrentLanguage() == Language::Spanish
			? L"Portapapeles"
			: L"Clipboard";
	}

	// Items del menu contextual.
	inline const wchar_t* LocMenuDelete() {
		return CurrentLanguage() == Language::Spanish
			? L"Eliminar"
			: L"Delete";
	}

	inline const wchar_t* LocMenuPin() {
		return CurrentLanguage() == Language::Spanish
			? L"Anclar"
			: L"Pin";
	}

	inline const wchar_t* LocMenuUnpin() {
		return CurrentLanguage() == Language::Spanish
			? L"Desanclar"
			: L"Unpin";
	}

	inline const wchar_t* LocMenuClearAll() {
		return CurrentLanguage() == Language::Spanish
			? L"Borrar todo"
			: L"Clear all";
	}

	// Mensaje del estado vacio (se renderiza con DT_WORDBREAK, acepta \n).
	inline const wchar_t* LocEmptyState() {
		return CurrentLanguage() == Language::Spanish
			? L"Tu portapapeles est\xe1 vac\xedo.\nCopia algo para empezar."
			: L"Your clipboard is empty.\nCopy something to get started.";
	}

	// Titulo del balloon/toast cuando se excede kMaxPinned.
	inline const wchar_t* LocPinLimitTitle() {
		return CurrentLanguage() == Language::Spanish
			? L"AeroClip \x2014 L\xedmite de anclados"
			: L"AeroClip \x2014 Pin limit";
	}

	// Mensaje del balloon/toast cuando se excede kMaxPinned.
	// Se formatea con el maximo real para que no quede desincronizado
	// si kMaxPinned cambia.
	inline std::wstring LocPinLimitMessage(int maxPinned) {
		wchar_t buf[256];
		if (CurrentLanguage() == Language::Spanish) {
			swprintf_s(buf,
				L"Solo puede fijar %d elementos. Desancla uno para anclar otro.",
				maxPinned);
		} else {
			swprintf_s(buf,
				L"You can only pin %d items. Unpin one to pin another.",
				maxPinned);
		}
		return buf;
	}

	// --- Items del menu Win+X -----------------------------------------
	// Bloque 1
	inline const wchar_t* LocWinXApps()        { return CurrentLanguage() == Language::Spanish ? L"Aplicaciones y caracter\xedsticas"  : L"Apps & Features"; }
	inline const wchar_t* LocWinXPower()       { return CurrentLanguage() == Language::Spanish ? L"Opciones de energ\u00eda"             : L"Power Options"; }
	inline const wchar_t* LocWinXEventVw()     { return CurrentLanguage() == Language::Spanish ? L"Visor de eventos"                  : L"Event Viewer"; }
	inline const wchar_t* LocWinXSystem()      { return CurrentLanguage() == Language::Spanish ? L"Sistema"                           : L"System"; }
	inline const wchar_t* LocWinXDevMgr()      { return CurrentLanguage() == Language::Spanish ? L"Administrador de dispositivos"     : L"Device Manager"; }
	inline const wchar_t* LocWinXNet()         { return CurrentLanguage() == Language::Spanish ? L"Conexiones de red"                 : L"Network Connections"; }
	inline const wchar_t* LocWinXDiskMgmt()    { return CurrentLanguage() == Language::Spanish ? L"Administraci\xf3n de discos"      : L"Disk Management"; }
	inline const wchar_t* LocWinXCompMgmt()    { return CurrentLanguage() == Language::Spanish ? L"Administraci\xf3n de equipos"     : L"Computer Management"; }
	inline const wchar_t* LocWinXPowershell()  { return CurrentLanguage() == Language::Spanish ? L"Windows PowerShell"                : L"Windows PowerShell"; }
	inline const wchar_t* LocWinXPowershellA() { return CurrentLanguage() == Language::Spanish ? L"Windows PowerShell (Administrador)" : L"Windows PowerShell (Admin)"; }
	// Bloque 2
	inline const wchar_t* LocWinXTaskMgr()     { return CurrentLanguage() == Language::Spanish ? L"Administrador de tareas"           : L"Task Manager"; }
	inline const wchar_t* LocWinXControl()     { return CurrentLanguage() == Language::Spanish ? L"Panel de control"                  : L"Control Panel"; }
	inline const wchar_t* LocWinXExplorer()    { return CurrentLanguage() == Language::Spanish ? L"Explorador de archivos"            : L"File Explorer"; }
	inline const wchar_t* LocWinXSearch()      { return CurrentLanguage() == Language::Spanish ? L"Buscar"                            : L"Search"; }
	inline const wchar_t* LocWinXRun()         { return CurrentLanguage() == Language::Spanish ? L"Ejecutar"                          : L"Run"; }
	// Bloque 3 + submenu Apagar
	inline const wchar_t* LocWinXShutDownSub() { return CurrentLanguage() == Language::Spanish ? L"Apagar o cerrar sesi\xf3n"        : L"Shut down or sign out"; }
	inline const wchar_t* LocWinXDesktop()     { return CurrentLanguage() == Language::Spanish ? L"Escritorio"                        : L"Desktop"; }
	inline const wchar_t* LocWinXLock()        { return CurrentLanguage() == Language::Spanish ? L"Bloquear"                          : L"Lock"; }
	inline const wchar_t* LocWinXSignOut()     { return CurrentLanguage() == Language::Spanish ? L"Cerrar sesi\xf3n"                  : L"Sign out"; }
	inline const wchar_t* LocWinXSleep()       { return CurrentLanguage() == Language::Spanish ? L"Suspender"                         : L"Sleep"; }
	inline const wchar_t* LocWinXRestart()     { return CurrentLanguage() == Language::Spanish ? L"Reiniciar"                         : L"Restart"; }
	inline const wchar_t* LocWinXShutDown()    { return CurrentLanguage() == Language::Spanish ? L"Apagar"                            : L"Shut down"; }

} // namespace AeroClip::UI
