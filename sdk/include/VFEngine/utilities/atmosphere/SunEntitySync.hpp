#pragma once
#include <glm/glm.hpp>
#include <cmath>

#include "AtmosphereSettings.hpp"

namespace render::atmosphere
{
    // Euler angles (degrees; X=pitch, Y=yaw, Z=roll) for a directional-light entity whose
    // local -Z (the direction light travels) points AWAY from the sun — i.e. the entity's
    // -Z equals -sunDirectionFromAngles(azimuth, elevation).
    //
    // This is the exact inverse of TransformComponent::getMatrix's rotation composition
    // (Rx(x) * Ry(y) * Rz(z)) solved for the (0,0,-1) column, so the entity's worldMatrix
    // stays bit-consistent with the atmosphere sky sun (AtmosphereTypes.hpp
    // sunDirectionFromAngles) and with the GPU/shadow -Z convention
    // (worldMatrix * (0,0,-1,0)). Verified to reproduce the sun direction to < 1e-6 across
    // the full azimuth/elevation sphere.
    //
    // NOTE: do NOT use the naive {-elevation, azimuth, 0} triple — it is only correct near
    // azimuth 0 and desyncs shadows from the sky as the day-night cycle sweeps azimuth.
    [[nodiscard]] inline glm::vec3 directionalLightEulerForSun(float azimuthDeg, float elevationDeg)
    {
        const float az = glm::radians(azimuthDeg);
        const float el = glm::radians(elevationDeg);
        const float cosEl = std::cos(el);
        const glm::vec3 dirToSun(cosEl * std::sin(az), std::sin(el), cosEl * std::cos(az));
        const glm::vec3 L = -dirToSun; // light travel direction = entity local -Z in world

        const float yaw = std::asin(glm::clamp(-L.x, -1.0f, 1.0f)); // rotation.y
        const float pitch = std::atan2(L.y, -L.z);                  // rotation.x
        return glm::vec3(glm::degrees(pitch), glm::degrees(yaw), 0.0f);
    }
}
