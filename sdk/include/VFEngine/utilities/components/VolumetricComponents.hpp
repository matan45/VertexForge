#pragma once
#include <glm/glm.hpp>
#include <cstdint>

namespace components
{
    struct VolumetricNavVolumeComponent
    {
        glm::vec3 boundsMin{-50.0f};
        glm::vec3 boundsMax{50.0f};
        float voxelSize = 1.0f;
        uint8_t connectivity = 26;   // 6 or 26
        float agentClearance = 0.5f;
        uint16_t physicsLayerMask = 0xFFFF;

        // Runtime (not serialized)
        bool isBaked = false;
        bool isBaking = false;
    };

    struct VolumetricAgentComponent
    {
        float radius = 0.5f;
        float maxSpeed = 5.0f;
        float maxAcceleration = 10.0f;
        float stoppingDistance = 0.5f;
        float arrivalDistance = 1.0f;

        // Runtime (not serialized) - path state is managed by VolumetricAgentManager
        bool isActive = false;
    };
}
