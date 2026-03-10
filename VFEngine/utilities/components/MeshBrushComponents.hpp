#pragma once

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

namespace components
{
    struct MeshBrushInstanceComponent
    {
        uint32_t brushGroupId = 0;
        uint32_t paletteIndex = 0;
        glm::vec3 surfaceNormal{0.0f, 1.0f, 0.0f};
    };

    struct BrushInstance
    {
        uint64_t id = 0;
        glm::mat4 transform{1.0f};
        glm::vec3 worldPosition{0.0f}; // For spatial grid queries
        bool active = true;
    };

    struct MeshBrushBatchComponent
    {
        std::vector<BrushInstance> instances;
        bool dirty = true; // Set when instances added/removed, cleared after frame
    };
}
