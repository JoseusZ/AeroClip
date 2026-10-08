#ifndef AEROCLIP_STORAGE_HISTORYBUFFER_H
#define AEROCLIP_STORAGE_HISTORYBUFFER_H

#include "AeroClip.h"
#include <windows.h>
#include <string>
#include <vector>

namespace AeroClip::Storage {

    struct RenderItem {
        std::wstring  text;
        // 48x48 grayscale JPEG-quality 50 (~1.5 KB por item).
        std::vector<BYTE> thumbnailJpeg;
    };

    class HistoryBuffer {
    public:
        HistoryBuffer();
        ~HistoryBuffer();

        // Thread-safe operations (CRITICAL_SECTION) 
        int  Add(ItemKind kind, const void* pData, DWORD cbData);
        BOOL Remove(int index);
        int  RemoveUnpinned();
        BOOL TogglePin(int index);

        int         Count() const;
        HistoryItem Get(int index) const;   // copy-by-value (thread-safe)
        BOOL        IsPinned(int index) const;

        // Compare the content with the most recent item to avoid duplicates.
        BOOL IsSameAsLast(const void* pData, DWORD cbData) const;

        // DPAPI 
        // Decrypts a copy of the item. Returns a HeapAlloc buffer (caller must use HeapFree).
        static LPVOID DecryptCopy(const AeroClip::HistoryItem& item, DWORD* pcbOut);

        // Render cache (avoids decoding during WM_PAINT)
        void PrepareRender();
        void ReleaseRender();
        const RenderItem& GetCached(int index) const;
        BOOL IsRenderReady() const;

        // Disk backing (images)
        BOOL  PersistImageToTemp(const void* pCompressed, DWORD cbCompressed,
                                  DWORD cbOriginal, WCHAR* outPath, DWORD cchOutPath);
        // Reads the file and returns a copy of the LZ4+DPAPI block in outCompressed.
        // (does not decrypt). Returns FALSE if the file does not exist or is corrupt.
        static BOOL  LoadImageFromTemp(const HistoryItem& item,
                                        std::vector<BYTE>& outCompressed,
                                        DWORD* pcbOriginal);
        // Deletes the associated temporary file (does not decrypt anything).
        static void  DeleteImageTemp(const HistoryItem& item);

        // Cleanup 
        void CleanupOrphanTemps();
        // Deletes the own PID files (upon closing the process).
        void CleanupByCurrentPid();

    private:
        std::vector<HistoryItem> m_items;
        std::vector<RenderItem> m_cache;
        BOOL m_renderReady;
        mutable CRITICAL_SECTION m_cs;
        mutable CRITICAL_SECTION m_cacheCs;

        // Internal helpers 
        static LPVOID Protect(const void* inData, DWORD cbData, SIZE_T& cbOut);
        static void   DestroyItem(HistoryItem& it);
        static LPVOID DecryptCopyInternal(const HistoryItem& item, DWORD* pcbOut);
        static BOOL   IsDuplicate(const HistoryItem& item, ItemKind kind,
                                  const void* pData, DWORD cbData);

        // Disk backing
        WCHAR m_tempDir[MAX_PATH] = {};
        DWORD m_seq = 0;
    };

} // namespace AeroClip::Storage

#endif // AEROCLIP_STORAGE_HISTORYBUFFER_H