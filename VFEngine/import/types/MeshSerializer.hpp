#pragma once
#include "Mesh.hpp"
#include <fstream>

namespace types
{
    class MeshSerializer
    {
    public:
        void writeLODLevel(std::ofstream& outFile, const LODMeshData& lodMesh) const;

        void writeMeshletData(std::ofstream& outFile,
                              const std::array<MeshletBuildResult, resource::LOD_LEVEL_COUNT>& meshletResults) const;

        void writeConvexDecompositionData(std::ofstream& outFile,
                                          const resource::ConvexDecompositionData& decomposition) const;

        void writeSkeletonData(std::ofstream& outFile, const ExtractedSkeleton& skeleton) const;

    private:
        static constexpr size_t chunkSize = 256 * 1024;
    };
}
