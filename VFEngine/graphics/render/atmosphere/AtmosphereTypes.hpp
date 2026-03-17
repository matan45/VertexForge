#pragma once
#include "atmosphere/AtmosphereSettings.hpp"
#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>

namespace render::atmosphere
{
    // GPU-side parameter UBO — all vec4/mat4 to guarantee std140 alignment
    struct AtmosphereGPUParams
    {
        glm::vec4 planetParams;        // x=planetRadius, y=atmosphereRadius, z=0, w=0
        glm::vec4 rayleighScattering;  // xyz=scattering, w=densityExpScale
        glm::vec4 mieParams;           // x=scattering, y=absorption, z=anisotropy, w=densityExpScale
        glm::vec4 ozoneAbsorption;     // xyz=absorption, w=centerAlt
        glm::vec4 ozoneParams;         // x=ozoneWidth, y=0, z=0, w=0
        glm::vec4 sunIrradiance;       // xyz=irradiance, w=angularRadius
        glm::vec4 sunDirection;        // xyz=direction, w=0
        glm::vec4 groundAlbedo;        // xyz=albedo, w=0
        glm::vec4 cameraPosition;      // xyz=world pos, w=altitude above surface
        glm::mat4 invViewProjection;
        glm::mat4 viewProjection;
        glm::vec4 screenParams;        // x=nearPlane, y=farPlane, z=aerialMaxDist, w=aerialIntensity
        glm::uvec4 screenSize;         // x=width, y=height, z=0, w=0
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
