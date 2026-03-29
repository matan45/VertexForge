#pragma once

#include "../serialization/SerializationExport.hpp"
#include "HLODTypes.hpp"
#include "../resource/Types.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace world
{
    static constexpr std::array<char, 4> HLOD_MAGIC = {'V', 'F', 'H', 'L'};
    static constexpr uint32_t HLOD_FORMAT_VERSION = 1;
    static constexpr size_t HLOD_HEADER_SIZE = 64;
    static constexpr uint32_t HLOD_LOD_LEVELS = 4;

    struct HLODFileHeader
    {
        std::array<char, 4> magic = {'V', 'F', 'H', 'L'};
        uint32_t version = 1;
        uint8_t tier = 0;
        uint8_t cellSize = 1;
        uint16_t reserved0 = 0;
        int32_t cellX = 0;
        int32_t cellZ = 0;
        float aabbMinX = 0.0f, aabbMinY = 0.0f, aabbMinZ = 0.0f;
        float aabbMaxX = 0.0f, aabbMaxY = 0.0f, aabbMaxZ = 0.0f;
        uint32_t submeshCount = 0;
        uint32_t totalVertexCount = 0;
        uint32_t totalIndexCount = 0;
        uint32_t lodLevelCount = HLOD_LOD_LEVELS;
        uint32_t reserved1 = 0;
    };

    struct HLODSubmeshLOD
    {
        uint32_t vertexOffset = 0;
        uint32_t vertexCount = 0;
        uint32_t indexOffset = 0;
        uint32_t indexCount = 0;
        uint32_t meshletOffset = 0;
        uint32_t meshletCount = 0;
    };

    struct HLODSubmeshInfo
    {
        std::string materialPath;
        std::array<HLODSubmeshLOD, HLOD_LOD_LEVELS> lods;
    };

    struct HLODMeshletData
    {
        std::vector<uint8_t> meshletBlob;       // raw meshlet descriptors
        std::vector<uint32_t> meshletVertices;   // vertex index buffer
        std::vector<uint8_t> meshletPrimitives;  // packed triangle indices
    };

    struct HLODFileData
    {
        HLODFileHeader header;
        std::vector<HLODSubmeshInfo> submeshes;
        std::vector<resource::Vertex> vertices;
        std::vector<uint32_t> indices;
        HLODMeshletData meshletData;
    };

    class VF_SERIALIZATION_API HLODSerialization
    {
    public:
        static bool save(const std::string& filePath, const HLODFileData& data);
        static bool load(const std::string& filePath, HLODFileData& outData);
        static bool readHeader(const std::string& filePath, HLODFileHeader& outHeader);
    };

} // namespace world
