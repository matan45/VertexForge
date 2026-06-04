#pragma once
#include "TerrainExport.hpp"

#include "TerrainTile.hpp"
#include <memory>
#include <functional>

namespace terrain
{
    // Callback to look up a neighbor tile by coordinate
    using TileLookup = std::function<const TerrainTile*(const TileCoord&)>;

#pragma warning(push)
#pragma warning(disable: 4251)
    class VF_TERRAIN_API TerrainTileGenerator
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


    private:
        void generateLODGeometry(TerrainTile& tile, uint32_t lodLevel,
                                 const TileLookup& getTile = nullptr) const;

        // Fast path: regenerate vertices/normals/bounds but reuse existing meshlet topology
        void generateLODGeometryFast(TerrainTile& tile, uint32_t lodLevel,
                                     const TileLookup& getTile = nullptr) const;

        void generateMeshlets(TileLODData& lodData) const;
        void appendSkirtMeshlets(TileLODData& lodData, uint32_t mainIndexCount) const;
        void updateMeshletBounds(TileLODData& lodData) const;

        void generateSkirts(
            std::vector<resource::Vertex>& vertices,
            std::vector<uint32_t>& indices,
            uint32_t lodLevel,
            float skirtDepth,
            const std::vector<uint8_t>& holeMask,
            uint32_t baseVertexCount
        ) const;

        [[nodiscard]] float computeGeometricError(const TerrainTile& tile, uint32_t lodLevel) const;
        void computeAllLODErrors(TerrainTile& tile) const;

        [[nodiscard]] uint32_t getLODVertexCount(uint32_t lodLevel) const;
        [[nodiscard]] uint32_t getLODSkipFactor(uint32_t lodLevel) const;

        void generateVertices(
            std::vector<resource::Vertex>& vertices,
            const TerrainTile& tile,
            uint32_t lodLevel
        ) const;

        void generateIndices(
            std::vector<uint32_t>& indices,
            uint32_t lodLevel,
            const std::vector<uint8_t>& holeMask,
            uint32_t baseVertexCount
        ) const;

        void calculateNormals(
            std::vector<resource::Vertex>& vertices,
            const std::vector<uint32_t>& indices,
            uint32_t vertCount
        ) const;

        void calculateBounds(TileLODData& lodData) const;

    public:
        struct SkirtEdgeVertex
        {
            uint32_t idx = 0;
            uint32_t baseX = 0;
            uint32_t baseZ = 0;
        };

        struct SkirtEdgeParams
        {
            const std::vector<resource::Vertex>* mainVertices = nullptr;
            TileEdge edge = TileEdge::North;
            uint32_t lodLevel = 0;
            float skirtDepth = 0.0f;
            const std::vector<uint8_t>* holeMask = nullptr;
            uint32_t baseVertexCount = 0;
        };

        void addSkirtEdge(
            std::vector<resource::Vertex>& vertices,
            std::vector<uint32_t>& indices,
            const SkirtEdgeParams& params
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

        // Override boundary vertex normals with analytical central differences
        // from full-resolution heightData to guarantee matching normals across tiles
        void overrideBoundaryNormals(
            std::vector<resource::Vertex>& vertices,
            const TerrainTile& tile,
            uint32_t lodLevel,
            uint32_t vertCount,
            const TileLookup& getTile
        ) const;
    };
#pragma warning(pop)

}
