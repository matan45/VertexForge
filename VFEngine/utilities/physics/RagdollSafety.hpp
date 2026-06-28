#pragma once
#include <glm/glm.hpp>
#include "../types/PhysicsAnimationTypes.hpp"
#include <cmath>

// Jolt-free ragdoll-simulation safety helpers. Kept on the Utilities "physics" surface (alongside
// HitReactionState) so they are unit-testable — the Tests project links Utilities but NOT
// Core/Physics/jolt, so anything that touches JPH types cannot be covered there.
namespace physics
{
    // True for the modes whose ragdoll bodies are simulated (so the entity's own gameplay collider
    // must be suspended to avoid fighting them — see colliderActionForModeChange).
    inline bool usesRagdollBodies(types::PhysicsAnimationMode mode)
    {
        return mode == types::PhysicsAnimationMode::Ragdoll
            || mode == types::PhysicsAnimationMode::PoweredRagdoll;
    }

    // VK-1437 fix #A: a physics-animation mode change either suspends the entity's gameplay collider
    // (entering a ragdoll mode), restores it (leaving one), or leaves it untouched (e.g. Ragdoll <->
    // PoweredRagdoll, or Animated <-> Kinematic). This mirrors how Unity/Godot/UE disable the alive
    // capsule/controller while a character ragdolls.
    enum class GameplayColliderAction { None, Suspend, Restore };

    inline GameplayColliderAction colliderActionForModeChange(types::PhysicsAnimationMode oldMode,
                                                              types::PhysicsAnimationMode newMode)
    {
        const bool wasRagdoll = usesRagdollBodies(oldMode);
        const bool isRagdoll = usesRagdollBodies(newMode);
        if (!wasRagdoll && isRagdoll) return GameplayColliderAction::Suspend;
        if (wasRagdoll && !isRagdoll) return GameplayColliderAction::Restore;
        return GameplayColliderAction::None;
    }

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
