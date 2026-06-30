#pragma once

#include "../ImportExport.hpp"
#include "Mesh.hpp"

#include <atomic>
#include <cstdint>
#include <string>

namespace types
{
    struct ConvexRegenerationResult
    {
        bool success = false;
        std::string message;
        std::string sidecarPath;
        uint32_t submeshCount = 0;
        uint32_t hullCount = 0;
    };

    class VF_IMPORT_API ConvexDecompositionRegenerator
    {
    public:
        static ConvexRegenerationResult regenerate(
            const std::string& meshPath,
            int32_t submeshIndex,
            importConfig::MeshImportConfig config,
            ConvexProgressCallback progressCallback = nullptr,
            std::atomic<bool>* cancelFlag = nullptr);
    };
}
