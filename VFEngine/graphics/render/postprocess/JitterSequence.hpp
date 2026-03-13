#pragma once

#include <glm/glm.hpp>

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
            return {halton(index + 1, 2), halton(index + 1, 3)};
        }

        static glm::mat4 applyJitter(const glm::mat4& projection, const glm::vec2& jitterPixels,
                                      uint32_t width, uint32_t height)
        {
            glm::mat4 jittered = projection;
            jittered[2][0] += jitterPixels.x / static_cast<float>(width);
            jittered[2][1] += jitterPixels.y / static_cast<float>(height);
            return jittered;
        }
    };
}
