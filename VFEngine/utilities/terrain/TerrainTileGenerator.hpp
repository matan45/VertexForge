#pragma once

#include "TerrainTile.hpp"
#include <memory>

namespace terrain
{
    // Generates terrain tile geometry from height data
    class TerrainTileGenerator
    {
    public:
        explicit TerrainTileGenerator(const TerrainTileConfig& config);
        ~TerrainTileGenerator() = default;

        // Configuration
        void setConfig(const TerrainTileConfig& config);
        [[nodiscard]] const TerrainTileConfig& getConfig() const { return config_; }

        // Set height sampling function (heightmap or procedural)
        void setHeightSampler(HeightSampler sampler);

        // Generate a new tile at the given coordinate
        [[nodiscard]] std::unique_ptr<TerrainTile> generateTile(
            const TileCoord& coord,
            ProgressCallback progress = nullptr
        ) const;

        // Generate geometry for a specific LOD level
        void generateLODGeometry(TerrainTile& tile, uint32_t lodLevel) const;

        // Generate all LOD levels for a tile
        void generateAllLODs(TerrainTile& tile, ProgressCallback progress = nullptr) const;

        // Generate meshlets for a LOD (stub - full implementation in VK-196)
        void generateMeshlets(TileLODData& lodData) const;

        // Generate skirt geometry for LOD crack prevention
        void generateSkirts(
            std::vector<resource::Vertex>& vertices,
            std::vector<uint32_t>& indices,
            uint32_t lodLevel,
            float skirtDepth
        ) const;

        // Extract edge vertices for neighbor stitching
        void extractEdgeVertices(TerrainTile& tile, uint32_t lodLevel) const;

        // Calculate optimal LOD based on distance from camera
        [[nodiscard]] uint32_t calculateLOD(
            const glm::vec3& cameraPosition,
            const TerrainTile& tile
        ) const;

        // Geometric error computation
        [[nodiscard]] float computeGeometricError(const TerrainTile& tile, uint32_t lodLevel) const;
        void computeAllLODErrors(TerrainTile& tile) const;

        // Edge stitching for LOD transitions
        void computeEdgeStitching(TerrainTile& tile, TileEdge edge, uint8_t neighborLOD) const;
        void updateEdgeStitching(TerrainTile& tile) const;

        // LOD helpers
        [[nodiscard]] uint32_t getLODVertexCount(uint32_t lodLevel) const;
        [[nodiscard]] uint32_t getLODSkipFactor(uint32_t lodLevel) const;

    private:
        TerrainTileConfig config_;
        HeightSampler heightSampler_;

        // Internal generation methods
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
            const std::vector<uint32_t>& indices
        ) const;

        void calculateBounds(TileLODData& lodData) const;

        // Skirt geometry helpers
        void addSkirtEdge(
            std::vector<resource::Vertex>& vertices,
            std::vector<uint32_t>& indices,
            const std::vector<resource::Vertex>& mainVertices,
            TileEdge edge,
            uint32_t lodLevel,
            float skirtDepth
        ) const;

        // Edge stitching helpers for vertex generation
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

} // namespace terrain
