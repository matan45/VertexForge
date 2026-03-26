#pragma once
#include <glm/glm.hpp>

namespace render::atmosphere
{
    struct AtmosphereSettings
    {
        bool enabled = false;

        // Planet
        float planetRadius = 6360000.0f;           // meters
        float atmosphereRadius = 6460000.0f;

        // Rayleigh (air molecules - blue sky)
        glm::vec3 rayleighScattering{5.802e-6f, 13.558e-6f, 33.1e-6f};
        float rayleighDensityExpScale = -1.0f / 8000.0f;

        // Mie (aerosols - haze, white glare around sun)
        float mieScattering = 3.996e-6f;
        float mieAbsorption = 4.4e-6f;
        float mieAnisotropy = 0.8f;
        float mieDensityExpScale = -1.0f / 1200.0f;

        // Ozone (absorbs red/green at horizon - orange/red sunsets)
        glm::vec3 ozoneAbsorption{0.65e-6f, 1.881e-6f, 0.085e-6f};
        float ozoneCenterAlt = 25000.0f;
        float ozoneWidth = 15000.0f;

        // Sun
        glm::vec3 sunIrradiance{1.474f, 1.8504f, 1.91198f};
        float sunAngularRadius = 0.00935f;          // radians
        float sunAzimuth = 0.0f;                    // degrees
        float sunElevation = 45.0f;                 // degrees

        // Moon
        float moonAzimuth = 180.0f;                 // degrees
        float moonElevation = -45.0f;               // degrees (below horizon by default)
        float moonAngularRadius = 0.009f;           // radians (~0.52 deg)
        float moonBrightness = 0.03f;               // relative to sunIrradiance

        // Stars
        float starDensity = 0.006f;                 // hash-cell probability threshold
        float starBrightness = 1.5f;                // HDR multiplier
        float starTwinkleSpeed = 1.0f;
        float nightSkyBrightness = 0.002f;          // ambient floor at night

        // Day-Night Cycle
        bool dayNightEnabled = false;
        float timeOfDay = 12.0f;                    // 0-24 hours
        float cycleSpeed = 1.0f;                    // real-time minute per game-hour at 1.0
        float moonPhaseOffset = 0.0f;               // 0-1, shifts moon orbit relative to sun

        // Ground
        glm::vec3 groundAlbedo{0.3f};

        // Aerial perspective
        float aerialMaxDist = 100000.0f;            // meters
        float aerialIntensity = 1.0f;
    };
}
