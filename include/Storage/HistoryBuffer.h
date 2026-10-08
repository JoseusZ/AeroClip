#ifndef AEROCLIP_STORAGE_HISTORYBUFFER_H
#define AEROCLIP_STORAGE_HISTORYBUFFER_H

#include "AeroClip.h"
#include <windows.h>
#include <string>
#include <vector>
#include <functional>

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

        // ---- Persistencia de anclados (NUEVO, no rompe API existente) ----
        // Devuelve el numero de anclados que hay actualmente en memoria.
        int  PinnedCount() const;
        // Carga desde %LOCALAPPDATA%\AeroClip\Pinned\*.acp los items
        // anclados en arranques anteriores y los inserta como pinned=TRUE.
        // Devuelve cuantos se cargaron con exito. No falla si el directorio
        // no existe o esta vacio.
        int  LoadPinnedFromDisk();
        // Callback opcional: se invoca cuando TogglePin rechaza la operacion
        // por haberse alcanzado kMaxPinned. El host puede usarlo para
        // mostrar un globo/balloon al usuario.
        void SetOnPinLimitExceeded(std::function<void()> cb);

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

        // ---- Persistencia de anclados (NUEVO) ----
        // Carpeta: %LOCALAPPDATA% / AeroClip / Pinned
        WCHAR m_pinDir[MAX_PATH] = {};
        DWORD m_pinSeq = 0;
        std::function<void()> m_onPinLimitExceeded;

        // Construye m_pinDir y crea el directorio si hace falta.
        void   InitPinDir();
        // Path final del fichero persistente para un item dado.
        void   GetPinnedFilePath(const HistoryItem& it, WCHAR* out, DWORD cch) const;
        // Serializa+cifra y escribe a m_pinDir. TRUE si OK.
        BOOL   SavePinnedToDisk(const HistoryItem& it);
        // Borra el fichero persistente asociado a un item (si existe).
        void   DeletePinnedFromDisk(const HistoryItem& it);
        // Lee un fichero .acp y rellena un HistoryItem. FALSE si falla.
        BOOL   LoadPinnedFromFile(const WCHAR* path, HistoryItem& out);
    };

} // namespace AeroClip::Storage

#endif // AEROCLIP_STORAGE_HISTORYBUFFER_H