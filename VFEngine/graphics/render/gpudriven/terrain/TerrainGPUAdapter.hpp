#pragma once

#include "../GPUDrivenTypes.hpp"
#include "TerrainMeshBuffer.hpp"
#include "resource/MeshletTypes.hpp"
#include "terrain/TerrainLayerVisibility.hpp"
#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <unordered_map>
#include <functional>
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
        std::array<TerrainLODAllocation, TERRAIN_LOD_LEVEL_COUNT> lodAllocs;
        TerrainLODAllocation caveAlloc;    // Cave meshlet allocation (single LOD)

        glm::vec3 aabbMin{0.0f};
        glm::vec3 aabbMax{0.0f};
        glm::vec4 boundingSphere{0.0f}; // xyz = center, w = radius

        std::array<float, TERRAIN_LOD_LEVEL_COUNT> geometricErrors{0.0f};

        uint32_t weightMapOffset = 0;      // Byte offset into weight map SSBO
        bool weightMapUploaded = false;
        // VK-1613: which layer-visibility mask this tile's uploaded weight bytes were packed for.
        // Starts at 0 while the adapter's live version starts at 1, so a tile that has never
        // uploaded can never be mistaken for one that is already current.
        uint32_t weightMaskVersion = 0;
        // VK-1613: gates the arena-exhaustion error to once per tile per failure episode. It does NOT
        // gate the retry — a tile that never uploaded publishes weightMapOffset = 0 and samples
        // another tile's weights, and retrying is the only way it recovers.
        bool weightMapAllocFailed = false;

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

        int32_t selectedCoordX_ = 0;
        int32_t selectedCoordZ_ = 0;
        bool hasSelectedTile_ = false;

        // VK-1613 per-layer visibility. The mask is material state pushed down by
        // registerTerrainLayerTextures; the version is what lets each tile discover, on its own
        // schedule, that its uploaded weight bytes were packed for an older mask.
        uint32_t layerEnabledMask_ = terrain::ALL_TERRAIN_LAYERS_ENABLED;
        uint32_t maskVersion_ = 1;

        void populateGPUTile(TerrainTileGPUData& gpuTile,
                             const TerrainTileAllocation& alloc,
                             const terrain::TerrainTile& tile,
                             const TerrainTileKey& key);

    public:
        explicit TerrainGPUAdapter(TerrainMeshBuffer& terrainBuffer);
        ~TerrainGPUAdapter();

        TerrainGPUAdapter(const TerrainGPUAdapter&) = delete;
        TerrainGPUAdapter& operator=(const TerrainGPUAdapter&) = delete;

        TerrainTileAllocation* uploadTile(const terrain::TerrainTile& tile);

        bool uploadTileAddLOD(const terrain::TerrainTile& tile, uint32_t lodLevel);

        bool uploadWeightMap(const terrain::TerrainTile& tile);

        // VK-1613: hidden layers are honoured by zeroing their weight bytes at upload time, so a
        // visibility change has to re-pack the tiles. Setting a NEW mask bumps the version, which is
        // what needsWeightMapUpload() below compares against — an unchanged mask re-uploads nothing.
        void setLayerEnabledMask(uint32_t mask);
        [[nodiscard]] uint32_t getLayerEnabledMask() const { return layerEnabledMask_; }

        // Single decision point for "does this tile's weight map need to go to the GPU": the
        // caller's dirty flag, a tile that has no upload yet, or one packed for an older mask.
        [[nodiscard]] bool needsWeightMapUpload(const terrain::TerrainTile& tile) const;
        bool uploadCaveMesh(const terrain::TerrainTile& tile);
        // Frees a tile's GPU cave allocation (when a cave is filled/undone away). The
        // next buildGPUTileData zeroes caveMeshletData so the cave stops rendering.
        bool releaseCaveMesh(const terrain::TerrainTile& tile);

        // Keeps other LODs intact
        void removeTileLOD(const TerrainTileKey& key, uint32_t lodLevel);

        bool hasTile(const TerrainTileKey& key) const;

        void clear();

        void markGPUTileDataDirty() { gpuTileDataDirty_ = true; }

        // Acceleration structure callbacks — invoked when tile LOD geometry is uploaded or removed
        std::function<void(const std::string& tileKey, uint32_t vertexOffset, uint32_t vertexCount,
                           uint32_t indexOffset, uint32_t indexCount)> onTileLODReady;
        std::function<void(const std::string& tileKey)> onTileRemoved;

        void setSelectedTile(int32_t coordX, int32_t coordZ);
        void clearSelectedTile();

        const std::vector<TerrainTileGPUData>& buildGPUTileData(
            const std::vector<terrain::TerrainTile*>& tiles);

        const std::vector<TerrainTileGPUData>& getCachedGPUTileData() const { return cachedGPUTileData_; }

    private:
        bool uploadLODData(
            TerrainTileAllocation& alloc,
            const terrain::TileLODData& lodData,
            uint32_t lodLevel,
            const glm::vec3& worldOrigin);

        GPUMeshlet convertMeshlet(
            const resource::Meshlet& srcMeshlet,
            uint32_t globalVertexOffset) const;

        bool uploadCaveMeshlets(const std::string& caveKey,
                                const terrain::TileLODData& caveLOD,
                                const TerrainLODGeometry& geomLod);
    };
}
