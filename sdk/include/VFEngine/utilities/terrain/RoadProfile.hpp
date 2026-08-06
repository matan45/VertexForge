#pragma once

#include <cstdint>
#include <vector>

namespace terrain
{
    // VK-1621 — the authored cross-section of a road, kept free of any mesh/resource types so it
    // can sit inside SplineParams (and therefore inside spline events and the serialized road
    // component) without dragging resource::Vertex along.

    // One vertex column of the cross-section. Columns are ordered by ASCENDING `offset`; the mesh
    // builder relies on that ordering for triangle winding.
    struct RoadProfileColumn
    {
        float offset = 0.0f;       // signed metres from the centreline, along the ring's right vector
        float u = 0.0f;            // authored U, multiplied by RoadProfile::uvTilingU
        float heightOffset = 0.0f; // metres relative to the road centre height (crown, kerb, shoulder drop)
        float terrainBlend = 0.0f; // 0 = road surface, 1 = sit on the terrain (shoulder feather)
    };

    struct RoadProfile
    {
        std::vector<RoadProfileColumn> columns;

        // Metres between cross-sections. The builder resamples the spline to an exactly uniform
        // arc-length spacing, so this is a real world-space density, not a curve-parameter step.
        float ringSpacing = 1.0f;

        float uvTilingU = 1.0f; // multiplies the authored per-column U
        float uvTilingV = 8.0f; // metres of road per V repeat (physical, tessellation-independent)

        // Lift above the terrain surface. Must exceed the residual between the flattened corridor
        // and the terrain's own triangulated surface, or the road z-fights / clips.
        float zOffset = 0.05f;

        // true  -> every column on a ring shares the centre height: a perfectly flat cross-section
        //          that ignores terrain micro-noise across the width (paved road on a sculpted corridor).
        // false -> each column samples the terrain under it (dirt path laid on untouched terrain).
        bool flatCrossSection = true;

        // A chunk must cover at least this fraction of a tile's width, measured in ARC LENGTH,
        // before the road is allowed to split again. A ring-count threshold is not enough: a curve
        // that merely grazes a tile boundary (Catmull-Rom overshoot puts a nominally z=0 road at
        // z=-0.1 for a stretch) flips tile for many rings and would otherwise shear the road into
        // slivers. Larger values mean fewer, looser chunks; 0 splits on every tile change.
        float chunkMinTileFraction = 0.5f;
    };

    // A 4-column road: shoulder, left edge, right edge, shoulder. The shoulders feather onto the
    // terrain (terrainBlend = 1) so the road has no floating lip, and their U runs slightly outside
    // [0,1] so an authored road texture can put a verge there.
    [[nodiscard]] inline RoadProfile makeDefaultRoadProfile(float halfWidth = 4.0f,
                                                            float shoulderWidth = 1.5f,
                                                            float shoulderDrop = 0.15f)
    {
        RoadProfile profile;
        const float shoulderU = (halfWidth > 0.0f) ? (shoulderWidth / (2.0f * halfWidth)) : 0.0f;
        profile.columns = {
            {-halfWidth - shoulderWidth, -shoulderU, -shoulderDrop, 1.0f},
            {-halfWidth, 0.0f, 0.0f, 0.0f},
            {halfWidth, 1.0f, 0.0f, 0.0f},
            {halfWidth + shoulderWidth, 1.0f + shoulderU, -shoulderDrop, 1.0f},
        };
        return profile;
    }
}
