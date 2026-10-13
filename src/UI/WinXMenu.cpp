#include "UI/WinXMenu.h"
#include "UI/Localization.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>

#pragma comment(lib, "shell32.lib")

namespace AeroClip::UI {


	void WinXMenu::LaunchCpl(LPCWSTR cpl) {

		::ShellExecuteW(nullptr, nullptr, cpl, nullptr, nullptr,
						SW_SHOWNORMAL);
	}

	void WinXMenu::LaunchMsc(LPCWSTR msc) {

		::ShellExecuteW(nullptr, nullptr, msc, nullptr, nullptr,
						SW_SHOWNORMAL);
	}

	void WinXMenu::LaunchShellProcess(LPCWSTR cmdLine) {
		STARTUPINFOW si = { sizeof(si) };
		PROCESS_INFORMATION pi = {};
		// CreateProcess needs a mutable copy of the command line; uses a local buffer.

		wchar_t buf[MAX_PATH * 2] = {};
		wcsncpy_s(buf, cmdLine, _TRUNCATE);
		::CreateProcessW(nullptr, buf, nullptr, nullptr, FALSE,
						 0, nullptr, nullptr, &si, &pi);
		if (pi.hProcess) ::CloseHandle(pi.hProcess);
		if (pi.hThread)  ::CloseHandle(pi.hThread);
	}

	void WinXMenu::LaunchPowerShell(BOOL elevated) {
		// ShellExecuteEx with lpVerb = L"runas" triggers the UAC prompt

		SHELLEXECUTEINFOW sei = { sizeof(sei) };
		sei.fMask = SEE_MASK_NOCLOSEPROCESS;
		sei.lpFile = L"powershell.exe";
		sei.nShow  = SW_SHOWNORMAL;
		if (elevated) sei.lpVerb = L"runas";
		::ShellExecuteExW(&sei);
		if (sei.hProcess) ::CloseHandle(sei.hProcess);
	}

	void WinXMenu::RunDialog() {

		::ShellExecuteW(nullptr, nullptr,
						L"rundll32.exe",
						L"shell32.dll,#61",
						nullptr, SW_SHOWNORMAL);
	}

	void WinXMenu::ShowDesktopToggle() {

		::ShellExecuteW(nullptr, nullptr,
						L"rundll32.exe",
						L"user32.dll,#12",
						nullptr, SW_SHOWNORMAL);
	}

	void WinXMenu::LockWorkstation() {
		// user32!LockWorkStation is documented API.
		::LockWorkStation();
	}

	void WinXMenu::SignOut() {

		::ExitWindowsEx(EWX_LOGOFF, 0);
	}

	void WinXMenu::Suspend() {
		// powrprof!SetSuspendState(0, 1, 0)
		::ShellExecuteW(nullptr, nullptr,
						L"rundll32.exe",
						L"powrprof.dll,SetSuspendState 0,1,0",
						nullptr, SW_SHOWNORMAL);
	}

	void WinXMenu::Shutdown(BOOL reboot) {
		::ShellExecuteW(nullptr, nullptr,
						reboot ? L"shutdown.exe" : L"shutdown.exe",
						reboot ? L"/r /t 0" : L"/s /t 0",
						nullptr, SW_HIDE);
	}

	void WinXMenu::DispatchCommand(int cmd) {
		switch (cmd) {
		case CMD_APPS_FEATURES:   LaunchCpl(L"appwiz.cpl");   break;
		case CMD_POWER_OPTIONS:   LaunchCpl(L"powercfg.cpl"); break;
		case CMD_EVENT_VIEWER:    LaunchMsc(L"eventvwr.msc"); break;
		case CMD_SYSTEM:

			LaunchCpl(L"sysdm.cpl");
			break;
		case CMD_DEVICE_MGR:      LaunchMsc(L"devmgmt.msc");   break;
		case CMD_NET_CONNECTIONS: LaunchCpl(L"ncpa.cpl");     break;
		case CMD_DISK_MGMT:       LaunchMsc(L"diskmgmt.msc"); break;
		case CMD_COMPUTER_MGT:    LaunchMsc(L"compmgmt.msc"); break;
		case CMD_POWERSHELL:      LaunchPowerShell(FALSE);    break;
		case CMD_POWERSHELL_ADM:  LaunchPowerShell(TRUE);     break;
		case CMD_TASKMGR:         LaunchShellProcess(L"taskmgr.exe"); break;
		case CMD_CONTROL_PANEL:

			LaunchShellProcess(L"control.exe");
			break;
		case CMD_EXPLORER:
			LaunchShellProcess(L"explorer.exe");
			break;
		case CMD_SEARCH:
			// search-ms: is the native search URI handler
			::ShellExecuteW(nullptr, L"open", L"search-ms:", nullptr,
							nullptr, SW_SHOWNORMAL);
			break;
		case CMD_RUN:             RunDialog();              break;
		case CMD_PM_LOCK:         LockWorkstation();        break;
		case CMD_PM_SIGNOUT:      SignOut();                break;
		case CMD_PM_SUSPEND:      Suspend();                break;
		case CMD_PM_RESTART:      Shutdown(TRUE);           break;
		case CMD_PM_SHUTDOWN:     Shutdown(FALSE);          break;
		case CMD_DESKTOP:         ShowDesktopToggle();      break;
		default: break;
		}
	}

