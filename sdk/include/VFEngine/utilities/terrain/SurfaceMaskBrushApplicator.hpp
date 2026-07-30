#pragma once

#include "BrushFalloff.hpp"
#include "BrushTypes.hpp"
#include "TerrainSurfaceMaskAsset.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>

// VK-1614 brush for the world-anchored wetness/snow mask.
//
// Header-only inline (the TerrainLayerVisibility.hpp / BrushFalloff.hpp pattern) so the CPU-only
// Tests project can cover the arithmetic directly.
//
// The important structural difference from WeightBrushApplicator: that one paints ONE TILE at a time
// into a per-tile grid whose resolution follows the tile's vertex count, and it renormalises across
// the 8 splat channels. This paints a SINGLE world-anchored image with independent channels — no
// tiles, no palette indirection, no normalisation. Which is also why the mask could not simply
// borrow a ninth weight-map channel: TileWeightMapData::normalizeAt divides every channel at a texel
// by their sum, so a mask channel would be rescaled by every unrelated stroke.
//
// It shares the falloff curve and the shape rule with the weight brush on purpose, so the viewport's
// brush ring preview stays honest for both.

namespace terrain
{
    // What a stroke is writing. Layers is the pre-existing weight-map behaviour and does not reach
    // this applicator; it is in the same enum because it is one selector in the Paint tool.
    enum class PaintTarget : uint8_t
    {
        Layers = 0,
        Wetness = 1,
        Snow = 2
    };

    [[nodiscard]] inline uint32_t paintTargetToMaskChannel(PaintTarget target)
    {
        return target == PaintTarget::Snow ? SURFACE_MASK_SNOW_CHANNEL : SURFACE_MASK_WETNESS_CHANNEL;
    }

    class SurfaceMaskBrushApplicator
    {
    public:
        struct ApplyParams
        {
            glm::vec2 brushCenter{0.0f};   // World XZ
            glm::vec4 maskWorldRect{0.0f}; // minX, minZ, maxX, maxZ — the AUTHORED rect
            float brushRadius = 1.0f;
            float brushStrength = 1.0f;
            float brushOpacity = 1.0f;
            BrushFalloff falloff = BrushFalloff::Smooth;
            BrushShape shape = BrushShape::Circle;
            uint32_t channel = SURFACE_MASK_WETNESS_CHANNEL;
            float deltaTime = 0.016f;
            bool invert = false; // erase instead of paint
        };

        // The texel rectangle a stroke touched, so the caller can bound an upload. Empty when nothing
        // was modified (minX > maxX).
        struct DirtyRect
        {
            uint32_t minX = 1;
            uint32_t minZ = 1;
            uint32_t maxX = 0;
            uint32_t maxZ = 0;

            [[nodiscard]] bool isEmpty() const { return minX > maxX || minZ > maxZ; }

            void add(uint32_t x, uint32_t z)
            {
                if (isEmpty())
                {
                    minX = maxX = x;
                    minZ = maxZ = z;
                    return;
                }
                minX = (std::min)(minX, x);
                maxX = (std::max)(maxX, x);
                minZ = (std::min)(minZ, z);
                maxZ = (std::max)(maxZ, z);
            }
        };

