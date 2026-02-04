#include "RaiseLowerBrush.hpp"
#include "../TerrainTile.hpp"
#include "../BrushSampler.hpp"
#include <algorithm>

namespace terrain::brushes
{
    void RaiseLowerBrush::apply(TerrainTile& tile, const BrushContext& context)
    {
        float direction = context.invert ? -1.0f : 1.0f;
        const auto& params = context.params;

        BrushSampler::forEachInfluencedVertex(tile, context,
            [&](const BrushVertexInfo& v)
            {
                float delta_h = direction * v.influence * params.strength * context.deltaTime;
                tile.heightData[v.index] = std::clamp(
                    tile.heightData[v.index] + delta_h,
                    tile.config.minHeight,
                    tile.config.maxHeight);
            });
    }
}
