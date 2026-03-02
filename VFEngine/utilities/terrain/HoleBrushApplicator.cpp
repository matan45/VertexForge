#include "HoleBrushApplicator.hpp"
#include <algorithm>
#include <cmath>

namespace terrain
{
    bool HoleBrushApplicator::apply(std::vector<uint8_t>& holeMask, const ApplyParams& params)
    {
        if (holeMask.empty())
            return false;

        bool modified = false;

        // Iterate per-quad: check center of each quad against brush
        for (uint32_t z = 0; z < params.quadsPerSide; ++z)
        {
            for (uint32_t x = 0; x < params.quadsPerSide; ++x)
            {
                // Quad center is offset by half a vertex spacing from each corner vertex
                glm::vec2 quadCenter = params.tileWorldOrigin
                    + glm::vec2(static_cast<float>(x) + 0.5f, static_cast<float>(z) + 0.5f)
                    * params.vertexSpacing;

                float dist = computeNormalizedDistance(
                    quadCenter, params.brushCenter, params.brushRadius, params.shape);

                if (dist >= 1.0f)
                    continue;

                float falloffValue = applyFalloff(dist, params.falloff);

                // Binary threshold: if influence >= 0.5, toggle the hole state
                if (falloffValue < 0.5f)
                    continue;

                size_t idx = static_cast<size_t>(z) * params.quadsPerSide + x;
                uint8_t newValue = params.erase ? 0 : 1;

                if (holeMask[idx] != newValue)
                {
                    holeMask[idx] = newValue;
                    modified = true;
                }
            }
        }

        return modified;
    }

    float HoleBrushApplicator::computeNormalizedDistance(
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

    // Must match GPU shader brush_influence.glsl and WeightBrushApplicator
    float HoleBrushApplicator::applyFalloff(float t, BrushFalloff falloff)
    {
        switch (falloff)
        {
            case BrushFalloff::Constant: return 1.0f;
            case BrushFalloff::Linear:   return 1.0f - t;
            case BrushFalloff::Smooth:   return 1.0f - t * t * (3.0f - 2.0f * t);
            case BrushFalloff::Sharp:    return 1.0f - t * t;
            default: return 0.0f;
        }
    }
}
