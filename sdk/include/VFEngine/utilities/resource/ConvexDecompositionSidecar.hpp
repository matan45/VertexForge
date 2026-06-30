#pragma once

#include "ConvexHullTypes.hpp"

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace resource
{
    struct ConvexDecompositionSidecarEntry
    {
        uint32_t submeshIndex = 0;
        ConvexDecompositionData data;
    };

    class ConvexDecompositionSidecar
    {
    public:
        static std::filesystem::path sidecarPathForMesh(std::string_view meshPath);

        static bool load(std::string_view meshPath,
                         std::vector<ConvexDecompositionSidecarEntry>& outEntries);

        static bool loadForSubmesh(std::string_view meshPath,
                                   uint32_t submeshIndex,
                                   ConvexDecompositionData& outData);

        static bool save(std::string_view meshPath,
                         const std::vector<ConvexDecompositionSidecarEntry>& entries);

        static bool upsert(std::string_view meshPath,
                           const std::vector<ConvexDecompositionSidecarEntry>& entries);
    };
}
