#pragma once
#include <glm/glm.hpp>

namespace vegetation
{
    struct GrassRenderConfig
    {
        glm::vec4 baseColor{0.1f, 0.4f, 0.05f, 1.0f};    // Root color
        glm::vec4 tipColor{0.2f, 0.6f, 0.1f, 1.0f};       // Tip color
        float heightMin = 0.3f;                               // Min blade height
        float heightMax = 0.8f;                               // Max blade height
        float widthMin = 0.02f;                               // Min blade width
        float widthMax = 0.05f;                               // Max blade width
        float windStrength = 1.0f;                             // Wind influence multiplier
        float slopeLimit = 0.7f;                               // Max slope for grass (dot product with up)
        float fadeStartDistance = 80.0f;                        // Start fading out
        float fadeEndDistance = 120.0f;                         // Fully faded out
        float densityMultiplier = 1.0f;                        // Global density scale

        // Wind parameters
        glm::vec3 windDirection{1.0f, 0.0f, 0.0f};            // Wind direction (normalized)
        float windSpeed = 1.0f;                                // Wind speed
        float gustStrength = 0.3f;                             // Gust intensity [0,1]
        float gustFrequency = 0.5f;                            // Gusts per second
    };
}
