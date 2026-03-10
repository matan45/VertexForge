#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace components
{
    struct MeshBrushInstanceComponent
    {
        uint32_t brushGroupId = 0;
        uint32_t paletteIndex = 0;
        glm::vec3 surfaceNormal{0.0f, 1.0f, 0.0f};
    };
}
