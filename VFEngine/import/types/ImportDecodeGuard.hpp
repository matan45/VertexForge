#pragma once
#include <semaphore>
#include "cpumem/CpuMemoryManager.hpp"
#include "cpumem/CpuMemoryCategories.hpp"

// Shared import-decode concurrency guard + CPU-memory categories.
//
// Extracted from Texture.cpp so every decode translation unit in the Import
// module (Texture.cpp, KtxTextureImport.cpp, DdsTextureImport.cpp) shares ONE
// semaphore. Using an `inline` variable guarantees a single instance across TUs
// — a per-TU copy would silently multiply the concurrency cap. Behaviour is
// identical to the original anonymous-namespace definition.
namespace types
{
    // Maximum number of concurrent heavy import decodes (texture + HDR + KTX/DDS)
    // within the Import module. Default 2 so at most two big scratch buffers
    // co-exist regardless of how many worker threads submit simultaneously.
    inline constexpr int kImportDecodeConcurrency = 2;
    inline std::counting_semaphore<kImportDecodeConcurrency> gImportDecodeSem{kImportDecodeConcurrency};

    // RAII acquire/release guard for the import-decode concurrency semaphore.
    struct ImportDecodeLock
    {
        ImportDecodeLock()  { gImportDecodeSem.acquire(); }
        ~ImportDecodeLock() { gImportDecodeSem.release(); }
        ImportDecodeLock(const ImportDecodeLock&) = delete;
        ImportDecodeLock& operator=(const ImportDecodeLock&) = delete;
    };

    // Category ids: resolved once per module, then reused lock-free.
    inline memory::CategoryId texDecodeCategory()
    {
        static const memory::CategoryId id =
            memory::CpuMemoryManager::instance().registerCategory(
                memory::categories::ImportTextureDecode, memory::CategoryKind::Transient);
        return id;
    }

    inline memory::CategoryId hdrDecodeCategory()
    {
        static const memory::CategoryId id =
            memory::CpuMemoryManager::instance().registerCategory(
                memory::categories::ImportHdrDecode, memory::CategoryKind::Transient);
        return id;
    }
}
