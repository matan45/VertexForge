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

namespace resource {

    // Information about a single LOD level for seeking
    struct LODFileInfo {
        std::streampos fileOffset = 0;  // File position where this LOD data starts
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;

        // Calculate memory size for this LOD
        size_t getMemorySize() const {
            return (vertexCount * sizeof(Vertex)) + (indexCount * sizeof(uint32_t));
        }
    };

    // Information about meshlet data for a single LOD level
    struct LODMeshletFileInfo {
        uint32_t meshletCount = 0;
        uint32_t vertexIndexCount = 0;
        uint32_t primitiveCount = 0;

        // Calculate memory size for meshlet data of this LOD
        size_t getMeshletMemorySize() const {
            return (meshletCount * sizeof(Meshlet)) +
                   (vertexIndexCount * sizeof(uint32_t)) +
                   (primitiveCount * sizeof(uint32_t));
        }
    };

    // Information about a submesh for streaming
    struct SubmeshStreamInfo {
        std::string name;
        std::array<LODFileInfo, LOD_LEVEL_COUNT> lods;

        // Meshlet info per LOD (v0.0.4+)
        std::array<LODMeshletFileInfo, LOD_LEVEL_COUNT> meshletLods{};
        std::streampos meshletDataOffset = 0;  // File position where meshlet data starts
        bool hasMeshletData = false;
    };

    // Header information parsed from mesh file (without loading geometry)
    struct MeshStreamHeader {
        FileType headerFileType = FileType::MESH;
        FileVersion version{};
        uint32_t numSubmeshes = 0;
        std::vector<SubmeshStreamInfo> submeshes;

        // Get total memory size for all LODs of all submeshes
        size_t getTotalMemorySize() const {
            size_t total = 0;
            for (const auto& submesh : submeshes) {
                for (const auto& lod : submesh.lods) {
                    total += lod.getMemorySize();
                }
            }
            return total;
        }

        // Get memory size for a specific LOD level across all submeshes
        size_t getLODLevelMemorySize(uint32_t lodLevel) const {
            if (lodLevel >= LOD_LEVEL_COUNT) return 0;
            size_t total = 0;
            for (const auto& submesh : submeshes) {
                total += submesh.lods[lodLevel].getMemorySize();
            }
            return total;
        }
    };

    // Streaming file handle for mesh files
    // Keeps file open and reads LOD data on demand
    class MeshStreamHandle {
    public:
        MeshStreamHandle() = default;
        ~MeshStreamHandle();

        // Non-copyable
        MeshStreamHandle(const MeshStreamHandle&) = delete;
        MeshStreamHandle& operator=(const MeshStreamHandle&) = delete;

        // Movable
        MeshStreamHandle(MeshStreamHandle&& other) noexcept;
        MeshStreamHandle& operator=(MeshStreamHandle&& other) noexcept;

        // Open mesh file and parse header (stores file offsets for LODs)
        bool openStream(std::string_view path);

        // Close the file handle
        void close();

        // Check if file is open
        bool isOpen() const { return file.is_open(); }

        // Get parsed header info
        const MeshStreamHeader& getHeader() const { return header; }

        // Get file path
        const std::string& getPath() const { return filePath; }

        // Read a specific LOD level for a submesh
        // Returns true on success, vertices/indices filled with data
        bool readLODLevel(uint32_t submeshIdx, uint32_t lodLevel,
                          std::vector<Vertex>& outVertices,
                          std::vector<uint32_t>& outIndices);

        // Read meshlet data for a submesh (v0.0.4+)
        // Returns true on success, meshlet data filled
        bool readMeshletData(uint32_t submeshIdx, SubmeshMeshletData& outMeshletData);

        // Check if file has meshlet data
        bool hasMeshletData() const { return hasMeshlets; }

        // Get memory size estimate for a specific LOD
        size_t getLODMemorySize(uint32_t submeshIdx, uint32_t lodLevel) const;

        // Get meshlet memory size estimate for a specific LOD
        size_t getMeshletMemorySize(uint32_t submeshIdx, uint32_t lodLevel) const;

        // Get total vertex count for a specific LOD across all submeshes
        uint32_t getTotalVertexCount(uint32_t lodLevel) const;

        // Get total index count for a specific LOD across all submeshes
        uint32_t getTotalIndexCount(uint32_t lodLevel) const;

    private:
        std::ifstream file;
        MeshStreamHeader header;
        std::string filePath;
        bool hasMeshlets = false;  // True if file has meshlet data (v0.0.4+)
        mutable std::mutex fileMutex;  // Protects file reads from concurrent access

        // Parse header and compute file offsets for each LOD
        bool parseHeader();

        // Parse meshlet headers after LOD data (v0.0.4+)
        bool parseMeshletHeaders(uint32_t meshIdx);

        // Safety limits (same as MeshResource)
        static constexpr uint32_t maxVertexCount = 10'000'000;
        static constexpr uint32_t maxIndexCount = 30'000'000;
        static constexpr uint32_t maxSubmeshCount = 10'000;
        static constexpr uint32_t maxMeshletCount = 1'000'000;
    };

    // Static factory for creating stream handles
    class MeshStreamResource {
    public:
        // Open a mesh file for streaming (reads header only)
        static std::unique_ptr<MeshStreamHandle> openStream(std::string_view path);

        // Check if a file supports streaming (valid mesh format)
        static bool supportsStreaming(std::string_view path);

        // Thread-safe static method to read a single LOD level
        // Opens its own file handle, reads the data, and closes it
        // Uses the pre-parsed header info for offsets and validation
        static bool readLODFromFile(std::string_view path,
                                     const LODFileInfo& lodInfo,
                                     std::vector<Vertex>& outVertices,
                                     std::vector<uint32_t>& outIndices);
    };

}
