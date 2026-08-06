#include "SplineCorridorDeform.hpp"

#include "BrushSampler.hpp"
#include "SegmentCorridor.hpp"

#include <limits>
#include <unordered_set>

namespace terrain
{
    bool applySplineCorridorToTile(
        const TileCoord& coord,
        const TerrainTileConfig& config,
        const std::vector<glm::vec3>& splineSamples,
        const SplineCorridorParams& params,
        const std::vector<float>& in,
        std::vector<float>& out)
    {
        const uint32_t vertCount = config.getVertexCount();
        const size_t expected = static_cast<size_t>(vertCount) * vertCount;

        if (in.size() != expected)
            return false;

        if (out.size() != expected)
            out.resize(expected);

        // Total write, always: every early-out below still leaves `out` a faithful copy of `in`.
        out = in;

        if (splineSamples.size() < 2)
            return false;

        const float totalHalfWidth = params.corridorWidth + params.falloffWidth;
        const float halfCorridor = params.corridorWidth;
        const float vertSpacing = config.getVertexSpacing();
        const float tileSize = config.worldTileSize;

        const glm::vec2 tileOrigin(
            static_cast<float>(coord.x) * tileSize,
            static_cast<float>(coord.z) * tileSize);

        // Filter segments that overlap this tile's AABB. Pure optimization, kept verbatim: a
        // segment whose bounds expanded by totalHalfWidth miss the tile box cannot hold any tile
        // vertex within totalHalfWidth of it, and such vertices are skipped by the distance test
        // below anyway -- so culling can never change which segment wins the minPerpDist race.
        std::vector<size_t> relevantSegments;
        for (size_t i = 0; i + 1 < splineSamples.size(); ++i)
        {
            const glm::vec2 segStart(splineSamples[i].x, splineSamples[i].z);
            const glm::vec2 segEnd(splineSamples[i + 1].x, splineSamples[i + 1].z);
            const glm::vec2 segMin = glm::min(segStart, segEnd) - glm::vec2(totalHalfWidth);
            const glm::vec2 segMax = glm::max(segStart, segEnd) + glm::vec2(totalHalfWidth);

            if (segMax.x >= tileOrigin.x && segMin.x <= tileOrigin.x + tileSize &&
                segMax.y >= tileOrigin.y && segMin.y <= tileOrigin.y + tileSize)
            {
                relevantSegments.push_back(i);
            }
        }

        if (relevantSegments.empty())
            return false;

        bool tileModified = false;

        for (uint32_t z = 0; z < vertCount; ++z)
        {
            for (uint32_t x = 0; x < vertCount; ++x)
            {
                const glm::vec2 vertPos =
                    tileOrigin + glm::vec2(static_cast<float>(x), static_cast<float>(z)) * vertSpacing;

                // Closest point on any relevant segment.
                float minPerpDist = std::numeric_limits<float>::max();
                float bestTargetHeight = 0.0f;

                for (const size_t segIdx : relevantSegments)
                {
                    const glm::vec2 segStart(splineSamples[segIdx].x, splineSamples[segIdx].z);
                    const glm::vec2 segEnd(splineSamples[segIdx + 1].x, splineSamples[segIdx + 1].z);
                    const glm::vec2 segDir = segEnd - segStart;
                    const float segLen = glm::length(segDir);
                    if (segLen < 0.001f)
                        continue;

                    const SegmentProjection projection =
                        projectOntoSegment(vertPos, segStart, segEnd, segLen);

                    if (projection.distance < minPerpDist)
                    {
                        minPerpDist = projection.distance;
                        bestTargetHeight = glm::mix(
                                               splineSamples[segIdx].y,
                                               splineSamples[segIdx + 1].y, projection.t)
                            + params.embankmentHeight;
                    }
                }

                if (minPerpDist > totalHalfWidth)
                    continue;

                const size_t idx = static_cast<size_t>(z) * vertCount + x;
                const float currentHeight = in[idx];

                const float blend = corridorBlend(minPerpDist, halfCorridor, params.falloffWidth);

                out[idx] = glm::mix(currentHeight, bestTargetHeight, blend);
                tileModified = true;
            }
        }

        return tileModified;
    }

    std::vector<TileCoord> splineCorridorAffectedTiles(
        const std::vector<glm::vec3>& splineSamples,
        float totalHalfWidth,
        float worldTileSize)
    {
        std::vector<TileCoord> result;
        if (splineSamples.size() < 2)
            return result;

        std::unordered_set<TileCoord, TileCoordHash> seen;
        for (size_t i = 0; i + 1 < splineSamples.size(); ++i)
        {
            const glm::vec2 s(splineSamples[i].x, splineSamples[i].z);
            const glm::vec2 e(splineSamples[i + 1].x, splineSamples[i + 1].z);
            for (const auto& coord :
                 BrushSampler::getAffectedTilesForSegment(s, e, totalHalfWidth, worldTileSize))
            {
                if (seen.insert(coord).second)
                    result.push_back(coord);
            }
        }

        return result;
    }
}
