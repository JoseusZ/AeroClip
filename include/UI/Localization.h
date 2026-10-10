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

} // namespace AeroClip::UI
