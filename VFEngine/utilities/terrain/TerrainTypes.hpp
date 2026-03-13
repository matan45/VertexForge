#pragma once

#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace terrain
{
    // Power-of-2 + 1 pattern ensures edge vertices overlap with neighbors
    enum class TileResolution : uint8_t
    {
        Low = 0, // 33x33 vertices (32x32 quads) -> ~17 meshlets
        Medium = 1, // 65x65 vertices (64x64 quads) -> ~66 meshlets
        High = 2 // 129x129 vertices (128x128 quads) -> ~264 meshlets
    };

    constexpr std::array<uint32_t, 3> TILE_VERTEX_COUNTS = {33, 65, 129};
    constexpr std::array<uint32_t, 3> TILE_QUAD_COUNTS = {32, 64, 128};

    constexpr uint32_t TERRAIN_LOD_COUNT = 4;

    enum class TileEdge : uint8_t
    {
        North = 0, // +Z direction
        East = 1, // +X direction
        South = 2, // -Z direction
        West = 3 // -X direction
    };

    struct TileCoord
    {
        int32_t x = 0;
        int32_t z = 0;

        TileCoord() = default;

        TileCoord(int32_t x, int32_t z) : x(x), z(z)
        {
        }

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

        static TileCoord getNeighborOffset(TileEdge edge)
        {
            switch (edge)
            {
            case TileEdge::North: return TileCoord(0, 1);
            case TileEdge::East: return TileCoord(1, 0);
            case TileEdge::South: return TileCoord(0, -1);
            case TileEdge::West: return TileCoord(-1, 0);
            default: return TileCoord(0, 0);
            }
        }

        static TileEdge getOppositeEdge(TileEdge edge)
        {
            switch (edge)
            {
            case TileEdge::North: return TileEdge::South;
            case TileEdge::East: return TileEdge::West;
            case TileEdge::South: return TileEdge::North;
            case TileEdge::West: return TileEdge::East;
            default: return edge;
            }
        }
    };

    struct TileCoordHash
    {
        size_t operator()(const TileCoord& coord) const
        {
            size_t h1 = std::hash<int32_t>{}(coord.x);
            size_t h2 = std::hash<int32_t>{}(coord.z);
            return h1 ^ (h2 << 1);
        }
    };

    struct TerrainTileConfig
    {
        TileResolution resolution = TileResolution::Low;
        float worldTileSize = 32.0f;
        float maxHeight = 100.0f;
        float minHeight = -10.0f;
        // Vertical skirt depth for LOD crack prevention
        float skirtDepth = 5.0f;

        [[nodiscard]] uint32_t getVertexCount() const
        {
            return TILE_VERTEX_COUNTS[static_cast<uint8_t>(resolution)];
        }

        [[nodiscard]] uint32_t getQuadCount() const
        {
            return TILE_QUAD_COUNTS[static_cast<uint8_t>(resolution)];
        }

        [[nodiscard]] float getVertexSpacing() const
        {
            return worldTileSize / static_cast<float>(getQuadCount());
        }
    };

    struct NeighborInfo
    {
        TileCoord coord;
        bool exists = false;

        NeighborInfo() = default;

        explicit NeighborInfo(const TileCoord& c) : coord(c), exists(true)
        {
        }
    };

    // Heightmap or procedural generation callback
    using HeightSampler = std::function<float(float worldX, float worldZ)>;

    using ProgressCallback = std::function<void(float progress, const std::string& stage)>;
}
