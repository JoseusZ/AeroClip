// AeroClip - Auto-start via HKCU\...\Run (implementation).
//
// All operations are against HKEY_CURRENT_USER, so no admin rights are
// required and no UAC prompt is shown. The path written is whatever
// GetModuleFileNameW(NULL, ...) returns for the running process, which
// in our case is the fully qualified path of AeroClip.exe as launched
// by the user (Explorer, a shortcut, the command line, etc.).

#include "Utils/AutoStart.h"
#include <strsafe.h>

namespace AeroClip::Utils {

	BOOL AutoStart::ReadCurrentValue(wchar_t* outPath, DWORD cchPath,
									 BOOL* outDisabled) {
		if (outPath && cchPath > 0) outPath[0] = 0;
		if (outDisabled) *outDisabled = FALSE;

		HKEY hKey = nullptr;
		LSTATUS status = ::RegOpenKeyExW(
			HKEY_CURRENT_USER, kRunKeyPath, 0,
			KEY_QUERY_VALUE | KEY_SET_VALUE, &hKey);
		if (status != ERROR_SUCCESS) {
			// The Run key itself does not exist yet (very unusual, but
			// possible on a stripped-down profile). Treat as "no entry".
			if (status == ERROR_FILE_NOT_FOUND) return FALSE;
			return FALSE;
		}

		// Try the enabled name first.
		DWORD cbData = cchPath * sizeof(wchar_t);
		DWORD type = 0;
		status = ::RegQueryValueExW(hKey, kValueName, nullptr, &type,
									reinterpret_cast<LPBYTE>(outPath), &cbData);
		if (status == ERROR_SUCCESS && type == REG_SZ) {
			::RegCloseKey(hKey);
			if (outDisabled) *outDisabled = FALSE;
			return TRUE;
		}

		// Try the disabled form: "AeroClip_disabled".
		wchar_t disabledName[64];
		::StringCchCopyW(disabledName, _countof(disabledName), kValueName);
		::StringCchCatW(disabledName, _countof(disabledName), kDisabledSuffix);

		cbData = cchPath * sizeof(wchar_t);
		status = ::RegQueryValueExW(hKey, disabledName, nullptr, &type,
									reinterpret_cast<LPBYTE>(outPath), &cbData);
		::RegCloseKey(hKey);
		if (status == ERROR_SUCCESS && type == REG_SZ) {
			if (outDisabled) *outDisabled = TRUE;
			return TRUE;
		}
		return FALSE;
	}

	BOOL AutoStart::IsEnabled() {
		wchar_t buf[MAX_PATH] = {};
		BOOL disabled = FALSE;
		if (!ReadCurrentValue(buf, _countof(buf), &disabled)) return FALSE;
		return !disabled;
	}

	BOOL AutoStart::IsDisabled() {
		wchar_t buf[MAX_PATH] = {};
		BOOL disabled = FALSE;
		if (!ReadCurrentValue(buf, _countof(buf), &disabled)) return FALSE;
		return disabled;
	}

	BOOL AutoStart::Exists() {
		wchar_t buf[MAX_PATH] = {};
		BOOL disabled = FALSE;
		return ReadCurrentValue(buf, _countof(buf), &disabled);
	}

	BOOL AutoStart::RegisterOnFirstRun() {
		// Resolve the current executable path.
		wchar_t exePath[MAX_PATH] = {};
		DWORD cch = ::GetModuleFileNameW(nullptr, exePath, _countof(exePath));
		if (cch == 0 || cch >= _countof(exePath)) {
			return FALSE;
		}

		// If an entry (enabled OR disabled) already exists, respect it.
		// We never re-create an enabled entry when the user has chosen
		// to keep it disabled, and we never flip a disabled entry back
		// to enabled on our own.
		wchar_t existing[MAX_PATH] = {};
		BOOL existingDisabled = FALSE;
		if (ReadCurrentValue(existing, _countof(existing), &existingDisabled)) {
			return TRUE;
		}

		// No entry at all. Open the key for write and create "AeroClip".
		HKEY hKey = nullptr;
		LSTATUS status = ::RegOpenKeyExW(
			HKEY_CURRENT_USER, kRunKeyPath, 0,
			KEY_QUERY_VALUE | KEY_SET_VALUE, &hKey);
		if (status != ERROR_SUCCESS) {
			// The Run subkey may not exist in a brand-new profile. Try
			// to create it on the fly.
			status = ::RegCreateKeyExW(
				HKEY_CURRENT_USER, kRunKeyPath, 0, nullptr,
				REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
				&hKey, nullptr);
			if (status != ERROR_SUCCESS) return FALSE;
		}

		DWORD cbBytes = (::lstrlenW(exePath) + 1) * sizeof(wchar_t);
		status = ::RegSetValueExW(
			hKey, kValueName, 0, REG_SZ,
			reinterpret_cast<const BYTE*>(exePath), cbBytes);
		::RegCloseKey(hKey);
		return (status == ERROR_SUCCESS);
	}

	BOOL AutoStart::Unregister() {
		HKEY hKey = nullptr;
		LSTATUS status = ::RegOpenKeyExW(
			HKEY_CURRENT_USER, kRunKeyPath, 0,
			KEY_QUERY_VALUE | KEY_SET_VALUE, &hKey);
		if (status != ERROR_SUCCESS) {
			return (status == ERROR_FILE_NOT_FOUND);
		}

		wchar_t disabledName[64];
		::StringCchCopyW(disabledName, _countof(disabledName), kValueName);
		::StringCchCatW(disabledName, _countof(disabledName), kDisabledSuffix);

		::RegDeleteValueW(hKey, kValueName);
		::RegDeleteValueW(hKey, disabledName);
		::RegCloseKey(hKey);
		return TRUE;
	}

} // namespace AeroClip::Utils
