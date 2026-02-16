#include "TerrainTileGenerator.hpp"
#include <algorithm>
#include <cmath>

namespace terrain
{
    void TerrainTileGenerator::extractEdgeVertices(TerrainTile& tile, uint32_t lodLevel) const
    {
        if (lodLevel >= TERRAIN_LOD_COUNT)
            return;

        const TileLODData& lodData = tile.getLODData(lodLevel);
        if (lodData.isEmpty())
            return;

        uint32_t vertCount = getLODVertexCount(lodLevel);

        for (uint8_t edgeIdx = 0; edgeIdx < 4; ++edgeIdx)
        {
            TileEdge edge = static_cast<TileEdge>(edgeIdx);
            EdgeVertices& edgeVerts = tile.edgeVertices[lodLevel][edgeIdx];
            edgeVerts.clear();
            edgeVerts.indices.reserve(vertCount);
            edgeVerts.positions.reserve(vertCount);

            for (uint32_t i = 0; i < vertCount; ++i)
            {
                uint32_t idx = 0;
                switch (edge)
                {
                case TileEdge::North:
                    idx = (vertCount - 1) * vertCount + i;
                    break;
                case TileEdge::South:
                    idx = i;
                    break;
                case TileEdge::East:
                    idx = i * vertCount + (vertCount - 1);
                    break;
                case TileEdge::West:
                    idx = i * vertCount;
                    break;
                }

                edgeVerts.indices.push_back(idx);

                // Store world-space position for neighbor comparison
                glm::vec3 worldPos = tile.worldOrigin + lodData.vertices[idx].position;
                edgeVerts.positions.push_back(worldPos);
            }
        }
    }

    void TerrainTileGenerator::computeEdgeStitching(
        TerrainTile& tile, TileEdge edge, uint8_t neighborLOD) const
    {
        uint8_t currentLOD = tile.currentLOD;
        uint8_t edgeIndex = static_cast<uint8_t>(edge);
        EdgeStitchInfo& info = tile.edgeStitchInfo[edgeIndex];

        // Only stitch if neighbor has coarser (higher numbered) LOD
        if (neighborLOD <= currentLOD)
        {
            info.clear();
            return;
        }

        // Store metadata only - actual snapped heights are computed inline
        // in getStitchedHeight() from tile.heightData for per-LOD correctness
        info.needsSnapping = true;
        info.neighborLOD = neighborLOD;
    }

    void TerrainTileGenerator::updateEdgeStitching(TerrainTile& tile) const
    {
        for (uint8_t i = 0; i < 4; ++i)
        {
            const NeighborInfo& neighbor = tile.neighbors[i];
            if (neighbor.exists)
            {
                computeEdgeStitching(tile, static_cast<TileEdge>(i), neighbor.lodLevel);
            }
            else
            {
                tile.edgeStitchInfo[i].clear();
            }
        }
    }

    bool TerrainTileGenerator::isEdgeVertex(uint32_t x, uint32_t z, uint32_t vertCount) const
    {
        return x == 0 || x == vertCount - 1 || z == 0 || z == vertCount - 1;
    }

    bool TerrainTileGenerator::isCornerVertex(uint32_t x, uint32_t z, uint32_t vertCount) const
    {
        bool onXEdge = (x == 0 || x == vertCount - 1);
        bool onZEdge = (z == 0 || z == vertCount - 1);
        return onXEdge && onZEdge;
    }

    TileEdge TerrainTileGenerator::getEdgeForVertex(uint32_t x, uint32_t z, uint32_t vertCount) const
    {
        // Priority: South > North > West > East (for non-corner vertices)
        // For corners, this returns the primary edge
        if (z == 0) return TileEdge::South;
        if (z == vertCount - 1) return TileEdge::North;
        if (x == 0) return TileEdge::West;
        return TileEdge::East;
    }

    uint32_t TerrainTileGenerator::getEdgeVertexIndex(uint32_t x, uint32_t z, uint32_t vertCount, TileEdge edge) const
    {
        switch (edge)
        {
        case TileEdge::North: return x; // Top row: index by x
        case TileEdge::South: return x; // Bottom row: index by x
        case TileEdge::East: return z; // Right column: index by z
        case TileEdge::West: return z; // Left column: index by z
        default: return 0;
        }
    }

