#pragma once

#include "TerrainTile.hpp"
#include <memory>
#include <functional>

namespace terrain
{
    // Callback to look up a neighbor tile by coordinate
    using TileLookup = std::function<const TerrainTile*(const TileCoord&)>;

    // Heights from a single neighbor edge (one row just beyond the shared boundary)
    struct NeighborEdgeHeights
    {
        std::vector<float> heights;  // vertexCount elements along the shared edge
        bool available = false;
    };

    // Neighbor height data for all four edges, used for cross-boundary normal calculation
    struct TileNeighborContext
    {
        std::array<NeighborEdgeHeights, 4> edges;  // North, East, South, West
    };

    class TerrainTileGenerator
    {
    private:
        TerrainTileConfig config;
        HeightSampler heightSampler;

    public:
        explicit TerrainTileGenerator(const TerrainTileConfig& config);
        ~TerrainTileGenerator() = default;

        void setConfig(const TerrainTileConfig& config);
        void setHeightSampler(HeightSampler sampler);

        [[nodiscard]] std::unique_ptr<TerrainTile> generateTile(
            const TileCoord& coord,
            ProgressCallback progress = nullptr
        ) const;

        void generateAllLODs(TerrainTile& tile, ProgressCallback progress = nullptr,
                             const TileLookup& getTile = nullptr) const;

        // Regenerate a single LOD from current heightData (for incremental sculpt updates)
        void regenerateLOD(TerrainTile& tile, uint32_t lodLevel,
                           const TileLookup& getTile = nullptr) const;

        [[nodiscard]] uint32_t calculateLOD(
            const glm::vec3& cameraPosition,
            const TerrainTile& tile
        ) const;

        void updateEdgeStitching(TerrainTile& tile) const;

    private:
        void generateLODGeometry(TerrainTile& tile, uint32_t lodLevel,
                                 const TileLookup& getTile = nullptr) const;

        // Fast path: regenerate vertices/normals/bounds but reuse existing meshlet topology
        void generateLODGeometryFast(TerrainTile& tile, uint32_t lodLevel,
                                     const TileLookup& getTile = nullptr) const;

        void generateMeshlets(TileLODData& lodData) const;
        void updateMeshletBounds(TileLODData& lodData) const;

        void generateSkirts(
            std::vector<resource::Vertex>& vertices,
            std::vector<uint32_t>& indices,
            uint32_t lodLevel,
            float skirtDepth
        ) const;

        void extractEdgeVertices(TerrainTile& tile, uint32_t lodLevel) const;

        [[nodiscard]] float computeGeometricError(const TerrainTile& tile, uint32_t lodLevel) const;
        void computeAllLODErrors(TerrainTile& tile) const;
        void computeEdgeStitching(TerrainTile& tile, TileEdge edge, uint8_t neighborLOD) const;

        [[nodiscard]] uint32_t getLODVertexCount(uint32_t lodLevel) const;
        [[nodiscard]] uint32_t getLODSkipFactor(uint32_t lodLevel) const;

        void generateVertices(
            std::vector<resource::Vertex>& vertices,
            const TerrainTile& tile,
            uint32_t lodLevel
        ) const;

        void generateIndices(
            std::vector<uint32_t>& indices,
            uint32_t lodLevel
        ) const;

        void calculateNormals(
            std::vector<resource::Vertex>& vertices,
            const std::vector<uint32_t>& indices,
            uint32_t vertCount,
            float spacing,
            const TileNeighborContext& neighborCtx
        ) const;

        [[nodiscard]] TileNeighborContext collectNeighborContext(
            const TerrainTile& tile,
            uint32_t lodLevel,
            const TileLookup& getTile
        ) const;

        void calculateBounds(TileLODData& lodData) const;

        void addSkirtEdge(
            std::vector<resource::Vertex>& vertices,
            std::vector<uint32_t>& indices,
            const std::vector<resource::Vertex>& mainVertices,
            TileEdge edge,
            uint32_t lodLevel,
            float skirtDepth
        ) const;

        [[nodiscard]] bool isEdgeVertex(uint32_t x, uint32_t z, uint32_t vertCount) const;
        [[nodiscard]] bool isCornerVertex(uint32_t x, uint32_t z, uint32_t vertCount) const;
        [[nodiscard]] TileEdge getEdgeForVertex(uint32_t x, uint32_t z, uint32_t vertCount) const;
        [[nodiscard]] uint32_t getEdgeVertexIndex(uint32_t x, uint32_t z, uint32_t vertCount, TileEdge edge) const;
        [[nodiscard]] float getStitchedHeight(
            const TerrainTile& tile,
            uint32_t x, uint32_t z,
            uint32_t vertCount,
            uint32_t lodLevel
        ) const;
    };

}
