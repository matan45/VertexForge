#pragma once
#include "Mesh.hpp"
#include <atomic>

namespace types
{
    class MeshLODGenerator
    {
    public:
        std::array<LODMeshData, resource::LOD_LEVEL_COUNT> generateLODLevels(const LODMeshData& lod0) const;
        MeshletBuildResult buildMeshletsForLOD(const LODMeshData& lodMesh) const;

        resource::ConvexDecompositionData generateConvexDecomposition(
            const LODMeshData& meshData,
            const importConfig::MeshImportConfig& config,
            ConvexProgressCallback progressCallback = nullptr,
            std::atomic<bool>* cancelFlag = nullptr) const;

    private:
        static constexpr std::array<float, resource::LOD_LEVEL_COUNT> lodRatios = {1.0f, 0.5f, 0.25f, 0.125f};

        LODMeshData simplifyMesh(const LODMeshData& source, float targetRatio) const;
    };
}
