#include "Storage/HistoryBuffer.h"
#include <dpapi.h>

#pragma comment(lib, "Crypt32.lib")

namespace AeroClip::Storage {

    struct CriticalSectionLock {
        CRITICAL_SECTION& cs;
        explicit CriticalSectionLock(CRITICAL_SECTION& c) : cs(c) { ::EnterCriticalSection(&cs); }
        ~CriticalSectionLock() { ::LeaveCriticalSection(&cs); }
    };

    HistoryBuffer::HistoryBuffer() {
        ::InitializeCriticalSection(&m_cs);
    }

    HistoryBuffer::~HistoryBuffer() {
        CriticalSectionLock lock(m_cs);
        for (auto& it : m_items) {
            DestroyItem(it);
        }
        m_items.clear();
        ::DeleteCriticalSection(&m_cs);
    }

    LPVOID HistoryBuffer::Protect(const void* inData, DWORD cbData, SIZE_T& cbOut) {
        if (!inData || cbData == 0) { cbOut = 0; return nullptr; }

        SIZE_T cbAligned = (cbData + 15) & ~static_cast<SIZE_T>(15);
        LPVOID buf = ::HeapAlloc(::GetProcessHeap(), HEAP_ZERO_MEMORY, cbAligned);
        if (!buf) { cbOut = 0; return nullptr; }

        memcpy(buf, inData, cbData);

        if (!::CryptProtectMemory(buf, static_cast<DWORD>(cbAligned), CRYPTPROTECTMEMORY_SAME_PROCESS)) {
            ::SecureZeroMemory(buf, cbAligned);
            ::HeapFree(::GetProcessHeap(), 0, buf);
            cbOut = 0;
            return nullptr;
        }
        cbOut = cbAligned;
        return buf;
    }

    void HistoryBuffer::DestroyItem(HistoryItem& it) {
        if (it.pData && it.cbAlloc) {
            ::SecureZeroMemory(it.pData, it.cbAlloc);
            ::HeapFree(::GetProcessHeap(), 0, it.pData);
        }
        it.pData = nullptr;
        it.cbData = 0;
        it.cbAlloc = 0;
    }

    LPVOID HistoryBuffer::DecryptCopyInternal(const HistoryItem& item, DWORD* pcbOut) {
        if (pcbOut) *pcbOut = 0;
        if (!item.pData || !item.cbAlloc || !item.cbData) return nullptr;

        LPVOID buf = ::HeapAlloc(::GetProcessHeap(), 0, item.cbAlloc);
        if (!buf) return nullptr;
        memcpy(buf, item.pData, item.cbAlloc);

        if (!::CryptUnprotectMemory(buf, static_cast<DWORD>(item.cbAlloc), CRYPTPROTECTMEMORY_SAME_PROCESS)) {
            ::SecureZeroMemory(buf, item.cbAlloc);
            ::HeapFree(::GetProcessHeap(), 0, buf);
            return nullptr;
        }
        if (pcbOut) *pcbOut = item.cbData;
        return buf;
    }

    LPVOID HistoryBuffer::DecryptCopy(const HistoryItem& item, DWORD* pcbOut) {
        return DecryptCopyInternal(item, pcbOut);
    }

    BOOL HistoryBuffer::IsDuplicate(const HistoryItem& item, ItemKind kind, const void* pData, DWORD cbData) {
        if (item.kind != kind || item.cbData != cbData) return FALSE;

        DWORD cbDec = 0;
        LPVOID dec = DecryptCopyInternal(item, &cbDec);
        if (!dec) return FALSE;

        BOOL equal = (memcmp(dec, pData, cbData) == 0);
        ::SecureZeroMemory(dec, cbDec);
        ::HeapFree(::GetProcessHeap(), 0, dec);
        return equal;
    }

    int HistoryBuffer::Add(ItemKind kind, const void* pData, DWORD cbData) {
        if (!pData || cbData == 0) return -1;

        // Guardia de tamano: evita que un DIB de 50 MB o un volcado de
        // 20 MB inche la RAM del proceso. Mantenemos la filosofia Win7
        // native: 15 items, footprint acotado.
        DWORD cbLimit = (kind == ItemKind::Image) ? kMaxImageBytes : kMaxTextBytes;
        if (cbData > cbLimit) return -1;

        CriticalSectionLock lock(m_cs);

        if (!m_items.empty() && IsDuplicate(m_items[0], kind, pData, cbData)) {
            return -1;
        }

        if (static_cast<int>(m_items.size()) >= kHistoryMax) {
            int dropIdx = -1;
            for (int i = static_cast<int>(m_items.size()) - 1; i >= 0; --i) {
                if (!m_items[i].pinned) {
                    dropIdx = i;
                    break;
                }
            }
            if (dropIdx < 0) {
                return -1;
            }
            DestroyItem(m_items[dropIdx]);
            m_items.erase(m_items.begin() + dropIdx);
        }

        SIZE_T cbAlloc = 0;
        LPVOID enc = Protect(pData, cbData, cbAlloc);
        if (!enc) return -1;

        HistoryItem it;
        it.kind = kind;
        it.cbData = cbData;
        it.pData = enc;
        it.cbAlloc = cbAlloc;
        it.pinned = FALSE;
        ::GetSystemTimeAsFileTime(&it.ftCaptured);

        m_items.insert(m_items.begin(), it);
        return 0;
    }

    BOOL HistoryBuffer::Remove(int index) {
        CriticalSectionLock lock(m_cs);
        if (index < 0 || index >= static_cast<int>(m_items.size())) return FALSE;
        DestroyItem(m_items[index]);
        m_items.erase(m_items.begin() + index);
        return TRUE;
    }

    int HistoryBuffer::RemoveUnpinned() {
        CriticalSectionLock lock(m_cs);
        int removed = 0;
        for (size_t i = 0; i < m_items.size();) {
            if (!m_items[i].pinned) {
                DestroyItem(m_items[i]);
                m_items.erase(m_items.begin() + i);
                ++removed;
            }
            else {
                ++i;
            }
        }
        return removed;
    }

    BOOL HistoryBuffer::TogglePin(int index) {
        CriticalSectionLock lock(m_cs);
        if (index < 0 || index >= static_cast<int>(m_items.size())) return FALSE;
        m_items[index].pinned = !m_items[index].pinned;
        return TRUE;
    }

    int HistoryBuffer::Count() const {
        CriticalSectionLock lock(m_cs);
        return static_cast<int>(m_items.size());
    }

    HistoryItem HistoryBuffer::Get(int index) const {
        CriticalSectionLock lock(m_cs);
        if (index < 0 || index >= static_cast<int>(m_items.size())) return HistoryItem{};
        return m_items[index];
    }

    BOOL HistoryBuffer::IsPinned(int index) const {
        CriticalSectionLock lock(m_cs);
        if (index < 0 || index >= static_cast<int>(m_items.size())) return FALSE;
        return m_items[index].pinned;
    }

    BOOL HistoryBuffer::IsSameAsLast(const void* pData, DWORD cbData) const {
        if (!pData || cbData == 0) return FALSE;
        CriticalSectionLock lock(m_cs);
        if (m_items.empty()) return FALSE;
        return IsDuplicate(m_items[0], m_items[0].kind, pData, cbData);
    }

} // namespace AeroClip::Storage