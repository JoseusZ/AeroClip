#ifndef AEROCLIP_STORAGE_HISTORYBUFFER_H
#define AEROCLIP_STORAGE_HISTORYBUFFER_H

#include "AeroClip.h"
#include <windows.h>
#include <string>
#include <vector>

namespace AeroClip::Storage {

    struct RenderItem {
        std::wstring  text;
        // 48x48 grayscale JPEG-quality 50 (~1.5 KB por item). Solo presente
        // para items de tipo Image. Para texto queda vacio.
        std::vector<BYTE> thumbnailJpeg;
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

        // --- Disk backing (imagenes) -----------------------------------------
        // Persiste un bloque LZ4+DPAPI a %TEMP%\AeroClip\<pid>_<seq>.bin.
        // Escribe header [DWORD cbOriginal][DWORD cbCompressed][BYTES].
        // Devuelve TRUE si el archivo quedo en outPath.
        // No-static: usa m_tempDir/m_seq.
        BOOL  PersistImageToTemp(const void* pCompressed, DWORD cbCompressed,
                                  DWORD cbOriginal, WCHAR* outPath, DWORD cchOutPath);
        // Lee el archivo y devuelve una copia del bloque LZ4+DPAPI en outCompressed
        // (no decifra). Devuelve FALSE si el archivo no existe o esta corrupto.
        static BOOL  LoadImageFromTemp(const HistoryItem& item,
                                        std::vector<BYTE>& outCompressed,
                                        DWORD* pcbOriginal);
        // Borra el archivo temporal asociado (no decifra nada).
        static void  DeleteImageTemp(const HistoryItem& item);

        // --- Cleanup --------------------------------------------------------
        // Borra todos los *.bin de %TEMP%\AeroClip\ (huerfanos de cualquier PID).
        // Llamar al iniciar el proceso.
        void CleanupOrphanTemps();
        // Borra los archivos de PID propio (al cerrar el proceso).
        void CleanupByCurrentPid();

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

        // Disk backing: directorio %TEMP%\AeroClip\ y secuencia monotnica por PID
        WCHAR m_tempDir[MAX_PATH] = {};
        DWORD m_seq = 0;
    };

} // namespace AeroClip::Storage

#endif // AEROCLIP_STORAGE_HISTORYBUFFER_H