    float TerrainTileGenerator::getStitchedHeight(
        const TerrainTile& tile,
        uint32_t x, uint32_t z,
        uint32_t vertCount,
        uint32_t lodLevel) const
    {
        uint32_t skipFactor = getLODSkipFactor(lodLevel);
        uint32_t heightX = x * skipFactor;
        uint32_t heightZ = z * skipFactor;
        float originalHeight = tile.getHeight(heightX, heightZ);

        if (!isEdgeVertex(x, z, vertCount))
        {
            return originalHeight;
        }

        uint32_t baseVertCount = config.getVertexCount();

        // Compute snapped height for one edge by sampling this tile's own heightData
        // at the neighbor's coarser LOD grid positions along the shared boundary.
        auto snapForEdge = [&](TileEdge edge, uint32_t edgeIdx) -> std::pair<bool, float>
        {
            const NeighborInfo& ni = tile.neighbors[static_cast<uint8_t>(edge)];
            if (!ni.exists)
                return {false, 0.0f};

            // Per-LOD check: only snap if neighbor is coarser than the LOD being generated
            if (ni.lodLevel <= lodLevel)
                return {false, 0.0f};

            uint32_t neighborSkip = getLODSkipFactor(ni.lodLevel);
            uint32_t neighborVertCount = getLODVertexCount(ni.lodLevel);

            if (neighborVertCount < 2)
                return {false, 0.0f};

            // Map this LOD's edge vertex to the neighbor's coarser grid
            float ratio = static_cast<float>(neighborVertCount - 1)
                        / static_cast<float>(vertCount - 1);
            float nIdx = static_cast<float>(edgeIdx) * ratio;
            uint32_t j0 = static_cast<uint32_t>(nIdx);
            uint32_t j1 = std::min(j0 + 1, neighborVertCount - 1);
            float t = nIdx - static_cast<float>(j0);

            // Sample heights along the shared boundary at the coarser grid positions
            uint32_t pos0 = std::min(j0 * neighborSkip, baseVertCount - 1);
            uint32_t pos1 = std::min(j1 * neighborSkip, baseVertCount - 1);

            float h0, h1;
            switch (edge)
            {
            case TileEdge::North: // z = max, boundary row
                h0 = tile.getHeight(pos0, baseVertCount - 1);
                h1 = tile.getHeight(pos1, baseVertCount - 1);
                break;
            case TileEdge::South: // z = 0, boundary row
                h0 = tile.getHeight(pos0, 0);
                h1 = tile.getHeight(pos1, 0);
                break;
            case TileEdge::East: // x = max, boundary column
                h0 = tile.getHeight(baseVertCount - 1, pos0);
                h1 = tile.getHeight(baseVertCount - 1, pos1);
                break;
            case TileEdge::West: // x = 0, boundary column
                h0 = tile.getHeight(0, pos0);
                h1 = tile.getHeight(0, pos1);
                break;
            default:
                return {false, 0.0f};
            }

            return {true, glm::mix(h0, h1, t)};
        };

        // Handle corner vertices (lie on two edges)
        if (isCornerVertex(x, z, vertCount))
        {
            TileEdge edge1, edge2;
            uint32_t idx1, idx2;

            if (z == 0) // South edge
            {
                edge1 = TileEdge::South;
                idx1 = x;
                edge2 = (x == 0) ? TileEdge::West : TileEdge::East;
                idx2 = 0;
            }
            else // North edge (z == vertCount - 1)
            {
                edge1 = TileEdge::North;
                idx1 = x;
                edge2 = (x == 0) ? TileEdge::West : TileEdge::East;
                idx2 = vertCount - 1;
            }

            auto [needs1, snap1] = snapForEdge(edge1, idx1);
            auto [needs2, snap2] = snapForEdge(edge2, idx2);

            if (needs1 && needs2)
                return (snap1 + snap2) * 0.5f;
            if (needs1)
                return snap1;
            if (needs2)
                return snap2;

            return originalHeight;
        }

        // Regular edge vertex (not a corner)
        TileEdge edge = getEdgeForVertex(x, z, vertCount);
        uint32_t edgeIdx = getEdgeVertexIndex(x, z, vertCount, edge);
        auto [needs, snapped] = snapForEdge(edge, edgeIdx);

        return needs ? snapped : originalHeight;
    }
}
