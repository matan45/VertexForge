#pragma once

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

namespace services::physics
{
    // VK-1530 render interpolation for a character controller. Correct fixed-timestep
    // interpolation (Gaffer, "Fix Your Timestep") needs TWO persistent bracketing sim
    // snapshots — `prev` = the position after sub-step N-1, `curr` = the position after
    // sub-step N — that shift ONLY when a physics sub-step runs. On a zero-step frame
    // (display refresh above the physics tick rate) neither endpoint moves, so the render
    // alpha smoothly sweeps prev -> curr instead of pinning to the last sim position.
    //
    // Kept as a pure, glm-only header (no Core, no Jolt) so the CPU-only Tests project can
    // pin the behavior down, mirroring FixedTimestepMath.hpp / DynamicResolutionBudget.hpp /
    // ScriptTickGovernor.hpp. Services must not include Core headers, so the render blend
    // lives here rather than in Core's interp helpers.
    struct CharacterInterp
    {
        glm::vec3 prev{0.0f};
        glm::vec3 curr{0.0f};
        // False until the entity has been seen once; drives first-sight seeding + the
        // teleport-snap guard (a freshly-placed character must not be snap-tested).
        bool seeded = false;
    };

    // First sight / respawn: collapse both endpoints onto the freshly-placed position, so
    // nothing is rendered as motion until a sub-step actually runs.
    inline void seedCharacterInterp(CharacterInterp& s, const glm::vec3& p) noexcept
    {
        s.prev = p;
        s.curr = p;
        s.seeded = true;
    }

    // Advance one fixed sub-step: the previous snapshot becomes the old current, the new
    // current is the post-step position. Called once per sub-step.
    inline void pushSimStep(CharacterInterp& s, const glm::vec3& postStepPos) noexcept
    {
        s.prev = s.curr;
        s.curr = postStepPos;
    }

    // Collapse the bracket to `curr` when the frame moved the character farther than it
    // could legitimately walk in one frame (teleport / respawn) — interpolating across it
    // would streak. `beforeFrame` is `curr` as it stood before this frame's sub-steps.
    inline void snapOnTeleport(CharacterInterp& s, const glm::vec3& beforeFrame,
                               float snapDistance2) noexcept
    {
        if (glm::distance2(beforeFrame, s.curr) > snapDistance2)
            s.prev = s.curr;
    }

    // The rendered position: interpolate between the two persistent snapshots. `alpha` is
    // the physics accumulator remainder in [0,1); clamped defensively so a hitch (alpha
    // momentarily > 1 before the drop-remainder guard) never extrapolates.
    [[nodiscard]] inline glm::vec3 interpRenderPos(const CharacterInterp& s, float alpha) noexcept
    {
        const float a = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
        return glm::mix(s.prev, s.curr, a);
    }
}
