#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Math/Vec3.h>
#include <Jolt/Math/Quat.h>
#include <Jolt/Math/Real.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace core::physics
{
    inline JPH::Vec3 toJolt(const glm::vec3& v)
    {
        return JPH::Vec3(v.x, v.y, v.z);
    }

    inline JPH::Quat toJolt(const glm::quat& q)
    {
        glm::quat normalized = glm::normalize(q);
        if (glm::any(glm::isnan(normalized)))
        {
            return JPH::Quat::sIdentity();
        }
        return JPH::Quat(normalized.x, normalized.y, normalized.z, normalized.w);
    }

    inline JPH::RVec3 toJoltR(const glm::vec3& v)
    {
        return JPH::RVec3(v.x, v.y, v.z);
    }

    inline glm::vec3 toGlm(const JPH::Vec3& v)
    {
        return glm::vec3(v.GetX(), v.GetY(), v.GetZ());
    }

    inline glm::vec3 toGlmR(const JPH::RVec3& v)
    {
        return glm::vec3(
            static_cast<float>(v.GetX()),
            static_cast<float>(v.GetY()),
            static_cast<float>(v.GetZ())
        );
    }

    inline glm::quat toGlm(const JPH::Quat& q)
    {
        return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
    }
}
