#pragma once

#include "GPUDrivenTypes.hpp"
#include "TerrainMeshBuffer.hpp"
#include "resource/MeshletTypes.hpp"
#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace terrain
{
    class TerrainTile;
    struct TileLODData;
}

namespace render::gpudriven
{
    struct TerrainTileKey
    {
        int32_t coordX = 0;
        int32_t coordZ = 0;

        bool operator==(const TerrainTileKey& other) const
        {
            return coordX == other.coordX && coordZ == other.coordZ;
        }
    };

    struct TerrainTileKeyHash
    {
        size_t operator()(const TerrainTileKey& key) const
        {
            // Bit masking handles negative coordinates correctly
            uint64_t x = static_cast<uint32_t>(key.coordX);
            uint64_t z = static_cast<uint32_t>(key.coordZ);
            return std::hash<uint64_t>()((x << 32) | z);
        }
    };

    struct TerrainLODAllocation
    {
        uint32_t vertexOffset = 0;
        uint32_t vertexCount = 0;
        uint32_t indexOffset = 0;
        uint32_t indexCount = 0;

        uint32_t meshletOffset = 0;
        uint32_t meshletCount = 0;
        uint32_t meshletVertexOffset = 0;
        uint32_t meshletVertexCount = 0;
        uint32_t meshletPrimitiveOffset = 0;
        uint32_t meshletPrimitiveCount = 0;

        bool isAllocated = false;
    };

    struct TerrainTileAllocation
    {
        TerrainTileKey key;
        std::array<TerrainLODAllocation, LOD_LEVEL_COUNT> lodAllocs;

        glm::vec3 aabbMin{0.0f};
        glm::vec3 aabbMax{0.0f};
        glm::vec4 boundingSphere{0.0f}; // xyz = center, w = radius

        std::array<float, LOD_LEVEL_COUNT> geometricErrors{0.0f};

        uint32_t weightMapOffset = 0;      // Byte offset into weight map SSBO
        bool weightMapUploaded = false;

        bool isUploaded = false;

        bool hasAnyAllocation() const
        {
            for (const auto& lod : lodAllocs)
            {
                if (lod.isAllocated) return true;
            }
            return false;
        }

        std::string getMeshPath() const
        {
            return "terrain_" + std::to_string(key.coordX) + "_" + std::to_string(key.coordZ);
        }
    };

    class TerrainGPUAdapter
    {
    private:
        TerrainMeshBuffer& terrainBuffer_;

        std::unordered_map<TerrainTileKey, TerrainTileAllocation, TerrainTileKeyHash> allocations_;

        std::vector<TerrainTileGPUData> cachedGPUTileData_;
        bool gpuTileDataDirty_ = true;

        std::unordered_map<TerrainTileKey, glm::uvec4, TerrainTileKeyHash> tileLightmapData_;

    public:
        explicit TerrainGPUAdapter(TerrainMeshBuffer& terrainBuffer);
        ~TerrainGPUAdapter();

        TerrainGPUAdapter(const TerrainGPUAdapter&) = delete;
        TerrainGPUAdapter& operator=(const TerrainGPUAdapter&) = delete;

        TerrainTileAllocation* uploadTile(const terrain::TerrainTile& tile);

        bool uploadTileAddLOD(const terrain::TerrainTile& tile, uint32_t lodLevel);

        bool uploadWeightMap(const terrain::TerrainTile& tile);

        // Keeps other LODs intact
        void removeTileLOD(const TerrainTileKey& key, uint32_t lodLevel);

        bool hasTile(const TerrainTileKey& key) const;

        void clear();

        void markGPUTileDataDirty() { gpuTileDataDirty_ = true; }

        void setTileLightmapData(int coordX, int coordZ, glm::uvec4 lightmapData);
        void clearTileLightmapData();

        const std::vector<TerrainTileGPUData>& buildGPUTileData(
            const std::vector<terrain::TerrainTile*>& tiles);

    private:
        bool uploadLODData(
            TerrainTileAllocation& alloc,
            const terrain::TileLODData& lodData,
            uint32_t lodLevel,
            const glm::vec3& worldOrigin);

        GPUMeshlet convertMeshlet(
            const resource::Meshlet& srcMeshlet,
            uint32_t globalVertexOffset) const;
    };
}
