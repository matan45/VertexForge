#include "FlattenBrush.hpp"
#include "../TerrainTile.hpp"
#include "../BrushSampler.hpp"
#include <algorithm>

namespace terrain::brushes
{
    void FlattenBrush::apply(TerrainTile& tile, const BrushContext& context)
    {
        const auto& params = context.params;

        BrushSampler::forEachInfluencedVertex(tile, context,
            [&](const BrushVertexInfo& v)
            {
                float blendFactor = v.influence * params.strength * context.deltaTime;
                blendFactor = std::clamp(blendFactor, 0.0f, 1.0f);

                float current = tile.heightData[v.index];
                tile.heightData[v.index] = std::clamp(
                    current + (context.targetHeight - current) * blendFactor,
                    tile.config.minHeight,
                    tile.config.maxHeight);
            });
    }
}
