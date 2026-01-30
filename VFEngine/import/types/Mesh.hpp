#pragma once
#include <fstream>
#include <functional>
#include <array>
#include <unordered_map>
#include "config/Config.hpp"
#include "resource/Types.hpp"
#include "resource/MeshletTypes.hpp"
#include "resource/ConvexHullTypes.hpp"
#include "resource/ClusterDAGTypes.hpp"
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

    struct ExtractedSkeleton
    {
        bool hasSkinning = false;
        std::vector<resource::SkeletonBone> bones;
        std::vector<glm::mat4> inverseBindPoses;
        glm::mat4 globalInverseTransform{1.0f};
        std::unordered_map<std::string, uint32_t> boneNameToIndex;
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
        static constexpr uint32_t MAX_BONES_PER_VERTEX = 4;

        // VK-300: lodRatios removed - discrete LOD no longer generated

        void saveToFileStreamingWithLOD(std::string_view location, std::string_view fileName,
                                        const aiScene* scene, const importConfig::ImportConfig& config,
                                        MeshProgressCallback progressCallback) const;

        ExtractedSkeleton extractSkeleton(const aiScene* scene) const;
        LODMeshData convertAssimpMesh(const aiMesh* assimpMesh, const ExtractedSkeleton& skeleton) const;
        // VK-300: generateLODLevels() and simplifyMesh() removed - discrete LOD no longer generated
        void writeLODLevel(std::ofstream& outFile, const LODMeshData& lodMesh) const;

        MeshletBuildResult buildMeshletsForLOD(const LODMeshData& lodMesh) const;
        // VK-300: Added optional reorder map for cluster DAG contiguous meshlet indices
        void writeMeshletData(std::ofstream& outFile,
                              const std::array<MeshletBuildResult, resource::LOD_LEVEL_COUNT>& meshletResults,
                              const std::vector<uint32_t>& meshletReorderMap = {}) const;

        resource::ConvexDecompositionData generateConvexDecomposition(
            const LODMeshData& meshData,
            const importConfig::MeshImportConfig& config) const;
        void writeConvexDecompositionData(std::ofstream& outFile,
                                          const resource::ConvexDecompositionData& decomposition) const;

        void writeClusterDAGData(std::ofstream& outFile,
                                 const resource::ClusterDAGData& dagData) const;

        void writeSkeletonData(std::ofstream& outFile, const ExtractedSkeleton& skeleton) const;
    };
}
