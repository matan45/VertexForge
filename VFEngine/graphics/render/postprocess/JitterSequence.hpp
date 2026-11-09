#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace render::postprocess
{
    class JitterSequence
    {
    public:
        static float halton(uint32_t index, uint32_t base)
        {
            float result = 0.0f;
            float fraction = 1.0f / static_cast<float>(base);
            uint32_t i = index;
            while (i > 0)
            {
                result += fraction * static_cast<float>(i % base);
                i /= base;
                fraction /= static_cast<float>(base);
            }
            return result;
        }

        static glm::vec2 halton23(uint32_t index)
        {
            // +1 to skip the degenerate (0,0) sample at Halton index 0
            return {halton(index + 1, 2), halton(index + 1, 3)};
        }

        // VK-1662: Halton phase count from the DLSS SR Programming Guide §3.7.1.1, 8 * (display / render)^2:
        // DLAA 8, Quality 18, Balanced 24, Performance 32, Ultra Performance 72. Uses the area ratio (the per-axis
        // ratio squared for uniform scaling); the epsilon keeps an exact product such as 8 * 2.25 = 18 from rounding
        // up to 19 through floating-point noise.
        static uint32_t phaseCount(uint32_t renderWidth, uint32_t renderHeight,
                                   uint32_t displayWidth, uint32_t displayHeight)
        {
            constexpr uint32_t basePhaseCount = 8;
            if (renderWidth == 0 || renderHeight == 0)
                return basePhaseCount;

            const double areaRatio = (static_cast<double>(displayWidth) * static_cast<double>(displayHeight))
                                   / (static_cast<double>(renderWidth) * static_cast<double>(renderHeight));
            // (std::max): immune to a windows.h max macro in whichever TU includes this header.
            const double phases = (std::max)(0.0, std::ceil(static_cast<double>(basePhaseCount) * areaRatio - 1e-3));
            return (std::max)(basePhaseCount, static_cast<uint32_t>(phases));
        }

        // VK-1662: offsets the projection so the rendered image moves by +jitterPixels render pixels (+x right,
        // +y down: Vulkan NDC with a positive-height viewport). It is a clip-space translation,
        // ndc' = ndc + 2 * jitterPixels / extent, built from the projection's own w row, so it is exact for
        // perspective (w = -z) and orthographic (w = 1) projections alike. jitterPixels is exactly what DLSS receives
        // as jitterOffset (§3.7.3: "the jitter applied to the projection matrix", in render pixels, on the motion
        // vectors' axes). The old form added jitter / extent to [2][0]/[2][1]: for GLM's right-handed perspective
        // (w = -z) that moved the image by -jitter / 2 pixels, and for an ortho projection it was a depth shear.
        static glm::mat4 applyJitter(const glm::mat4& projection, const glm::vec2& jitterPixels,
                                      uint32_t width, uint32_t height)
        {
            if (width == 0 || height == 0)
                return projection;

            const glm::vec2 ndcOffset = 2.0f * jitterPixels
                                      / glm::vec2(static_cast<float>(width), static_cast<float>(height));
            glm::mat4 jittered = projection;
            for (glm::length_t column = 0; column < 4; ++column)
            {
                jittered[column][0] += ndcOffset.x * projection[column][3];
                jittered[column][1] += ndcOffset.y * projection[column][3];
            }
            return jittered;
        }
    };
}
