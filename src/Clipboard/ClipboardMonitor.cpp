#include "Clipboard/ClipboardMonitor.h"
#include "Clipboard/DataConverter.h"
#include "Storage/HistoryBuffer.h"

namespace AeroClip::Clipboard {

    static const wchar_t* kClassName = L"AeroClip_ClipboardListener";

    ClipboardMonitor::ClipboardMonitor() = default;

    ClipboardMonitor::~ClipboardMonitor() {
        Stop();
    }

    BOOL ClipboardMonitor::Start(Storage::HistoryBuffer& buffer, HWND flyoutHwnd) {
        if (m_running) return TRUE;

        m_buffer = &buffer;
        m_flyoutHwnd = flyoutHwnd;
        m_running = TRUE;

        m_thread = ::CreateThread(nullptr, 0, &ClipboardMonitor::ThreadProc,
            this, 0, &m_threadId);
        if (!m_thread) {
            m_running = FALSE;
            return FALSE;
        }

        ULONGLONG startMs = ::GetTickCount64();
        while (!m_listenerHwnd && (::GetTickCount64() - startMs < 2000)) {
            ::Sleep(10);
        }

        return (m_listenerHwnd != nullptr);
    }

    void ClipboardMonitor::Stop() {
        if (!m_running) return;
        m_running = FALSE;

        if (m_listenerHwnd) {
            ::PostMessage(m_listenerHwnd, WM_QUIT, 0, 0);
        }

        if (m_thread) {
            ::WaitForSingleObject(m_thread, 3000);
            ::CloseHandle(m_thread);
            m_thread = nullptr;
        }

        m_listenerHwnd = nullptr;
        m_buffer = nullptr;
    }

    DWORD WINAPI ClipboardMonitor::ThreadProc(LPVOID selfPtr) {
        auto* self = static_cast<ClipboardMonitor*>(selfPtr);

        WNDCLASSEX wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &ClipboardMonitor::WndProc;
        wc.hInstance = ::GetModuleHandle(nullptr);
        wc.lpszClassName = kClassName;

        if (!::RegisterClassEx(&wc)) {
            DWORD e = ::GetLastError();
            if (e != ERROR_CLASS_ALREADY_EXISTS) return 1;
        }

        self->m_listenerHwnd = ::CreateWindowEx(
            0, kClassName, L"", 0, 0, 0, 0, 0,
            HWND_MESSAGE, nullptr, wc.hInstance, nullptr);

        if (!self->m_listenerHwnd) return 2;

        ::SetWindowLongPtr(self->m_listenerHwnd, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(self));

        ::AddClipboardFormatListener(self->m_listenerHwnd);

        MSG msg;
        while (self->m_running && ::GetMessage(&msg, nullptr, 0, 0) > 0) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }

        ::RemoveClipboardFormatListener(self->m_listenerHwnd);
        ::DestroyWindow(self->m_listenerHwnd);
        self->m_listenerHwnd = nullptr;
        return 0;
    }

    LRESULT CALLBACK ClipboardMonitor::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<ClipboardMonitor*>(
            ::GetWindowLongPtr(hwnd, GWLP_USERDATA));

        if (msg == WM_CLIPBOARDUPDATE && self) {
            self->OnClipboardUpdate();
            return 0;
        }
        return ::DefWindowProc(hwnd, msg, wp, lp);
    }

    static ULONGLONG s_lastSuccessTick = 0;

        void ClipboardMonitor::OnClipboardUpdate() {
            if (!m_buffer) return;

            // Debounce: si el ultimo add exitoso fue hace < 10 ms, suprime. Solo lo
                    // justo para coalescer los 2-3 WM_CLIPBOARDUPDATE que dispara Windows
                    // por un unico cambio de clipboard. NO contar ADDs fallidos (paste-back
                    // propio filtrado por isDuplicate) para no suprimir capturas legitimas.
                    ULONGLONG now = ::GetTickCount64();
                    if (now - s_lastSuccessTick < 10) return;

            if (ProcessCurrent()) {
                s_lastSuccessTick = now;
            }
        }

        BOOL ClipboardMonitor::ProcessCurrent() {
            BOOL added = FALSE;

            if (::IsClipboardFormatAvailable(CF_UNICODETEXT)) {
                LPCVOID pData = nullptr;
                DWORD cb = 0;
                if (DataConverter::ReadUnicodeText(&pData, &cb) && pData && cb > 0) {
                    if (m_buffer->Add(ItemKind::Text, pData, cb) == 0) {
                        if (m_flyoutHwnd && ::IsWindow(m_flyoutHwnd)) {
                            ::PostMessage(m_flyoutHwnd, MSG_CLIPBOARD_UPDATED, 0, 0);
                        }
                        added = TRUE;
                    }
                    ::SecureZeroMemory(const_cast<LPVOID>(pData), cb);
                    ::HeapFree(::GetProcessHeap(), 0, const_cast<LPVOID>(pData));
                }
                return added;
            }

            // Imagen: intentar primero CF_DIB (PasteImage de AeroClip, Snip &
            // Sketch, etc.); si no esta disponible, fallback a CF_BITMAP (Print
            // Screen de Windows 10/11 pone la mayoria de veces solo CF_BITMAP).
            LPCVOID pData = nullptr;
            DWORD cb = 0;
            BOOL got = DataConverter::ReadDibImage(&pData, &cb);
            if (!got) {
                got = DataConverter::ReadBitmapAsDIB(&pData, &cb);
            }
            if (got && pData && cb > 0) {
                if (m_buffer->Add(ItemKind::Image, pData, cb) == 0) {
                    if (m_flyoutHwnd && ::IsWindow(m_flyoutHwnd)) {
                        ::PostMessage(m_flyoutHwnd, MSG_CLIPBOARD_UPDATED, 0, 0);
                    }
                    added = TRUE;
                }
                ::SecureZeroMemory(const_cast<LPVOID>(pData), cb);
                ::HeapFree(::GetProcessHeap(), 0, const_cast<LPVOID>(pData));
            }
            return added;
        }

} // namespace AeroClip::Clipboard