#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <fstream>
#include <memory>
#include <mutex>
#include "Types.hpp"
#include "MeshletTypes.hpp"

namespace resource
{
    struct LODFileInfo
    {
        std::streampos fileOffset = 0; // File position where this LOD data starts
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
    };

    struct LODMeshletFileInfo
    {
        uint32_t meshletCount = 0;
        uint32_t vertexIndexCount = 0;
        uint32_t primitiveCount = 0;
    };

    struct SubmeshStreamInfo
    {
        std::string name;
        std::array<LODFileInfo, LOD_LEVEL_COUNT> lods;

        // Meshlet info per LOD (v0.0.4+)
        std::array<LODMeshletFileInfo, LOD_LEVEL_COUNT> meshletLods{};
        std::streampos meshletDataOffset = 0; // File position where meshlet data starts
        bool hasMeshletData = false;
    };

    struct MeshStreamHeader
    {
        FileType headerFileType = FileType::MESH;
        FileVersion version{};
        uint32_t numSubmeshes = 0;
        std::vector<SubmeshStreamInfo> submeshes;
    };


    class MeshStreamHandle
    {
    private:
        // Safety limits to prevent excessive memory allocation from malformed files
        static constexpr uint32_t maxVertexCount = 10'000'000;
        static constexpr uint32_t maxIndexCount = 30'000'000;
        static constexpr uint32_t maxSubmeshCount = 10'000;
        static constexpr uint32_t maxMeshletCount = 1'000'000;

        std::ifstream file;
        MeshStreamHeader header;
        std::string filePath;
        bool hasMeshlets = false; // True if file has meshlet data (v0.0.4+)
        mutable std::mutex fileMutex; // Protects file reads from concurrent access

    public:
        MeshStreamHandle() = default;
        ~MeshStreamHandle();

        // Non-copyable
        MeshStreamHandle(const MeshStreamHandle&) = delete;
        MeshStreamHandle& operator=(const MeshStreamHandle&) = delete;

        // Movable
        MeshStreamHandle(MeshStreamHandle&& other) noexcept;
        MeshStreamHandle& operator=(MeshStreamHandle&& other) noexcept;

        bool openStream(std::string_view path);

        void close();

        const MeshStreamHeader& getHeader() const { return header; }

        bool readLODLevel(uint32_t submeshIdx, uint32_t lodLevel,
                          std::vector<Vertex>& outVertices,
                          std::vector<uint32_t>& outIndices);

        bool readMeshletData(uint32_t submeshIdx, SubmeshMeshletData& outMeshletData);

        bool hasMeshletData() const { return hasMeshlets; }

        uint32_t getTotalVertexCount(uint32_t lodLevel) const;

        uint32_t getTotalIndexCount(uint32_t lodLevel) const;

    private:
        bool parseHeader();

        bool parseMeshletHeaders(uint32_t meshIdx);
    };

    class MeshStreamResource
    {
    public:
        static std::unique_ptr<MeshStreamHandle> openStream(std::string_view path);

        static MeshesData loadAll(std::string_view path);

        static bool readLODFromFile(std::string_view path,
                                    const LODFileInfo& lodInfo,
                                    std::vector<Vertex>& outVertices,
                                    std::vector<uint32_t>& outIndices);
    };
}