	HMENU WinXMenu::BuildMenu() {
		HMENU hRoot = ::CreatePopupMenu();
		if (!hRoot) return nullptr;

		// Block 1: 10 items + separators
		::AppendMenuW(hRoot, MF_STRING,    CMD_APPS_FEATURES,   LocWinXApps());
		::AppendMenuW(hRoot, MF_STRING,    CMD_POWER_OPTIONS,   LocWinXPower());
		::AppendMenuW(hRoot, MF_STRING,    CMD_EVENT_VIEWER,    LocWinXEventVw());
		::AppendMenuW(hRoot, MF_STRING,    CMD_SYSTEM,          LocWinXSystem());
		::AppendMenuW(hRoot, MF_STRING,    CMD_DEVICE_MGR,      LocWinXDevMgr());
		::AppendMenuW(hRoot, MF_STRING,    CMD_NET_CONNECTIONS, LocWinXNet());
		::AppendMenuW(hRoot, MF_STRING,    CMD_DISK_MGMT,       LocWinXDiskMgmt());
		::AppendMenuW(hRoot, MF_STRING,    CMD_COMPUTER_MGT,    LocWinXCompMgmt());
		::AppendMenuW(hRoot, MF_STRING,    CMD_POWERSHELL,      LocWinXPowershell());
		::AppendMenuW(hRoot, MF_STRING,    CMD_POWERSHELL_ADM,  LocWinXPowershellA());
		::AppendMenuW(hRoot, MF_SEPARATOR, 0, nullptr);

		// items 
		::AppendMenuW(hRoot, MF_STRING,    CMD_TASKMGR,         LocWinXTaskMgr());
		::AppendMenuW(hRoot, MF_STRING,    CMD_CONTROL_PANEL,   LocWinXControl());
		::AppendMenuW(hRoot, MF_STRING,    CMD_EXPLORER,        LocWinXExplorer());
		::AppendMenuW(hRoot, MF_STRING,    CMD_SEARCH,          LocWinXSearch());
		::AppendMenuW(hRoot, MF_STRING,    CMD_RUN,             LocWinXRun());
		::AppendMenuW(hRoot, MF_SEPARATOR, 0, nullptr);

		// submenu Shut down + item Desktop
		HMENU hPower = ::CreatePopupMenu();
		if (hPower) {

			::AppendMenuW(hPower, MF_STRING, CMD_PM_LOCK,     LocWinXLock());
			::AppendMenuW(hPower, MF_STRING, CMD_PM_SIGNOUT,  LocWinXSignOut());
			::AppendMenuW(hPower, MF_STRING, CMD_PM_SUSPEND,  LocWinXSleep());
			::AppendMenuW(hPower, MF_STRING, CMD_PM_RESTART,  LocWinXRestart());
			::AppendMenuW(hPower, MF_STRING, CMD_PM_SHUTDOWN, LocWinXShutDown());
			::AppendMenuW(hRoot, MF_POPUP | MF_STRING,
						  (UINT_PTR)hPower,
						  LocWinXShutDownSub());
		}
		::AppendMenuW(hRoot, MF_STRING,    CMD_DESKTOP,         LocWinXDesktop());

		return hRoot;
	}


	void WinXMenu::Show() {

		if (!m_hAnchor) {
			static const wchar_t* kClass = L"AeroClipWinXAnchor";
			WNDCLASSEX wc = { sizeof(wc) };
			wc.lpfnWndProc   = ::DefWindowProc;
			wc.hInstance     = ::GetModuleHandle(nullptr);
			wc.lpszClassName = kClass;
			if (!::RegisterClassEx(&wc)
				&& ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
				return;
			}
			m_hAnchor = ::CreateWindowEx(
				WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
				kClass, L"",
				WS_OVERLAPPED,
				0, 0, 0, 0,
				HWND_DESKTOP,
				nullptr, wc.hInstance, nullptr);
			if (m_hAnchor) {

				::SetLayeredWindowAttributes(m_hAnchor, 0, 0, LWA_ALPHA);
			}
		}
		if (!m_hAnchor) return;

		HMENU hMenu = BuildMenu();
		if (!hMenu) return;

		::AllowSetForegroundWindow(::GetCurrentProcessId());
		::SetForegroundWindow(m_hAnchor);

		// Position
		RECT workArea = {};
		::SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
		POINT pt;
		pt.x = workArea.left;
		pt.y = workArea.bottom - 1;

		// TrackPopupMenuEx with m_hAnchor as owner
		int cmd = ::TrackPopupMenuEx(hMenu,
									 TPM_RIGHTBUTTON | TPM_RETURNCMD |
									 TPM_NONOTIFY | TPM_VERPOSANIMATION |
									 TPM_BOTTOMALIGN | TPM_LEFTALIGN,
									 pt.x, pt.y,
									 m_hAnchor, nullptr);

		::PostMessage(m_hAnchor, WM_NULL, 0, 0);

		::DestroyMenu(hMenu);

		// If the user canceled (Escape, click away) cmd == 0.
		if (cmd != 0) {
			DispatchCommand(cmd);
		}
	}

	// Release the anchor window if it was created.
	WinXMenu::~WinXMenu() {
		if (m_hAnchor) {
			::DestroyWindow(m_hAnchor);
			m_hAnchor = nullptr;
		}
	}

} // namespace AeroClip::UI
