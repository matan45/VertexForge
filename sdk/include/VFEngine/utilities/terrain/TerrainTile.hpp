#pragma once
#include "TerrainExport.hpp"

#include "TerrainTypes.hpp"
#include "TerrainWeightMap.hpp"
#include "CaveSDFData.hpp"
#include "../vegetation/VegetationTypes.hpp"
#include "../foliage/FoliageTypes.hpp"
#include "../resource/Types.hpp"
#include "../resource/MeshletTypes.hpp"
#include "../math/Frustum.hpp"
#include <vector>
#include <array>
#include <memory>

namespace terrain
{
#pragma warning(push)
#pragma warning(disable: 4251)
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

    class VF_TERRAIN_API TerrainTile
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

        // Billboard vegetation instances placed by brush
        std::vector<vegetation::BillboardInstance> billboardInstances;
        bool billboardInstancesDirty = false;
        bool billboardInstancesGPUDirty = false;

        // Foliage mesh instances placed by brush (packed, entity-free — VK-1571)
        std::vector<foliage::FoliageInstance> foliageInstances;
        bool foliageInstancesDirty = false;    // CPU/serialization dirty (VK-1575)
        bool foliageInstancesGPUDirty = false; // GPU re-upload gate      (VK-1573)

        std::unique_ptr<CaveSDFData> caveData;
        TileLODData caveLOD;
        bool caveDirty = false;
        bool caveGPUDirty = false;

        bool isDirty = true;
        bool isVisible = true;

        bool edgeSyncDirty = false;
        uint8_t dirtyLODMask = 0;
        uint8_t gpuDirtyLODMask = 0;

        bool isLODDirty(uint32_t lod) const { return (dirtyLODMask & (1 << lod)) != 0; }
        void clearLODDirty(uint32_t lod) { dirtyLODMask &= ~(1 << lod); }
        void setAllLODsDirty() { dirtyLODMask = 0x3F; isDirty = true; }

        // VK-1620: the RVT world-height plane is baked from a GPU copy of heightData, so that copy
        // has to be invalidated whenever heights move. Starts true so a freshly created or loaded
        // tile uploads once. Only meaningful when the world-height plane is enabled; otherwise the
        // arena does not exist and every upload path early-outs.
        bool heightFieldGPUDirty = true;

        bool isLODGPUDirty(uint32_t lod) const { return (gpuDirtyLODMask & (1 << lod)) != 0; }
        // Marking a LOD for GPU re-upload is precisely the moment a tile's geometry changed, which
        // is the only way its heights change. Invalidating here rather than at each sculpt / erosion
        // / spline / stamp call site means a future height-editing path cannot forget to do it. A
        // topology-only edit (hole mask) re-uploads heights redundantly — one memcpy of data already
        // in RAM, which is a cheap price for that immunity.
        void setLODGPUDirty(uint32_t lod) { gpuDirtyLODMask |= (1 << lod); heightFieldGPUDirty = true; }
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

        [[nodiscard]] bool hasBillboardInstances() const { return !billboardInstances.empty(); }
        [[nodiscard]] bool hasFoliageInstances() const { return !foliageInstances.empty(); }

        void initializeCaveSDF();
        void initializeCaveSDFFromHeights();
        [[nodiscard]] bool hasCaveData() const { return caveData && caveData->isInitialized(); }
        [[nodiscard]] bool hasCaveGeometry() const { return caveData && caveData->hasCaveGeometry(); }

        void maskBelowWaterLevel(float waterHeight, float margin = 0.0f);

    private:
        void initializeFlat(float height = 0.0f);

        [[nodiscard]] bool isValidHeightIndex(uint32_t x, uint32_t z) const;
        [[nodiscard]] size_t getHeightIndex(uint32_t x, uint32_t z) const;

    };
#pragma warning(pop)

} // namespace terrain
