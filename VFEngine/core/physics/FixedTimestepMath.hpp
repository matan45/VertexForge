#pragma once

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Pure-logic fixed-timestep + render-interpolation math, factored out of
// FixedTimestep / PhysicsAdapter. Kept free of any Jolt include so it can be
// unit tested in the CPU-only Tests project, which links Services (not Core/Jolt).
// Mirrors the core/physics/SpatialQueryHelpers.hpp pattern.
namespace core::physics
{
    // Result of advancing the fixed-timestep accumulator over one frame.
    struct FixedStepPlan
    {
        int steps = 0;      // number of fixed sub-steps to simulate this frame
        double alpha = 0.0; // leftover accumulator / timestep — the render interpolation factor, in [0, 1)
    };

    // Advance `accumulator` by `dt` and decide how many fixed sub-steps of length
    // `timestep` to run this frame. `accumulator` is updated in place; the caller
    // is responsible for actually simulating `plan.steps` steps of `timestep`.
    //
    // Spiral-of-death guards:
    //   * the incoming dt is clamped to `maxAccumulator`, so a single long stall
    //     cannot inject an unbounded amount of simulation time in one frame;
    //   * at most `maxSteps` sub-steps run per frame. When that cap is hit, the
    //     un-simulated whole steps are DROPPED (accumulator := fmod(accumulator,
    //     timestep)) so the interpolation alpha stays in [0, 1) and never
    //     extrapolates. Without this, a hitch leaves alpha >> 1 and the render
    //     transform overshoots wildly (glm::mix/slerp with t > 1).
    inline FixedStepPlan planFixedSteps(double& accumulator, double dt,
                                        double timestep, double maxAccumulator, int maxSteps)
    {
        FixedStepPlan plan;
        if (timestep <= 0.0)
        {
            return plan;
        }

        dt = std::min(dt, maxAccumulator);
        accumulator += dt;

        while (accumulator >= timestep && plan.steps < maxSteps)
        {
            accumulator -= timestep;
            ++plan.steps;
        }

        // Drop the remainder only when the per-frame step cap prevented us from
        // draining the accumulator (spiral-of-death path). In the normal path the
        // loop already exits with accumulator < timestep, so fmod is a no-op there
        // and behaviour is unchanged.
        if (plan.steps >= maxSteps && accumulator >= timestep)
        {
            accumulator = std::fmod(accumulator, timestep);
        }

        plan.alpha = accumulator / timestep;
        return plan;
    }

    // Clamp a render-interpolation factor to [0, 1] so the helpers below always
    // interpolate (never extrapolate), even if handed a stale/overshooting alpha.
    inline float clampAlpha(float alpha)
    {
        return alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
    }

    // Linear interpolation of a vector state (position / velocity) by a clamped alpha.
    inline glm::vec3 interpVec3(const glm::vec3& prev, const glm::vec3& curr, float alpha)
    {
        return glm::mix(prev, curr, clampAlpha(alpha));
    }

    // Spherical interpolation of an orientation by a clamped alpha.
    inline glm::quat interpRotation(const glm::quat& prev, const glm::quat& curr, float alpha)
    {
        return glm::slerp(prev, curr, clampAlpha(alpha));
    }
}
