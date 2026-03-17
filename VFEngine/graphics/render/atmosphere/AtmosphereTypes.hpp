#pragma once
#include "atmosphere/AtmosphereSettings.hpp"
#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>

namespace render::atmosphere
{
    // GPU-side parameter UBO (std140 layout)
    struct alignas(16) AtmosphereGPUParams
    {
        // Planet
        float planetRadius;
        float atmosphereRadius;
        float pad0[2];

        // Rayleigh
        glm::vec4 rayleighScattering;  // xyz = scattering, w = densityExpScale

        // Mie
        float mieScattering;
        float mieAbsorption;
        float mieAnisotropy;
        float mieDensityExpScale;

        // Ozone
        glm::vec4 ozoneAbsorption;     // xyz = absorption, w = centerAlt
        float ozoneWidth;
        float pad1[3];

        // Sun
        glm::vec4 sunIrradiance;       // xyz = irradiance, w = angularRadius
        glm::vec4 sunDirection;        // xyz = direction, w = unused

        // Ground
        glm::vec4 groundAlbedo;        // xyz = albedo, w = unused

        // Camera
        glm::vec4 cameraPosition;      // xyz = world pos, w = altitude above surface

        // Matrices
        glm::mat4 invViewProjection;
        glm::mat4 viewProjection;

        // Screen
        float nearPlane;
        float farPlane;
        float aerialMaxDist;
        float aerialIntensity;

        uint32_t screenWidth;
        uint32_t screenHeight;
        float pad2[2];
    };

    inline glm::vec3 sunDirectionFromAngles(float azimuthDeg, float elevationDeg)
    {
        float azRad = glm::radians(azimuthDeg);
        float elRad = glm::radians(elevationDeg);
        float cosEl = std::cos(elRad);
        return glm::vec3(
            cosEl * std::sin(azRad),
            std::sin(elRad),
            cosEl * std::cos(azRad)
        );
    }
}
