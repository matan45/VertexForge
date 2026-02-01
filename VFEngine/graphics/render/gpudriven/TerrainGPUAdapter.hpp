#pragma once

#include "GPUDrivenTypes.hpp"
#include "MeshletBufferTypes.hpp"
#include "../mesh/MeshTypes.hpp"
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
    class MergedMeshBuffer;
    class MeshletBuffer;

    // Unique key for terrain tiles in GPU buffers
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
            // Combine x and z coordinates into a single hash
            return std::hash<int64_t>()(
                (static_cast<int64_t>(key.coordX) << 32) |
                static_cast<uint32_t>(key.coordZ)
            );
        }
    };

    // Per-LOD allocation info for a terrain tile
    struct TerrainLODAllocation
    {
        // Vertex/index allocation in MergedMeshBuffer
        uint32_t vertexOffset = 0;
        uint32_t vertexCount = 0;
        uint32_t indexOffset = 0;
        uint32_t indexCount = 0;

        // Meshlet allocation in MeshletBuffer
        uint32_t meshletOffset = 0;
        uint32_t meshletCount = 0;
        uint32_t meshletVertexOffset = 0;
        uint32_t meshletVertexCount = 0;
        uint32_t meshletPrimitiveOffset = 0;
        uint32_t meshletPrimitiveCount = 0;

        bool isAllocated = false;
    };

    // Allocation tracking per terrain tile
    struct TerrainTileAllocation
    {
        TerrainTileKey key;
        std::array<TerrainLODAllocation, LOD_LEVEL_COUNT> lodAllocs;

        // Bounds in world space
        glm::vec3 aabbMin{0.0f};
        glm::vec3 aabbMax{0.0f};
        glm::vec4 boundingSphere{0.0f}; // xyz = center, w = radius

        // Geometric error per LOD for GPU LOD selection
        std::array<float, LOD_LEVEL_COUNT> geometricErrors{0.0f};

        bool isUploaded = false;

        bool hasAnyAllocation() const
        {
            for (const auto& lod : lodAllocs)
            {
                if (lod.isAllocated) return true;
            }
            return false;
        }

        // Generate mesh path key for this tile
        std::string getMeshPath() const
        {
            return "terrain_" + std::to_string(key.coordX) + "_" + std::to_string(key.coordZ);
        }
    };

    // Adapter that converts terrain tiles to GPU-compatible format
    class TerrainGPUAdapter
    {
    private:
        MergedMeshBuffer& mergedBuffer_;
        MeshletBuffer& meshletBuffer_;

        std::unordered_map<TerrainTileKey, TerrainTileAllocation, TerrainTileKeyHash> allocations_;

    public:
        TerrainGPUAdapter(MergedMeshBuffer& mergedBuffer, MeshletBuffer& meshletBuffer);
        ~TerrainGPUAdapter();

        TerrainGPUAdapter(const TerrainGPUAdapter&) = delete;
        TerrainGPUAdapter& operator=(const TerrainGPUAdapter&) = delete;

        // Upload a terrain tile's geometry to GPU buffers
        // Returns allocation info for tracking, nullptr on failure
        TerrainTileAllocation* uploadTile(const terrain::TerrainTile& tile);

        // Remove a tile from GPU buffers
        void removeTile(const TerrainTileKey& key);

        // Check if tile is already uploaded
        bool hasTile(const TerrainTileKey& key) const;

        // Get allocation for a tile
        const TerrainTileAllocation* getAllocation(const TerrainTileKey& key) const;

        // Build MeshRenderData for visible terrain tiles
        std::vector<mesh::MeshRenderData> buildRenderData(
            const std::vector<terrain::TerrainTile*>& tiles,
            entt::entity terrainEntity) const;

        // Get all active allocations
        size_t getAllocationCount() const { return allocations_.size(); }

        // Clear all allocations
        void clear();

        // Build GPU tile data for terrain mesh shader pipeline
        // Converts TerrainTileAllocation + TerrainTile to TerrainTileGPUData format
        std::vector<TerrainTileGPUData> buildGPUTileData(
            const std::vector<terrain::TerrainTile*>& tiles) const;

    private:
        // Upload single LOD data to GPU buffers
        bool uploadLODData(
            TerrainTileAllocation& alloc,
            const terrain::TileLODData& lodData,
            uint32_t lodLevel,
            const glm::vec3& worldOrigin);

        // Convert terrain meshlet to GPU meshlet format
        GPUMeshlet convertMeshlet(
            const resource::Meshlet& srcMeshlet,
            uint32_t globalVertexOffset) const;
    };
}
