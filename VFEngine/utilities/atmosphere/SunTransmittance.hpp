#pragma once
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

#include "AtmosphereSettings.hpp"

namespace render::atmosphere
{
    // CPU port of the GPU transmittance-LUT integral
    // (resources/shaders/atmosphere/transmittance_lut.glsl +
    //  atmosphere_common.glsl::computeOpticalDepth / distToAtmosphereBoundary).
    //
    // Returns the fraction of sunlight (per RGB channel) that survives the atmosphere
    // along the ray toward the sun. Pure, allocation-free, 40 samples -> microseconds
    // per call, no Vulkan / no GPU readback / no fence latency -> fully doctestable.
    //
    //   s              : atmosphere parameters (planet/Rayleigh/Mie/ozone). Only extinction
    //                    terms are read; sunIrradiance / phase terms are NOT used here.
    //   dirToSun       : world-space direction from the observer TO the sun (need not be
    //                    normalized). Local up is +Y, so cosZenith = normalize(dirToSun).y.
    //   altitudeMeters : observer height above the planet surface (0 = ground).
    //
    // The result is ~white at the zenith, red-shifted (r > g > b, b -> 0) near the horizon,
    // and exactly vec3(0) below the horizon (no direct sun). The below-horizon clamp is an
    // explicit addition the raw shader integral does not perform; it matches VK-1566's
    // scene-lighting semantics (the sun stops lighting geometry once it sets).
    [[nodiscard]] inline glm::vec3 evaluateSunTransmittance(const AtmosphereSettings& s,
                                                            glm::vec3 dirToSun,
                                                            float altitudeMeters)
    {
        // Cosine of the angle between the local zenith (+Y) and the sun ray.
        const float len = glm::length(dirToSun);
        if (len <= 0.0f)
            return glm::vec3(0.0f);
        const float cosZenith = dirToSun.y / len;

        // Sun at or below the horizon -> no direct light reaches the observer.
        if (cosZenith <= 0.0f)
            return glm::vec3(0.0f);

        constexpr int NUM_SAMPLES = 40; // matches transmittance_lut.glsl:29

        // distToAtmosphereBoundary (atmosphere_common.glsl:25-33)
        const float r = s.planetRadius + altitudeMeters;
        const float rCosZ = r * cosZenith;
        const float atmoRSq = s.atmosphereRadius * s.atmosphereRadius;
        const float dist = -rCosZ + std::sqrt(std::max(rCosZ * rCosZ - r * r + atmoRSq, 0.0f));
        const float ds = dist / static_cast<float>(NUM_SAMPLES);

        // computeOpticalDepth (atmosphere_common.glsl:163-196)
        glm::vec3 opticalDepth(0.0f);
        for (int i = 0; i < NUM_SAMPLES; ++i)
        {
            const float t = (static_cast<float>(i) + 0.5f) * ds;
            const float sampleR = std::sqrt(r * r + 2.0f * r * cosZenith * t + t * t);
            const float sampleAlt = sampleR - s.planetRadius;
            if (sampleAlt < 0.0f)
                break;

            const float densityR = std::exp(s.rayleighDensityExpScale * sampleAlt);
            const float densityM = std::exp(s.mieDensityExpScale * sampleAlt);
            const float densityO =
                std::max(0.0f, 1.0f - std::abs(sampleAlt - s.ozoneCenterAlt) / s.ozoneWidth);

            const glm::vec3 extinction = s.rayleighScattering * densityR
                                       + (s.mieScattering + s.mieAbsorption) * densityM
                                       + s.ozoneAbsorption * densityO;
            opticalDepth += extinction * ds;
        }

        return glm::vec3(std::exp(-opticalDepth.x),
                         std::exp(-opticalDepth.y),
                         std::exp(-opticalDepth.z));
    }
}
