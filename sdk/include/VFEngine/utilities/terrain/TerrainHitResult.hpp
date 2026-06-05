#pragma once

#include <glm/glm.hpp>

namespace terrain
{
    struct TerrainHitResult
    {
        glm::vec3 position{0.0f};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        bool hit = false;
    };
}
