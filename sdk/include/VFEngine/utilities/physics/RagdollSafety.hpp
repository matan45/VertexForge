#pragma once
#include <glm/glm.hpp>
#include <cmath>

// Jolt-free ragdoll-simulation safety helpers. Kept on the Utilities "physics" surface (alongside
// HitReactionState) so they are unit-testable — the Tests project links Utilities but NOT
// Core/Physics/jolt, so anything that touches JPH types cannot be covered there.
namespace physics
{
    // Bounds a ragdoll body's velocity so an applied impulse cannot reach escape velocity. An
    // unbounded velocity integrates to a non-finite position within a single physics step, which
    // corrupts Jolt's broad-phase quad tree (a hard crash inside PhysicsSystem::Update). Non-finite
    // input (NaN / inf) collapses to zero; finite input over maxMagnitude is scaled down with its
    // direction preserved; everything else is returned unchanged.
    inline glm::vec3 clampVelocityMagnitude(const glm::vec3& v, float maxMagnitude)
    {
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
            return glm::vec3(0.0f);

        const float lenSq = v.x * v.x + v.y * v.y + v.z * v.z;
        if (maxMagnitude > 0.0f && lenSq > maxMagnitude * maxMagnitude)
        {
            const float len = std::sqrt(lenSq);
            return v * (maxMagnitude / len);
        }
        return v;
    }
}
