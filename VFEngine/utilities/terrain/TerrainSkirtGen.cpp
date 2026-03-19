#include "TerrainTileGenerator.hpp"
#include <algorithm>

namespace terrain
{
    void TerrainTileGenerator::generateSkirts(
        std::vector<resource::Vertex>& vertices,
        std::vector<uint32_t>& indices,
        uint32_t lodLevel,
        float skirtDepth,
        const std::vector<uint8_t>& holeMask,
        uint32_t baseVertexCount) const
    {
        std::vector<resource::Vertex> mainVertices = vertices;

        SkirtEdgeParams params;
        params.mainVertices = &mainVertices;
        params.lodLevel = lodLevel;
        params.skirtDepth = skirtDepth;
        params.holeMask = &holeMask;
        params.baseVertexCount = baseVertexCount;

        params.edge = TileEdge::North;
        addSkirtEdge(vertices, indices, params);
        params.edge = TileEdge::East;
        addSkirtEdge(vertices, indices, params);
        params.edge = TileEdge::South;
        addSkirtEdge(vertices, indices, params);
        params.edge = TileEdge::West;
        addSkirtEdge(vertices, indices, params);
    }

    static std::vector<TerrainTileGenerator::SkirtEdgeVertex> collectEdgeVertices(
        TileEdge edge, uint32_t vertCount, uint32_t skipFactor, uint32_t baseVertexCount)
    {
        std::vector<TerrainTileGenerator::SkirtEdgeVertex> edgeVerts;
        edgeVerts.reserve(vertCount);

        for (uint32_t i = 0; i < vertCount; ++i)
        {
            TerrainTileGenerator::SkirtEdgeVertex ev{};
            switch (edge)
            {
            case TileEdge::North:
                ev.idx = (vertCount - 1) * vertCount + i;
                ev.baseX = i * skipFactor;
                ev.baseZ = (vertCount - 1) * skipFactor;
                break;
            case TileEdge::South:
                ev.idx = i;
                ev.baseX = i * skipFactor;
                ev.baseZ = 0;
                break;
            case TileEdge::East:
                ev.idx = i * vertCount + (vertCount - 1);
                ev.baseX = (vertCount - 1) * skipFactor;
                ev.baseZ = i * skipFactor;
                break;
            case TileEdge::West:
                ev.idx = i * vertCount;
                ev.baseX = 0;
                ev.baseZ = i * skipFactor;
                break;
            }
            ev.baseX = std::min(ev.baseX, baseVertexCount - 1);
            ev.baseZ = std::min(ev.baseZ, baseVertexCount - 1);
            edgeVerts.push_back(ev);
        }
        return edgeVerts;
    }

    static bool isEdgeSegmentHole(
        uint32_t segmentIdx, TileEdge edge, uint32_t skipFactor,
        uint32_t baseQuadCount, const std::vector<uint8_t>& holeMask)
    {
        if (holeMask.empty() || baseQuadCount == 0) return false;

        uint32_t quadX = 0, quadZ = 0;
        uint32_t baseSegIdx = segmentIdx * skipFactor;
        switch (edge)
        {
        case TileEdge::North:
            quadX = std::min(baseSegIdx, baseQuadCount - 1);
            quadZ = baseQuadCount - 1;
            break;
        case TileEdge::South:
            quadX = std::min(baseSegIdx, baseQuadCount - 1);
            quadZ = 0;
            break;
        case TileEdge::East:
            quadX = baseQuadCount - 1;
            quadZ = std::min(baseSegIdx, baseQuadCount - 1);
            break;
        case TileEdge::West:
            quadX = 0;
            quadZ = std::min(baseSegIdx, baseQuadCount - 1);
            break;
        }

        for (uint32_t k = 0; k < skipFactor; ++k)
        {
            uint32_t qx = quadX, qz = quadZ;
            if (edge == TileEdge::North || edge == TileEdge::South)
                qx = std::min(baseSegIdx + k, baseQuadCount - 1);
            else
                qz = std::min(baseSegIdx + k, baseQuadCount - 1);
            if (holeMask[static_cast<size_t>(qz) * baseQuadCount + qx])
                return true;
        }
        return false;
    }

    static void generateSkirtVertices(
        std::vector<resource::Vertex>& vertices,
        const std::vector<TerrainTileGenerator::SkirtEdgeVertex>& edgeVerts,
        const std::vector<resource::Vertex>& mainVertices,
        float skirtDepth)
    {
        for (const auto& ev : edgeVerts)
        {
            resource::Vertex skirtVertex = mainVertices[ev.idx];
            skirtVertex.position.y -= skirtDepth;
            vertices.push_back(skirtVertex);
        }
    }

    static void generateSkirtIndices(
        std::vector<uint32_t>& indices,
        const std::vector<TerrainTileGenerator::SkirtEdgeVertex>& edgeVerts,
        uint32_t skirtStartIndex, TileEdge edge,
        uint32_t skipFactor, uint32_t baseQuadCount,
        const std::vector<uint8_t>& holeMask)
    {
        for (size_t i = 0; i < edgeVerts.size() - 1; ++i)
        {
            if (isEdgeSegmentHole(static_cast<uint32_t>(i), edge, skipFactor, baseQuadCount, holeMask))
                continue;

            uint32_t topCurrent = edgeVerts[i].idx;
            uint32_t topNext = edgeVerts[i + 1].idx;
            uint32_t bottomCurrent = skirtStartIndex + static_cast<uint32_t>(i);
            uint32_t bottomNext = skirtStartIndex + static_cast<uint32_t>(i) + 1;

            // Winding depends on edge direction to maintain consistent facing
            switch (edge)
            {
            case TileEdge::North:
            case TileEdge::East:
                indices.push_back(topCurrent);
                indices.push_back(topNext);
                indices.push_back(bottomCurrent);

                indices.push_back(bottomCurrent);
                indices.push_back(topNext);
                indices.push_back(bottomNext);
                break;

            case TileEdge::South:
            case TileEdge::West:
                indices.push_back(topCurrent);
                indices.push_back(bottomCurrent);
                indices.push_back(topNext);

                indices.push_back(topNext);
                indices.push_back(bottomCurrent);
                indices.push_back(bottomNext);
                break;
            }
        }
    }

    void TerrainTileGenerator::addSkirtEdge(
        std::vector<resource::Vertex>& vertices,
        std::vector<uint32_t>& indices,
        const SkirtEdgeParams& params) const
    {
        uint32_t vertCount = getLODVertexCount(params.lodLevel);
        uint32_t skipFactor = getLODSkipFactor(params.lodLevel);

        auto edgeVerts = collectEdgeVertices(params.edge, vertCount, skipFactor, params.baseVertexCount);

        uint32_t baseQuadCount = (params.baseVertexCount > 0) ? params.baseVertexCount - 1 : 0;
        uint32_t skirtStartIndex = static_cast<uint32_t>(vertices.size());

        generateSkirtVertices(vertices, edgeVerts, *params.mainVertices, params.skirtDepth);
        generateSkirtIndices(indices, edgeVerts, skirtStartIndex, params.edge,
                             skipFactor, baseQuadCount,
                             params.holeMask ? *params.holeMask : std::vector<uint8_t>{});
    }
}
