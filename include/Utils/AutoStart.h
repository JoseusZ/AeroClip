#ifndef AEROCLIP_UTILS_AUTOSTART_H
#define AEROCLIP_UTILS_AUTOSTART_H

#include <windows.h>

namespace AeroClip::Utils {

	class AutoStart {
	public:

		static constexpr const wchar_t* kRunKeyPath =
			L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
		static constexpr const wchar_t* kValueName       = L"AeroClip";
		static constexpr const wchar_t* kDisabledSuffix = L"_disabled";

		// Returns TRUE if a "AeroClip" (enabled) value exists in HKCU\Run.
		static BOOL IsEnabled();

		// Returns TRUE if an "AeroClip_disabled" value exists in HKCU\Run.
		static BOOL IsDisabled();

		// Returns TRUE if either the enabled or the disabled form exists.
		static BOOL Exists();
		static BOOL RegisterOnFirstRun();

		// Removes both "AeroClip" and "AeroClip_disabled" from HKCU\Run.
		// Returns TRUE if neither form exists after the call.
		static BOOL Unregister();

	private:
		// Reads the value that currently lives in the registry
		static BOOL ReadCurrentValue(wchar_t* outPath, DWORD cchPath,
									 BOOL* outDisabled);
	};

} // namespace AeroClip::Utils

#endif // AEROCLIP_UTILS_AUTOSTART_H
