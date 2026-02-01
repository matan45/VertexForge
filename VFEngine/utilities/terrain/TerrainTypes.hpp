#pragma once

#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace terrain
{
    // Tile resolution presets (vertices per side = quads + 1)
    // Power-of-2 + 1 pattern ensures edge vertices overlap with neighbors
    enum class TileResolution : uint8_t
    {
        Low = 0,      // 33x33 vertices (32x32 quads) -> ~17 meshlets
        Medium = 1,   // 65x65 vertices (64x64 quads) -> ~66 meshlets
        High = 2      // 129x129 vertices (128x128 quads) -> ~264 meshlets
    };

    // Vertex counts per tile side for each resolution
    constexpr std::array<uint32_t, 3> TILE_VERTEX_COUNTS = { 33, 65, 129 };

    // Quad counts per tile side for each resolution
    constexpr std::array<uint32_t, 3> TILE_QUAD_COUNTS = { 32, 64, 128 };

    // Maximum texture layers for weight-based terrain blending (RGBA8 = 4, can use multiple textures)
    constexpr uint32_t MAX_TERRAIN_LAYERS = 8;

    // LOD level count matches existing engine system
    constexpr uint32_t TERRAIN_LOD_COUNT = 4;

    // Edge identifiers for neighbor stitching
    enum class TileEdge : uint8_t
    {
        North = 0,  // +Z direction
        East = 1,   // +X direction
        South = 2,  // -Z direction
        West = 3    // -X direction
    };

    // 2D tile coordinate in the terrain grid
    struct TileCoord
    {
        int32_t x = 0;
        int32_t z = 0;

        TileCoord() = default;
        TileCoord(int32_t x, int32_t z) : x(x), z(z) {}

        bool operator==(const TileCoord& other) const
        {
            return x == other.x && z == other.z;
        }

        bool operator!=(const TileCoord& other) const
        {
            return !(*this == other);
        }

        TileCoord operator+(const TileCoord& other) const
        {
            return TileCoord(x + other.x, z + other.z);
        }

        TileCoord operator-(const TileCoord& other) const
        {
            return TileCoord(x - other.x, z - other.z);
        }

        // Get neighbor coordinate offset for a given edge
        static TileCoord getNeighborOffset(TileEdge edge)
        {
            switch (edge)
            {
                case TileEdge::North: return TileCoord(0, 1);
                case TileEdge::East:  return TileCoord(1, 0);
                case TileEdge::South: return TileCoord(0, -1);
                case TileEdge::West:  return TileCoord(-1, 0);
                default: return TileCoord(0, 0);
            }
        }

        // Get the opposite edge
        static TileEdge getOppositeEdge(TileEdge edge)
        {
            switch (edge)
            {
                case TileEdge::North: return TileEdge::South;
                case TileEdge::East:  return TileEdge::West;
                case TileEdge::South: return TileEdge::North;
                case TileEdge::West:  return TileEdge::East;
                default: return edge;
            }
        }
    };

    // Hash function for TileCoord (for use in unordered_map)
    struct TileCoordHash
    {
        size_t operator()(const TileCoord& coord) const
        {
            // Combine x and z into a single hash value
            size_t h1 = std::hash<int32_t>{}(coord.x);
            size_t h2 = std::hash<int32_t>{}(coord.z);
            return h1 ^ (h2 << 1);
        }
    };

    // Terrain tile configuration
    struct TerrainTileConfig
    {
        // Tile resolution preset
        TileResolution resolution = TileResolution::Low;

        // World units per tile edge
        float worldTileSize = 32.0f;

        // Height range
        float maxHeight = 100.0f;
        float minHeight = -10.0f;

        // Weight map resolution per tile (independent of geometry resolution)
        uint32_t weightMapResolution = 64;

        // LOD distance thresholds (world units from camera)
        std::array<float, TERRAIN_LOD_COUNT> lodDistances = { 100.0f, 300.0f, 600.0f, 1200.0f };

        // Vertical skirt depth for LOD crack prevention
        float skirtDepth = 5.0f;

        // Get the number of vertices per tile side for current resolution
        [[nodiscard]] uint32_t getVertexCount() const
        {
            return TILE_VERTEX_COUNTS[static_cast<uint8_t>(resolution)];
        }

        // Get the number of quads per tile side for current resolution
        [[nodiscard]] uint32_t getQuadCount() const
        {
            return TILE_QUAD_COUNTS[static_cast<uint8_t>(resolution)];
        }

        // Calculate vertex spacing in world units
        [[nodiscard]] float getVertexSpacing() const
        {
            return worldTileSize / static_cast<float>(getQuadCount());
        }

        // Get total number of vertices in a tile (including all edges)
        [[nodiscard]] uint32_t getTotalVertexCount() const
        {
            uint32_t count = getVertexCount();
            return count * count;
        }

        // Get total number of triangles in a tile
        [[nodiscard]] uint32_t getTotalTriangleCount() const
        {
            uint32_t quads = getQuadCount();
            return quads * quads * 2;  // 2 triangles per quad
        }
    };

    // Neighbor information for edge stitching
    struct NeighborInfo
    {
        TileCoord coord;
        uint8_t lodLevel = 0;
        bool exists = false;

        NeighborInfo() = default;
        NeighborInfo(const TileCoord& c, uint8_t lod) : coord(c), lodLevel(lod), exists(true) {}
    };

    // Height sampling callback type (heightmap or procedural generation)
    using HeightSampler = std::function<float(float worldX, float worldZ)>;

    // Progress callback type for async operations
    using ProgressCallback = std::function<void(float progress, const std::string& stage)>;

} // namespace terrain
