#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include "../weather/WeatherTypes.hpp"

namespace components
{
    enum class WeatherZoneShape : uint8_t
    {
        Sphere = 0,
        Box = 1
    };

    struct WeatherZoneComponent
    {
        WeatherZoneShape shape = WeatherZoneShape::Sphere;
        float radius = 20.0f;
        glm::vec3 halfExtents{10.0f};
        weather::WeatherState overrideState;
        int priority = 0;
        float falloffDistance = 5.0f;
        bool active = true;
        bool showDebugVolume = false;

        // Runtime (not serialized)
        bool isCameraInside = false;
        float currentBlendWeight = 0.0f;
    };
}
