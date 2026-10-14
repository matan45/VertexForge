#include "DayNightCycleController.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace render::atmosphere
{
    void DayNightCycleController::tick(float deltaTime, AtmosphereSettings& settings)
    {
        if (!settings.dayNightEnabled)
            return;

        // Advance time: 1 real-time minute per game-hour at cycleSpeed=1
        settings.timeOfDay += settings.cycleSpeed * deltaTime * (1.0f / 60.0f);
        settings.timeOfDay = std::fmod(settings.timeOfDay, 24.0f);
        if (settings.timeOfDay < 0.0f)
            settings.timeOfDay += 24.0f;

        const DayNightAngles angles = dayNightAngles(settings.timeOfDay, settings.moonPhaseOffset);
        settings.sunElevation = angles.sunElevation;
        settings.sunAzimuth = angles.sunAzimuth;
        settings.moonElevation = angles.moonElevation;
        settings.moonAzimuth = angles.moonAzimuth;
    }
}
