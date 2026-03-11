#pragma once

#include "TerrainTypes.hpp"
#include "TerrainWeightMap.hpp"
#include "../vegetation/VegetationDensityMap.hpp"
#include "../resource/Types.hpp"
#include "../resource/MeshletTypes.hpp"
#include "../math/Frustum.hpp"
#include <vector>
#include <array>

namespace terrain
{
    struct TileLODData
    {
        std::vector<resource::Vertex> vertices;
        std::vector<uint32_t> indices;

        std::vector<resource::Meshlet> meshlets;
        std::vector<uint32_t> meshletVertices;
        std::vector<uint32_t> meshletPrimitives;
        uint32_t mainMeshletCount = 0; // Surface-only meshlets (excludes skirts), used by shadow pass

        math::AABB aabb;
        glm::vec4 boundingSphere{0.0f};
        float geometricError = 0.0f;

        [[nodiscard]] bool isEmpty() const { return vertices.empty(); }
        [[nodiscard]] bool hasMeshlets() const { return !meshlets.empty(); }

        void clear()
        {
            vertices.clear();
            indices.clear();
            meshlets.clear();
            meshletVertices.clear();
            meshletPrimitives.clear();
            mainMeshletCount = 0;
            aabb = math::AABB();
            boundingSphere = glm::vec4(0.0f);
        }
    };

    class TerrainTile
    {
    public:
        TileCoord coord;
        TerrainTileConfig config;

        glm::vec3 worldOrigin{0.0f};
        math::AABB worldBounds;

        std::array<TileLODData, TERRAIN_LOD_COUNT> lodLevels;

        std::array<NeighborInfo, 4> neighbors;

        std::vector<float> heightData;

        std::vector<uint8_t> holeMask; // Per-quad hole mask, size = quadCount*quadCount where quadCount = vertexCount-1 (0=solid, 1=hole)
        bool topologyDirty = false;    // Forces full meshlet rebuild (bypasses fast path)

        TileWeightMapData weightMap;
        bool weightMapDirty = false;
        bool weightMapGPUDirty = false;

        vegetation::VegetationDensityMap vegetationDensity;
        bool vegetationDensityDirty = false;
        bool vegetationDensityGPUDirty = false;

        bool isDirty = true;
        bool isVisible = true;

        bool edgeSyncDirty = false;
        uint8_t dirtyLODMask = 0;
        uint8_t gpuDirtyLODMask = 0;

        bool isLODDirty(uint32_t lod) const { return (dirtyLODMask & (1 << lod)) != 0; }
        void clearLODDirty(uint32_t lod) { dirtyLODMask &= ~(1 << lod); }
        void setAllLODsDirty() { dirtyLODMask = 0x0F; isDirty = true; }

        bool isLODGPUDirty(uint32_t lod) const { return (gpuDirtyLODMask & (1 << lod)) != 0; }
        void setLODGPUDirty(uint32_t lod) { gpuDirtyLODMask |= (1 << lod); }
        void clearLODGPUDirty(uint32_t lod) { gpuDirtyLODMask &= ~(1 << lod); }
        bool hasAnyGPUDirtyLOD() const { return gpuDirtyLODMask != 0; }

    public:
        TerrainTile() = default;
        TerrainTile(const TileCoord& coord, const TerrainTileConfig& config);

        void initializeFromHeights(const std::vector<float>& heights);
        void initializeMetadataOnly();

        [[nodiscard]] glm::vec3 computeWorldOrigin() const;
        [[nodiscard]] float getHeight(uint32_t x, uint32_t z) const;

        void setNeighbor(TileEdge edge, const TileCoord& neighborCoord);
        void clearNeighbor(TileEdge edge);

        [[nodiscard]] bool hasHeightData() const { return !heightData.empty(); }
        [[nodiscard]] bool hasLODData(uint32_t lod) const { return lod < TERRAIN_LOD_COUNT && !lodLevels[lod].isEmpty(); }
        [[nodiscard]] bool hasAnyLODData() const;

        [[nodiscard]] bool hasHoleMask() const { return !holeMask.empty(); }
        [[nodiscard]] bool isHole(uint32_t x, uint32_t z) const;
        void setHole(uint32_t x, uint32_t z, bool isHoleValue);
        void initializeHoleMask();

        [[nodiscard]] TileLODData& getLODData(uint32_t level);
        [[nodiscard]] const TileLODData& getLODData(uint32_t level) const;

        void updateWorldBounds();

        void initializeWeightMap();
        [[nodiscard]] bool hasWeightMap() const { return weightMap.isInitialized(); }

        void initializeVegetationDensity();
        [[nodiscard]] bool hasVegetationDensity() const { return vegetationDensity.isInitialized(); }

    private:
        void initializeFlat(float height = 0.0f);

        [[nodiscard]] bool isValidHeightIndex(uint32_t x, uint32_t z) const;
        [[nodiscard]] size_t getHeightIndex(uint32_t x, uint32_t z) const;

    };

} // namespace terrain
