#pragma once
#include <fstream>
#include <functional>
#include <array>
#include <atomic>
#include <cstdint>
#include <unordered_map>
#include "config/Config.hpp"
#include "resource/Types.hpp"
#include "resource/MeshletTypes.hpp"
#include "resource/ConvexHullTypes.hpp"
struct aiScene;
struct aiMesh;

namespace types
{
    using MeshProgressCallback = std::function<void(float progress)>;
    using ConvexProgressCallback = std::function<void(float progress, std::string_view stage)>;

    enum class MeshOutputLayout : uint8_t
    {
        Split,
        CombinedStatic
    };

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
        std::vector<animator::SocketDefinition> sockets;
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
        // outWrittenFiles, when non-null, receives the absolute path of every
        // .vfMesh files written according to outputLayout. outWrittenTextures, when
        // non-null, enables embedded-texture extraction (VK-55): each texture in
        // aiScene->mTextures is written as a .vfImage and its path appended. Both
        // let the importer surface produced assets for .vfmeta / registration.
        void loadFromFile(const importConfig::ImportFiles& file, std::string_view fileName,
                          std::string_view location, MeshOutputLayout outputLayout = MeshOutputLayout::Split,
                          MeshProgressCallback progressCallback = nullptr,
                          std::vector<std::string>* outWrittenFiles = nullptr,
                          std::vector<std::string>* outWrittenTextures = nullptr) const;

    private:
        static constexpr uint32_t MAX_BONES_PER_VERTEX = 4;

        // Extracts aiScene embedded textures (aiScene->mTextures) into .vfImage
        // files in `location`, appending each written path to outWrittenTextures.
        void extractEmbeddedTextures(const aiScene* scene, std::string_view location,
                                     std::string_view fileName, const importConfig::ImportConfig& config,
                                     std::vector<std::string>& outWrittenTextures) const;

        // Writes one .vfMesh per mesh in the scene (each with numMeshes == 1).
        void saveToFileStreamingWithLOD(std::string_view location, std::string_view fileName,
                                        const aiScene* scene, const importConfig::ImportConfig& config,
                                        MeshProgressCallback progressCallback,
                                        std::vector<std::string>* outWrittenFiles) const;

        void saveCombinedStaticMesh(std::string_view location, std::string_view fileName,
                                    const aiScene* scene, const importConfig::ImportConfig& config,
                                    MeshProgressCallback progressCallback,
                                    std::vector<std::string>* outWrittenFiles) const;

        void generateAndSaveFracturedMesh(std::string_view location, std::string_view fileName,
                                          const aiScene* scene, const importConfig::ImportConfig& config,
                                          MeshProgressCallback progressCallback) const;

        ExtractedSkeleton extractSkeleton(const aiScene* scene) const;
        LODMeshData convertAssimpMesh(const aiMesh* assimpMesh, const ExtractedSkeleton& skeleton) const;
    };
}
