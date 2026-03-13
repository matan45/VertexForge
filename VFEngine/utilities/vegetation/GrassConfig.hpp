#pragma once
#include <glm/glm.hpp>

namespace vegetation
{
    struct GrassRenderConfig
    {
        glm::vec4 baseColor{0.1f, 0.4f, 0.05f, 1.0f};
        glm::vec4 tipColor{0.2f, 0.6f, 0.1f, 1.0f};
        float heightMin = 0.3f;
        float heightMax = 0.8f;
        float widthMin = 0.02f;
        float widthMax = 0.05f;
        float windStrength = 1.0f;
        float slopeLimit = 0.7f;
        float fadeStartDistance = 80.0f;
        float fadeEndDistance = 120.0f;
        float densityMultiplier = 1.0f;

        glm::vec3 windDirection{1.0f, 0.0f, 0.0f};
        float windSpeed = 1.0f;
        float gustStrength = 0.3f;
        float gustFrequency = 0.5f;
    };
}
