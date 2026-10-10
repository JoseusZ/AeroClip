// =============================================================================
// AeroClip - Version info (single source of truth).
// Edita SOLO este archivo para actualizar la version del ejecutable.
// Los macros se usan desde el recurso .rc (VS_VERSION_INFO) y desde C++,
// de modo que el "About" del EXE y cualquier mensaje en log/UI quedan
// sincronizados con la version que compilas.
// =============================================================================
#pragma once

// --- Numero de version (cambia aqui y solo aqui) -----------------------------
#define AEROCLIP_VERSION_MAJOR    1
#define AEROCLIP_VERSION_MINOR    0
#define AEROCLIP_VERSION_PATCH    0
#define AEROCLIP_VERSION_BUILD    0

// --- Tuplas numericas para FILEVERSION / PRODUCTVERSION (4 WORDs) ------------
#define AEROCLIP_FILEVERSION_NUM \
	AEROCLIP_VERSION_MAJOR,AEROCLIP_VERSION_MINOR,AEROCLIP_VERSION_PATCH,AEROCLIP_VERSION_BUILD

#define AEROCLIP_PRODUCTVERSION_NUM \
	AEROCLIP_VERSION_MAJOR,AEROCLIP_VERSION_MINOR,AEROCLIP_VERSION_PATCH,AEROCLIP_VERSION_BUILD

// --- Stringification: "MAJOR.MINOR.PATCH.BUILD" ------------------------------
#define AEROCLIP_STRINGIFY_INNER(x) #x
#define AEROCLIP_STRINGIFY(x)        AEROCLIP_STRINGIFY_INNER(x)

#define AEROCLIP_VERSION_STRING_ \
	AEROCLIP_STRINGIFY(AEROCLIP_VERSION_MAJOR) "." \
	AEROCLIP_STRINGIFY(AEROCLIP_VERSION_MINOR) "." \
	AEROCLIP_STRINGIFY(AEROCLIP_VERSION_PATCH) "." \
	AEROCLIP_STRINGIFY(AEROCLIP_VERSION_BUILD)

// --- Metadatos de la aplicacion (editable) -----------------------------------
#define AEROCLIP_COMPANY_NAME        "JoseusZ"
#define AEROCLIP_PRODUCT_NAME        "AeroClip"
#define AEROCLIP_FILE_DESCRIPTION    "Historial del portapapeles para Windows 7"
#define AEROCLIP_INTERNAL_NAME       "AeroClip"
#define AEROCLIP_ORIGINAL_FILENAME   "AeroClip.exe"
#define AEROCLIP_LEGAL_COPYRIGHT     "Copyright (c) 2026 JoseusZ. Todos los derechos reservados."

// --- Acceso desde C++ --------------------------------------------------------
#include <string>

namespace AeroClip {

	struct Version {
		int major;
		int minor;
		int patch;
		int build;
	};

	constexpr Version kVersion = {
		AEROCLIP_VERSION_MAJOR,
		AEROCLIP_VERSION_MINOR,
		AEROCLIP_VERSION_PATCH,
		AEROCLIP_VERSION_BUILD
	};

	// Cadena ANSI "1.0.0.0" (util para logs/consola).
	inline const char* VersionStringA() {
		return AEROCLIP_VERSION_STRING_;
	}

	// Cadena wide L"1.0.0.0" (util para MessageBoxW, titulos, etc).
	inline const wchar_t* VersionStringW() {
		return L"" AEROCLIP_VERSION_STRING_;
	}

	// Version completa formateada, p.ej. "AeroClip 1.0.0.0".
	inline std::wstring FullVersionW() {
		return std::wstring(L"" AEROCLIP_PRODUCT_NAME) + L" " + VersionStringW();
	}

} // namespace AeroClip
