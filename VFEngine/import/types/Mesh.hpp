#pragma once
#include <fstream>
#include <functional>
#include <array>
#include "config/Config.hpp"
#include "resource/Types.hpp"
#include "resource/MeshletTypes.hpp"
#include "resource/ConvexHullTypes.hpp"
struct aiScene;
struct aiMesh;

namespace types
{
    using MeshProgressCallback = std::function<void(float progress)>;

    struct LODMeshData
    {
        std::vector<resource::Vertex> vertices;
        std::vector<uint32_t> indices;
    };


    struct MeshletBuildResult
    {
        std::vector<resource::Meshlet> meshlets;
        std::vector<uint32_t> meshletVertices;
        std::vector<uint32_t> meshletPrimitives;
    };

    class Mesh
    {
    public:
        void loadFromFile(const importConfig::ImportFiles& file, std::string_view fileName,
                          std::string_view location, MeshProgressCallback progressCallback = nullptr) const;

    private:
        static constexpr size_t chunkSize = 256 * 1024;

        static constexpr std::array<float, resource::LOD_LEVEL_COUNT> lodRatios = {1.0f, 0.5f, 0.25f, 0.125f};


        void saveToFileStreamingWithLOD(std::string_view location, std::string_view fileName,
                                        const aiScene* scene, const importConfig::ImportConfig& config,
                                        MeshProgressCallback progressCallback) const;

        LODMeshData convertAssimpMesh(const aiMesh* assimpMesh) const;

        std::array<LODMeshData, resource::LOD_LEVEL_COUNT> generateLODLevels(const LODMeshData& lod0) const;

        LODMeshData simplifyMesh(const LODMeshData& source, float targetRatio) const;

        void writeLODLevel(std::ofstream& outFile, const LODMeshData& lodMesh) const;

        // Meshlet generation
        MeshletBuildResult buildMeshletsForLOD(const LODMeshData& lodMesh) const;

        void writeMeshletData(std::ofstream& outFile,
                              const std::array<MeshletBuildResult, resource::LOD_LEVEL_COUNT>& meshletResults) const;

        // V-HACD convex decomposition
        resource::ConvexDecompositionData generateConvexDecomposition(
            const LODMeshData& meshData,
            const importConfig::MeshImportConfig& config) const;

        void writeConvexDecompositionData(std::ofstream& outFile,
                                          const resource::ConvexDecompositionData& decomposition) const;
    };
}
