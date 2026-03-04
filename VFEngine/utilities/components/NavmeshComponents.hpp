#pragma once
#include <glm/glm.hpp>
#include <string>

namespace components
{
    struct NavmeshComponent
    {
        std::string navmeshPath;
    };

    struct NavmeshAgentComponent
    {
        float radius = 0.3f;
        float height = 2.0f;
        float maxSpeed = 3.5f;
        float maxAcceleration = 8.0f;
        float stoppingDistance = 0.1f;

        // Avoidance
        uint8_t avoidanceQuality = 3;       // 0-3, higher = better but slower
        float separationWeight = 2.0f;

        // Runtime state (not serialized)
        bool isActive = false;
        int crowdAgentIndex = -1;
    };
}
