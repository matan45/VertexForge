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

        float t = settings.timeOfDay;

        // Sun path: elevation = 90*sin((t-6)/24 * 2*PI)
        // 6:00=sunrise(0), 12:00=zenith(90), 18:00=sunset(0), 0:00=nadir(-90)
        float sunElevation = 90.0f * std::sin((t - 6.0f) / 24.0f * glm::two_pi<float>());
        float sunAzimuth = std::fmod(t / 24.0f * 360.0f + 90.0f, 360.0f);

        settings.sunElevation = sunElevation;
        settings.sunAzimuth = sunAzimuth;

        // Moon: opposite to sun, with optional phase offset
        settings.moonElevation = -sunElevation;
        settings.moonAzimuth = std::fmod(sunAzimuth + 180.0f + settings.moonPhaseOffset * 360.0f, 360.0f);
    }
}
