#ifndef AEROCLIP_CLIPBOARD_CLIPBOARDMONITOR_H
#define AEROCLIP_CLIPBOARD_CLIPBOARDMONITOR_H

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace AeroClip::Storage { class HistoryBuffer; }

namespace AeroClip::Clipboard {

    class ClipboardMonitor {
    public:
        ClipboardMonitor();
        ~ClipboardMonitor();

        BOOL Start(Storage::HistoryBuffer& buffer, HWND flyoutHwnd);
        void Stop();
        void SetTargetHwnd(HWND h) { m_flyoutHwnd = h; }

    private:
        Storage::HistoryBuffer* m_buffer = nullptr;
        HWND                    m_flyoutHwnd = nullptr;
        HWND                    m_listenerHwnd = nullptr;
        HANDLE                  m_thread = nullptr;
        DWORD                   m_threadId = 0;
        volatile BOOL           m_running = FALSE;

        static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
        static DWORD WINAPI      ThreadProc(LPVOID self);

        void OnClipboardUpdate();
        void ProcessCurrent();
    };

} // namespace AeroClip::Clipboard

#endif // AEROCLIP_CLIPBOARD_CLIPBOARDMONITOR_H