#pragma once

#include "TerrainTypes.hpp"
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

        math::AABB aabb;
        glm::vec4 boundingSphere{0.0f};  // xyz = center (local space), w = radius

        // Max vertical deviation from previous LOD (world units)
        // LOD 0 = 0.0 (highest detail), LOD N = max deviation from LOD N-1
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
            aabb = math::AABB();
            boundingSphere = glm::vec4(0.0f);
        }
    };

    struct EdgeVertices
    {
        std::vector<uint32_t> indices;
        std::vector<glm::vec3> positions;  // World-space positions for neighbor comparison

        void clear()
        {
            indices.clear();
            positions.clear();
        }
    };

    struct EdgeStitchInfo
    {
        bool needsSnapping = false;
        uint8_t neighborLOD = 0;
        std::vector<float> snappedHeights;

        void clear()
        {
            needsSnapping = false;
            neighborLOD = 0;
            snappedHeights.clear();
        }
    };

    // Weight map for texture painting (prepared for VK-178)
    struct TileWeightMap
    {
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t layerCount = 0;

        // Weights per pixel, indexed by [y * width + x][layer]
        // Values 0-255, should sum to 255 per pixel for proper blending
        std::vector<std::array<uint8_t, MAX_TERRAIN_LAYERS>> weights;

        std::array<uint32_t, MAX_TERRAIN_LAYERS> layerMaterialIndices{};

        TileWeightMap() = default;

        void resize(uint32_t w, uint32_t h, uint32_t layers);
        void setWeight(uint32_t x, uint32_t y, uint32_t layer, uint8_t value);
        [[nodiscard]] uint8_t getWeight(uint32_t x, uint32_t y, uint32_t layer) const;
        void normalize(uint32_t x, uint32_t y);
        void clear();

        [[nodiscard]] bool isEmpty() const { return weights.empty(); }
    };

    class TerrainTile
    {
    public:
        TileCoord coord;
        TerrainTileConfig config;

        glm::vec3 worldOrigin{0.0f};
        math::AABB worldBounds;

        std::array<TileLODData, TERRAIN_LOD_COUNT> lodLevels;
        uint8_t currentLOD = 0;

        std::array<NeighborInfo, 4> neighbors;

        // [lodLevel][edge] -> EdgeVertices
        std::array<std::array<EdgeVertices, 4>, TERRAIN_LOD_COUNT> edgeVertices;

        // Used when neighbor has coarser LOD to snap edge vertex heights
        std::array<EdgeStitchInfo, 4> edgeStitchInfo;

        std::vector<float> heightData;

        TileWeightMap weightMap;

        bool isDirty = true;
        bool isWeightMapDirty = true;
        bool isVisible = true;

        // Per-LOD dirty tracking for incremental sculpt updates
        // Bit N = LOD N needs CPU meshlet regeneration from heightData
        uint8_t dirtyLODMask = 0;
        // Bit N = LOD N was regenerated on CPU but not yet re-uploaded to GPU
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

        [[nodiscard]] glm::vec3 computeWorldOrigin() const;
        [[nodiscard]] float sampleHeightWorld(float worldX, float worldZ) const;
        [[nodiscard]] float getHeight(uint32_t x, uint32_t z) const;

        void setNeighbor(TileEdge edge, const TileCoord& neighborCoord, uint8_t lod);
        void clearNeighbor(TileEdge edge);

        [[nodiscard]] bool stitchingChanged() const;
        void saveStitchState();

        [[nodiscard]] TileLODData& getCurrentLODData();
        [[nodiscard]] const TileLODData& getCurrentLODData() const;
        [[nodiscard]] TileLODData& getLODData(uint32_t level);
        [[nodiscard]] const TileLODData& getLODData(uint32_t level) const;

        void updateWorldBounds();

    private:
        void initializeFlat(float height = 0.0f);

        [[nodiscard]] float sampleHeight(float u, float v) const;
        [[nodiscard]] bool containsWorldPosition(float worldX, float worldZ) const;

        [[nodiscard]] bool isValidHeightIndex(uint32_t x, uint32_t z) const;
        [[nodiscard]] size_t getHeightIndex(uint32_t x, uint32_t z) const;

        struct StitchState
        {
            bool needsSnapping = false;
            uint8_t neighborLOD = 0;
        };
        std::array<StitchState, 4> previousStitchState;
    };

} // namespace terrain