        // Returns the texels modified. Only the brush's world-space bounding box is visited, so cost
        // scales with brush size rather than mask size — which matters because the mask is one image
        // for the whole terrain (a 1024^2 full scan per stroke would be a million texels).
        static DirtyRect apply(TerrainSurfaceMaskData& mask, const ApplyParams& params)
        {
            DirtyRect dirty;
            if (!mask.isValid() || params.channel >= SURFACE_MASK_CHANNELS)
                return dirty;

            const float spanX = params.maskWorldRect.z - params.maskWorldRect.x;
            const float spanZ = params.maskWorldRect.w - params.maskWorldRect.y;
            if (spanX <= 1e-4f || spanZ <= 1e-4f || params.brushRadius <= 0.0f)
                return dirty;

            // World -> texel. The mask spans [min, max] over (width - 1) intervals so texel 0 sits on
            // minX and texel (width - 1) on maxX, matching the clamp-to-edge bilinear read in
            // TerrainSurfaceMaskData::sample and on the GPU.
            const float texelsPerWorldX = static_cast<float>(mask.width - 1) / spanX;
            const float texelsPerWorldZ = static_cast<float>(mask.height - 1) / spanZ;

            const auto toTexelX = [&](float worldX) {
                return (worldX - params.maskWorldRect.x) * texelsPerWorldX;
            };
            const auto toTexelZ = [&](float worldZ) {
                return (worldZ - params.maskWorldRect.y) * texelsPerWorldZ;
            };

            const float fx0 = toTexelX(params.brushCenter.x - params.brushRadius);
            const float fx1 = toTexelX(params.brushCenter.x + params.brushRadius);
            const float fz0 = toTexelZ(params.brushCenter.y - params.brushRadius);
            const float fz1 = toTexelZ(params.brushCenter.y + params.brushRadius);

            // Whole brush outside the mask rect: nothing to do, and no clamped smear at the border.
            if (fx1 < 0.0f || fz1 < 0.0f
                || fx0 > static_cast<float>(mask.width - 1)
                || fz0 > static_cast<float>(mask.height - 1))
                return dirty;

            const auto clampX = [&](float v) {
                return static_cast<uint32_t>(std::clamp(v, 0.0f, static_cast<float>(mask.width - 1)));
            };
            const auto clampZ = [&](float v) {
                return static_cast<uint32_t>(std::clamp(v, 0.0f, static_cast<float>(mask.height - 1)));
            };

            const uint32_t x0 = clampX(std::floor(fx0));
            const uint32_t x1 = clampX(std::ceil(fx1));
            const uint32_t z0 = clampZ(std::floor(fz0));
            const uint32_t z1 = clampZ(std::ceil(fz1));

            const float worldPerTexelX = spanX / static_cast<float>(mask.width - 1);
            const float worldPerTexelZ = spanZ / static_cast<float>(mask.height - 1);

            for (uint32_t z = z0; z <= z1; ++z)
            {
                const float worldZ = params.maskWorldRect.y + static_cast<float>(z) * worldPerTexelZ;
                for (uint32_t x = x0; x <= x1; ++x)
                {
                    const float worldX = params.maskWorldRect.x + static_cast<float>(x) * worldPerTexelX;

                    const float dist = normalizedDistance(glm::vec2(worldX, worldZ), params.brushCenter,
                                                          params.brushRadius, params.shape);
                    if (dist >= 1.0f)
                        continue;

                    float influence = applyFalloff(dist, params.falloff)
                                    * params.brushStrength * params.brushOpacity * params.deltaTime;
                    influence = std::clamp(influence, 0.0f, 1.0f);
                    if (influence <= 0.0001f)
                        continue;

                    const float current = mask.getChannel(x, z, params.channel);
                    // Same accumulate/decay shape as WeightBrushApplicator's paintLayer/eraseLayer,
                    // minus the cross-channel renormalisation, which does not apply here: wetness and
                    // snow are independent signals, not a partition of unity.
                    const float target = params.invert
                        ? (std::max)(current - influence, 0.0f)
                        : (std::min)(current + influence, 1.0f);
                    if (std::abs(target - current) <= 0.0f)
                        continue;

                    mask.setChannel(x, z, params.channel, target);
                    dirty.add(x, z);
                }
            }

            return dirty;
        }

    private:
        // Mirrors WeightBrushApplicator::computeNormalizedDistance so the two brushes agree on shape.
        // BrushShape::Square is Chebyshev distance — every consumer in the repo implements it as the
        // `else` of Circle.
        [[nodiscard]] static float normalizedDistance(const glm::vec2& texelWorldPos,
                                                      const glm::vec2& brushCenter,
                                                      float brushRadius, BrushShape shape)
        {
            const glm::vec2 delta = texelWorldPos - brushCenter;
            if (shape == BrushShape::Circle)
                return glm::length(delta) / brushRadius;
            return (std::max)(std::abs(delta.x), std::abs(delta.y)) / brushRadius;
        }
    };
}
