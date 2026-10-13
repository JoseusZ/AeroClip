// AeroClip - Auto-start via HKCU\...\Run.
//
// Writes the current executable path under
//   HKCU\Software\Microsoft\Windows\CurrentVersion\Run
// so that the entry shows up in msconfig ("Inicio de Windows" tab) and in
// the Control Panel "Startup" UI. From there the user can enable or
// disable AeroClip at will, just like any other startup entry.
//
// The contract enforced here is the standard Windows one:
//   - First launch: register "AeroClip" pointing at the current .exe path.
//   - Subsequent launches: if the entry already exists, do nothing.
//   - If the user disables the entry through msconfig, Windows renames the
//     value to "AeroClip_disabled". We treat that as a deliberate user
//     choice and we do NOT re-create the enabled "AeroClip" entry on the
//     next launch. To re-enable, the user goes back to msconfig.
//   - Unregister() removes both forms (the enabled and the disabled one).

#ifndef AEROCLIP_UTILS_AUTOSTART_H
#define AEROCLIP_UTILS_AUTOSTART_H

#include <windows.h>

namespace AeroClip::Utils {

	class AutoStart {
	public:
		// Registry path constants. Exposed so the host can clean up on
		// uninstall or expose toggle UI in the future.
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

		// First-run registration. Behaviour:
		//   - If neither "AeroClip" nor "AeroClip_disabled" exists,
		//     creates "AeroClip" with the current executable path and
		//     returns TRUE.
		//   - If either form already exists, does NOT touch the registry
		//     and returns TRUE (the user's choice is respected, including
		//     a previous disable through msconfig).
		//   - Returns FALSE only on a genuine registry failure.
		static BOOL RegisterOnFirstRun();

		// Removes both "AeroClip" and "AeroClip_disabled" from HKCU\Run.
		// Returns TRUE if neither form exists after the call.
		static BOOL Unregister();

	private:
		// Reads the value that currently lives in the registry, or returns
		// an empty string if no value is present. outDisabled is set to
		// TRUE if the value is the "AeroClip_disabled" form.
		static BOOL ReadCurrentValue(wchar_t* outPath, DWORD cchPath,
									 BOOL* outDisabled);
	};

} // namespace AeroClip::Utils

#endif // AEROCLIP_UTILS_AUTOSTART_H
