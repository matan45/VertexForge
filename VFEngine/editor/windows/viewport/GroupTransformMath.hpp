#pragma once
#include "data/DTOs.hpp"
#include "math/TransformUtils.hpp"
#include <glm/glm.hpp>
#include <cmath>
#include <optional>

// VK-1490: pure world-space group-transform math for the viewport gizmo.
// The gizmo manipulates the ACTIVE entity's world matrix; the delta between its
// pre-drag and manipulated matrices is applied to every top-level group member's
// pre-drag world, so the group moves rigidly around the active entity's pivot.
// Pure functions (no ImGui/dispatcher/registry) — unit-testable from Tests.
namespace grouptransform
{
    // Shared NaN/Inf guards (single source of truth in math::).
    using math::isFinite;

    inline glm::mat4 composeWorld(const services::TransformData& t)
    {
        return math::composeMatrix(t.position, t.rotation, t.scale);
    }

    // Delta taking the active entity's pre-drag world matrix to its manipulated
    // one. nullopt when the start matrix is non-invertible (degenerate scale)
    // or the result is non-finite — the caller drops that manipulation frame.
    inline std::optional<glm::mat4> worldDelta(const glm::mat4& startActiveWorld,
                                               const glm::mat4& newActiveWorld)
    {
        constexpr float kMinDeterminant = 1e-12f;
        if (!isFinite(startActiveWorld) || !isFinite(newActiveWorld))
        {
            return std::nullopt;
        }
        if (std::abs(glm::determinant(startActiveWorld)) < kMinDeterminant)
        {
            return std::nullopt;
        }

        const glm::mat4 delta = newActiveWorld * glm::inverse(startActiveWorld);
        if (!isFinite(delta)) return std::nullopt;
        return delta;
    }

    // Applies the delta to a member's pre-drag world and decomposes the result.
    // nullopt on a non-finite result (gimbal/shear edge), so a bad frame is
    // dropped instead of persisted — same backstop as the prefab rig gizmo.
    inline std::optional<services::TransformData> applyWorldDelta(
        const glm::mat4& delta, const services::TransformData& startWorld)
    {
        const glm::mat4 newWorld = delta * composeWorld(startWorld);
        if (!isFinite(newWorld)) return std::nullopt;

        const math::DecomposedTransform d = math::decomposeMatrix(newWorld);
        if (!isFinite(d.position) || !isFinite(d.rotation) ||
            !isFinite(d.scale))
        {
            return std::nullopt;
        }

        services::TransformData result;
        result.position = d.position;
        result.rotation = d.rotation; // Euler XYZ degrees == TransformComponent schema
        result.scale = d.scale;
        return result;
    }
}
