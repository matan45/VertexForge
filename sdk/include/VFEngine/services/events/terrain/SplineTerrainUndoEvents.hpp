#pragma once

#include "../EventTypes.hpp"
#include "../../utilities/terrain/TerrainTypes.hpp"
#include "../../utilities/terrain/TerrainWeightMap.hpp"

#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

// VK-1621. The weight-map snapshot pair, kept OUT of SplineTerrainEvents.hpp on purpose.
//
// terrain::TileWeightMapData is a VF_TERRAIN_API class, and the Editor does not link Terrain
// (premake5.lua:90-103) — it only ever includes header-only terrain types. SplineTerrainEvents.hpp
// is included Editor-side by the road generator, so putting a dllimport class in it would create a
// link dependency that does not exist today. Only Services code includes this header.
namespace events::splineTerrain
{
    using SplineWeightSnapshot =
        std::unordered_map<::terrain::TileCoord, ::terrain::TileWeightMapData, ::terrain::TileCoordHash>;

    // Paint used to record nothing (SplineTerrainServiceImpl stored a SplineData only in the
    // sculpt branch), so a painted spline was unrevertable. These mirror the height snapshot pair
    // so a combined sculpt+paint+mesh apply can be undone as one entry.
    //
    // The whole per-tile weight map is captured — channels, palette indices and resolution
    // together — because WeightBrushApplicator can evict a channel and renormalize the tile, so
    // the forward operation is not invertible from the brush params alone.
    struct GetSplineOriginalWeightsQuery : IQuery<SplineWeightSnapshot>
    {
        std::vector<glm::vec3> splineSamples;
        float totalHalfWidth = 0.0f;

        std::string_view getName() const override { return "GetSplineOriginalWeights"; }
    };

    struct RestoreSplineWeightsCommand : ICommand<>
    {
        SplineWeightSnapshot originalWeights;

        std::string_view getName() const override { return "RestoreSplineWeights"; }
    };
}
