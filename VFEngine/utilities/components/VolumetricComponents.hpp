#pragma once
#include <glm/glm.hpp>
#include <cstdint>

namespace components
{
    struct VolumetricNavVolumeComponent
    {
        glm::vec3 boundsMin{-50.0f, -10.0f, -50.0f};
        glm::vec3 boundsMax{50.0f, 40.0f, 50.0f};
        float voxelSize = 1.0f;
        uint8_t connectivity = 26;   // 6 or 26
        float agentClearance = 0.5f;

        // Runtime (not serialized)
        bool isBaked = false;
    };

    struct VolumetricAgentComponent
    {
        float radius = 0.5f;
        float maxSpeed = 3.5f;
        float maxAcceleration = 8.0f;
        float stoppingDistance = 1.0f;

        // Runtime (not serialized)
        bool isActive = false;
    };
}
