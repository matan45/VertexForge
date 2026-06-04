#pragma once

#include <glm/glm.hpp>

namespace vegetation
{
    struct WindConfig
    {
        glm::vec3 direction{1.0f, 0.0f, 0.0f};  // Normalized wind direction
        float speed = 1.0f;                        // Wind speed multiplier
        float gustStrength = 0.3f;                 // Gust intensity [0,1]
        float gustFrequency = 0.5f;                // Gusts per second
        float turbulenceScale = 0.1f;              // Spatial turbulence scale
    };
}
