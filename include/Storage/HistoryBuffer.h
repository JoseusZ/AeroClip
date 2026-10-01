#ifndef AEROCLIP_STORAGE_HISTORYBUFFER_H
#define AEROCLIP_STORAGE_HISTORYBUFFER_H

#include "AeroClip.h"
#include <windows.h>
#include <string>
#include <vector>

namespace AeroClip::Storage {

    struct RenderItem {
        std::wstring  text;
        std::vector<BYTE> image;
    };

    class HistoryBuffer {
    public:
        HistoryBuffer();
        ~HistoryBuffer();

        // --- Operaciones thread-safe (CRITICAL_SECTION) -----------------------
        int  Add(ItemKind kind, const void* pData, DWORD cbData);
        BOOL Remove(int index);
        int  RemoveUnpinned();
        BOOL TogglePin(int index);

        int         Count() const;
        HistoryItem Get(int index) const;   // copia por valor (thread-safe)
        BOOL        IsPinned(int index) const;

        // Compara el contenido con el item mas reciente para evitar duplicados.
        BOOL IsSameAsLast(const void* pData, DWORD cbData) const;

        // --- DPAPI ------------------------------------------------------------
        // Descifra una copia del item. Devuelve buffer HeapAlloc (caller HeapFree).
        static LPVOID DecryptCopy(const AeroClip::HistoryItem& item, DWORD* pcbOut);

        // --- Render cache (evita descifrar en WM_PAINT) ----------------------
        void PrepareRender();
        void ReleaseRender();
        const RenderItem& GetCached(int index) const;
        BOOL IsRenderReady() const;

    private:
        std::vector<HistoryItem> m_items;
        std::vector<RenderItem> m_cache;
        BOOL m_renderReady;
        mutable CRITICAL_SECTION m_cs;
        mutable CRITICAL_SECTION m_cacheCs;

        // Helpers internos
        static LPVOID Protect(const void* inData, DWORD cbData, SIZE_T& cbOut);
        static void   DestroyItem(HistoryItem& it);
        static LPVOID DecryptCopyInternal(const HistoryItem& item, DWORD* pcbOut);
        static BOOL   IsDuplicate(const HistoryItem& item, ItemKind kind,
                                  const void* pData, DWORD cbData);
    };

} // namespace AeroClip::Storage

#endif // AEROCLIP_STORAGE_HISTORYBUFFER_H