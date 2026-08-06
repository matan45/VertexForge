#include "HeightBrushApplicator.hpp"
#include "BrushFalloff.hpp"
#include <algorithm>
#include <cmath>

namespace terrain
{
    bool HeightBrushApplicator::apply(std::vector<float>& heightData, const ApplyParams& params)
    {
        // A streamed-out or not-yet-loaded tile has an empty (or short) plane. Bail rather than
        // index it -- the caller cannot always tell, because ensureHeightsLoaded is file I/O it
        // deliberately does not perform on the script task.
        const size_t required = static_cast<size_t>(params.verticesPerSide) * params.verticesPerSide;
        if (params.verticesPerSide == 0 || heightData.size() < required)
            return false;

        // The shader divides by brushRadius unguarded because BrushParams::validate() floors it at
        // 0.1; a script native has no such guarantee.
        if (!(params.brushRadius > 0.0f))
            return false;

        bool modified = false;

        for (uint32_t z = 0; z < params.verticesPerSide; ++z)
        {
            for (uint32_t x = 0; x < params.verticesPerSide; ++x)
            {
                glm::vec2 vertexWorldPos = params.tileWorldOrigin
                    + glm::vec2(static_cast<float>(x), static_cast<float>(z)) * params.vertexSpacing;

                float dist = computeNormalizedDistance(
                    vertexWorldPos, params.brushCenter, params.brushRadius, params.shape);

                // Exclusive rim, matching brush_compute.glsl and both sibling applicators: a vertex
                // sitting exactly on the brush edge must not move.
                if (dist >= 1.0f)
                    continue;

                float influence = applyFalloff(dist, params.falloff);

                size_t idx = static_cast<size_t>(z) * params.verticesPerSide + x;
                float currentHeight = heightData[idx];
                float newHeight = currentHeight;

                switch (params.mode)
                {
                    case HeightEditMode::Add:
                        newHeight = currentHeight + params.amount * influence;
                        break;

                    case HeightEditMode::Set:
                        newHeight = glm::mix(currentHeight, params.amount,
                                             std::clamp(influence, 0.0f, 1.0f));
                        break;
                }

                // Clamp last, exactly as the shader's single trailing clamp does, so a run of edits
                // cannot walk the surface outside the tile's authored height range.
                newHeight = std::clamp(newHeight, params.minHeight, params.maxHeight);

                if (newHeight != currentHeight)
                {
                    heightData[idx] = newHeight;
                    modified = true;
                }
            }
        }

        return modified;
    }

    float HeightBrushApplicator::computeNormalizedDistance(
        const glm::vec2& sampleWorldPos,
        const glm::vec2& brushCenter,
        float brushRadius,
        BrushShape shape)
    {
        glm::vec2 delta = sampleWorldPos - brushCenter;

        if (shape == BrushShape::Circle)
        {
            return glm::length(delta) / brushRadius;
        }
        else
        {
            return std::max(std::abs(delta.x), std::abs(delta.y)) / brushRadius;
        }
    }

    // Must match GPU shader brush_compute.glsl and the Weight/Hole applicators
    float HeightBrushApplicator::applyFalloff(float t, BrushFalloff falloff)
    {
        return terrain::applyFalloff(t, falloff);
    }
}
