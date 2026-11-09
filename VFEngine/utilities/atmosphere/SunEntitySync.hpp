#pragma once
#include <glm/glm.hpp>
#include <cmath>

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

    // VK-1654: the inverse — the sun azimuth/elevation (degrees; x = azimuth in [0, 360),
    // y = elevation in [-90, 90]) that a directional light with these Euler angles points away
    // from. Rx(x) * Ry(y) * Rz(z) applied to (0,0,-1) gives the light-travel direction
    // L = (-sin y, cos y * sin x, -cos y * cos x); roll never moves -Z. The sun is at -L.
    // directionalLightEulerForSun(az, el) round-trips through this for any elevation strictly
    // between -90 and 90 (at the poles the azimuth is undefined and comes back as 0).
    [[nodiscard]] inline glm::vec2 sunAnglesFromDirectionalLightEuler(const glm::vec3& eulerDeg)
    {
        const float pitch = glm::radians(eulerDeg.x);
        const float yaw = glm::radians(eulerDeg.y);
        const glm::vec3 dirToSun(std::sin(yaw), -std::cos(yaw) * std::sin(pitch), std::cos(yaw) * std::cos(pitch));

        const float elevation = glm::degrees(std::asin(glm::clamp(dirToSun.y, -1.0f, 1.0f)));
        float azimuth = glm::degrees(std::atan2(dirToSun.x, dirToSun.z));
        if (azimuth < 0.0f)
            azimuth += 360.0f;
        if (azimuth >= 360.0f) // a tiny negative angle + 360 can round up to exactly 360
            azimuth -= 360.0f;
        if (azimuth == 0.0f)
            azimuth = 0.0f; // fold -0 (atan2 of a -0 yaw) into +0
        return glm::vec2(azimuth, elevation);
    }
}
