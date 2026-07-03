#pragma once

#include "VFXForceTypes.hpp"

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <string>

namespace vfx
{
    inline const char* killVolumeShapeToString(KillVolumeShape shape)
    {
        switch (shape)
        {
        case KillVolumeShape::Plane:  return "Plane";
        case KillVolumeShape::Sphere: return "Sphere";
        case KillVolumeShape::Box:    return "Box";
        default:                      return "Plane";
        }
    }

    inline KillVolumeShape stringToKillVolumeShape(const std::string& str)
    {
        if (str == "Sphere") return KillVolumeShape::Sphere;
        if (str == "Box")    return KillVolumeShape::Box;
        return KillVolumeShape::Plane;
    }

    inline glm::vec3 sanitizeKillVolumeNormal(const glm::vec3& normal)
    {
        const float lenSq = glm::dot(normal, normal);
        if (lenSq <= 1e-8f)
            return glm::vec3(0.0f, 1.0f, 0.0f);
        return normal / std::sqrt(lenSq);
    }

    inline glm::vec3 sanitizeKillVolumeHalfExtents(const glm::vec3& halfExtents)
    {
        return glm::max(halfExtents, glm::vec3(0.0f));
    }

    inline bool applyKillVolumeInvert(bool killed, bool invert)
    {
        return invert ? !killed : killed;
    }

    inline bool killedByPlane(const glm::vec3& position, const glm::vec3& center,
                              const glm::vec3& normal, bool invert)
    {
        const glm::vec3 n = sanitizeKillVolumeNormal(normal);
        return applyKillVolumeInvert(glm::dot(n, position - center) < 0.0f, invert);
    }

    inline bool killedBySphere(const glm::vec3& position, const glm::vec3& center,
                               float radius, bool invert)
    {
        const float r = std::max(radius, 0.0f);
        const glm::vec3 delta = position - center;
        return applyKillVolumeInvert(glm::dot(delta, delta) <= r * r, invert);
    }

    inline bool killedByBox(const glm::vec3& position, const glm::vec3& center,
                            const glm::vec3& halfExtents, bool invert)
    {
        const glm::vec3 extents = sanitizeKillVolumeHalfExtents(halfExtents);
        const glm::vec3 delta = glm::abs(position - center);
        const bool inside = delta.x <= extents.x && delta.y <= extents.y && delta.z <= extents.z;
        return applyKillVolumeInvert(inside, invert);
    }

    inline bool killedByVolume(const glm::vec3& position, const KillVolumeForceConfig& config)
    {
        switch (config.shape)
        {
        case KillVolumeShape::Plane:
            return killedByPlane(position, config.center, config.normal, config.invert);
        case KillVolumeShape::Sphere:
            return killedBySphere(position, config.center, config.radius, config.invert);
        case KillVolumeShape::Box:
            return killedByBox(position, config.center, config.halfExtents, config.invert);
        default:
            return false;
        }
    }
}
