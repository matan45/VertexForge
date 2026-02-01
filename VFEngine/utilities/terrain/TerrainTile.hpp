#pragma once

#include "TerrainTypes.hpp"
#include "../resource/Types.hpp"
#include "../resource/MeshletTypes.hpp"
#include "../math/Frustum.hpp"
#include <vector>
#include <array>

namespace terrain
{
    // Per-LOD geometry data for a terrain tile
    struct TileLODData
    {
        // Geometry data (uses existing engine vertex format for compatibility)
        std::vector<resource::Vertex> vertices;
        std::vector<uint32_t> indices;

        // Meshlet data (populated by VK-196 meshlet generation)
        std::vector<resource::Meshlet> meshlets;
        std::vector<uint32_t> meshletVertices;
        std::vector<uint32_t> meshletPrimitives;

        // Bounding volumes
        math::AABB aabb;
        glm::vec4 boundingSphere{0.0f};  // xyz = center (local space), w = radius

        // Geometric error: max vertical deviation from previous LOD (world units)
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

    // Edge vertices for seamless border stitching between tiles
    struct EdgeVertices
    {
        std::vector<uint32_t> indices;     // Vertex indices along edge in the LOD's vertex array
        std::vector<glm::vec3> positions;  // World-space positions for neighbor comparison

        [[nodiscard]] bool isEmpty() const { return indices.empty(); }

        void clear()
        {
            indices.clear();
            positions.clear();
        }
    };

    // Edge stitching info for LOD transitions between neighboring tiles
    struct EdgeStitchInfo
    {
        bool needsSnapping = false;        // True if this edge needs vertex height snapping
        uint8_t neighborLOD = 0;           // LOD level of the neighbor tile
        std::vector<float> snappedHeights; // Snapped Y values for edge vertices

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

        // Material indices for each layer (references into material system)
        std::array<uint32_t, MAX_TERRAIN_LAYERS> layerMaterialIndices{};

        TileWeightMap() = default;

        void resize(uint32_t w, uint32_t h, uint32_t layers);
        void setWeight(uint32_t x, uint32_t y, uint32_t layer, uint8_t value);
        [[nodiscard]] uint8_t getWeight(uint32_t x, uint32_t y, uint32_t layer) const;
        void normalize(uint32_t x, uint32_t y);  // Ensure weights sum to 255
        void clear();

        [[nodiscard]] bool isEmpty() const { return weights.empty(); }
    };

    // Terrain tile - represents a single chunk of terrain geometry
    class TerrainTile
    {
    public:
        // Identity and configuration
        TileCoord coord;
        TerrainTileConfig config;

        // World-space data
        glm::vec3 worldOrigin{0.0f};  // Bottom-left corner at minimum height
        math::AABB worldBounds;

        // LOD geometry data (4 levels)
        std::array<TileLODData, TERRAIN_LOD_COUNT> lodLevels;

        // Current LOD level (set by LOD selection pass)
        uint8_t currentLOD = 0;

        // Neighbor information for edge stitching (North, East, South, West)
        std::array<NeighborInfo, 4> neighbors;

        // Edge vertices per LOD level per edge direction
        // [lodLevel][edge] -> EdgeVertices
        std::array<std::array<EdgeVertices, 4>, TERRAIN_LOD_COUNT> edgeVertices;

        // Edge stitching info per edge (indexed by TileEdge)
        // Used when neighbor has coarser LOD to snap edge vertex heights
        std::array<EdgeStitchInfo, 4> edgeStitchInfo;

        // Height data (kept for runtime queries and modification)
        // Row-major order, size = vertexCount * vertexCount
        std::vector<float> heightData;

        // Weight map for texture painting
        TileWeightMap weightMap;

        // State flags
        bool isDirty = true;           // Needs geometry regeneration
        bool isWeightMapDirty = true;  // Needs weight map GPU update
        bool isVisible = true;         // Result of frustum culling

    public:
        TerrainTile() = default;
        TerrainTile(const TileCoord& coord, const TerrainTileConfig& config);

        // Initialize tile with flat height
        void initializeFlat(float height = 0.0f);

        // Initialize tile from height array
        void initializeFromHeights(const std::vector<float>& heights);

        // World-space calculations
        [[nodiscard]] glm::vec3 computeWorldOrigin() const;
        [[nodiscard]] glm::vec3 getWorldVertexPosition(uint32_t x, uint32_t z) const;
        [[nodiscard]] bool containsWorldPosition(float worldX, float worldZ) const;

        // Height sampling
        [[nodiscard]] float sampleHeight(float u, float v) const;  // UV in [0,1]
        [[nodiscard]] float sampleHeightWorld(float worldX, float worldZ) const;
        void setHeight(uint32_t x, uint32_t z, float height);
        [[nodiscard]] float getHeight(uint32_t x, uint32_t z) const;

        // Neighbor management
        void setNeighbor(TileEdge edge, const TileCoord& neighborCoord, uint8_t lod);
        void clearNeighbor(TileEdge edge);
        [[nodiscard]] bool hasNeighbor(TileEdge edge) const;
        [[nodiscard]] bool needsStitching() const;

        // Edge stitching helpers
        [[nodiscard]] bool hasActiveStitching() const;
        [[nodiscard]] float getStitchedEdgeHeight(TileEdge edge, uint32_t vertexIndex) const;

        // Bounds calculation
        void updateWorldBounds();

        // Get the LOD data for current or specified level
        [[nodiscard]] TileLODData& getCurrentLODData();
        [[nodiscard]] const TileLODData& getCurrentLODData() const;
        [[nodiscard]] TileLODData& getLODData(uint32_t level);
        [[nodiscard]] const TileLODData& getLODData(uint32_t level) const;

        // Clear all generated data
        void clearGeometry();

    private:
        // Validate height data array index
        [[nodiscard]] bool isValidHeightIndex(uint32_t x, uint32_t z) const;
        [[nodiscard]] size_t getHeightIndex(uint32_t x, uint32_t z) const;
    };

} // namespace terrain
