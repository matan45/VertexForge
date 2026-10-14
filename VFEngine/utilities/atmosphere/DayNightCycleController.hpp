#pragma once
#include "AtmosphereSettings.hpp"
#include <cmath>
#include <glm/gtc/constants.hpp>

namespace render::atmosphere
{
    // Sun and moon angles (degrees) for a time of day, as the day-night cycle places them.
    struct DayNightAngles
    {
        float sunAzimuth = 0.0f;
        float sunElevation = 0.0f;
        float moonAzimuth = 0.0f;
        float moonElevation = 0.0f;
    };

    // VK-1654: the day-night cycle's angle formulas, shared by DayNightCycleController::tick
    // and by tools that set an absolute time of day in edit mode (where the cycle never ticks).
    // timeOfDay is in hours; tick() passes it already wrapped to [0, 24).
    [[nodiscard]] inline DayNightAngles dayNightAngles(float timeOfDay, float moonPhaseOffset)
    {
        // Sun path: elevation = 90*sin((t-6)/24 * 2*PI)
        // 6:00=sunrise(0), 12:00=zenith(90), 18:00=sunset(0), 0:00=nadir(-90)
        const float sunElevation = 90.0f * std::sin((timeOfDay - 6.0f) / 24.0f * glm::two_pi<float>());
        const float sunAzimuth = std::fmod(timeOfDay / 24.0f * 360.0f + 90.0f, 360.0f);

        // Moon: opposite to sun, with optional phase offset
        DayNightAngles angles;
        angles.sunAzimuth = sunAzimuth;
        angles.sunElevation = sunElevation;
        angles.moonElevation = -sunElevation;
        angles.moonAzimuth = std::fmod(sunAzimuth + 180.0f + moonPhaseOffset * 360.0f, 360.0f);
        return angles;
    }

    class DayNightCycleController
    {
    public:
        void tick(float deltaTime, AtmosphereSettings& settings);
    };
}
