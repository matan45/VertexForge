#pragma once
#include "TerrainExport.hpp"

#include "TerrainTypes.hpp"
#include <vector>
#include <glm/glm.hpp>

namespace terrain
{
    // VK-1645. The height half of a sculpt spline, lifted out of the ApplySplineDeformCommand
    // handler lambda so it can run as a reserved height layer's evaluator and be unit-tested
    // without a TerrainService, a TerrainGrid, a dispatcher or a GPU.
    //
    // Only the three fields the height corridor actually reads. Deliberately NOT SplineParams:
    // that struct drags in RoadProfile, two std::strings and the paint/mesh op flags, none of
    // which a height evaluator may depend on.
    struct SplineCorridorParams
    {
        float corridorWidth = 5.0f;    // flat area half-width
        float falloffWidth = 3.0f;     // smooth blend distance outside the corridor
        float embankmentHeight = 0.0f; // raise above the spline height
    };

    // VK-1646. Everything a corridor evaluator needs, as plain data — the serializable twin of the
    // lambda captures the spline-apply handler used to build by hand.
    //
    // A HeightLayerRecord's `eval` is a std::function and cannot be written to disk, so the
    // sidecar persists this instead and rebuilds the callable on load. Keeping the two in one
    // struct is what makes "what was saved" and "what runs" the same thing by construction.
    struct SplineCorridorLayerParams
    {
        SplineCorridorParams corridor;
        std::vector<glm::vec3> samples; // world XZ polyline, .y = target height
    };

    // Writes the corridor into `out` reading from `in`. `in` and `out` MUST NOT alias, and
    // `out` is fully populated on every path (unaffected vertices are copied straight from
    // `in`) -- that total-write contract is what lets composeTileHeights ping-pong buffers
    // without pre-seeding.
    //
    // Bit-identical to the in-place loop it replaces: that loop read heightData[idx] and wrote
    // heightData[idx] with each index visited exactly once, so reading the pre-pass value from a
    // separate buffer yields the same float for every vertex. test_terrain_height_compose.cpp
    // pins that equivalence against a transcribed copy of the original.
    //
    // Returns true when at least one vertex was deformed.
    VF_TERRAIN_API bool applySplineCorridorToTile(
        const TileCoord& coord,
        const TerrainTileConfig& config,
        const std::vector<glm::vec3>& splineSamples, // world XZ polyline, .y = target height
        const SplineCorridorParams& params,
        const std::vector<float>& in,
        std::vector<float>& out);

    // Every tile the corridor plus its falloff can touch. Union over consecutive sample pairs,
    // deduplicated. Order is unspecified -- callers that need determinism must sort.
    VF_TERRAIN_API std::vector<TileCoord> splineCorridorAffectedTiles(
        const std::vector<glm::vec3>& splineSamples,
        float totalHalfWidth,
        float worldTileSize);
}
