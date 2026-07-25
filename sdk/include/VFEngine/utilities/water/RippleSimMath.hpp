#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace water
{
    // VK-1606: CPU twin of resources/shaders/water/ripple_sim.glsl. Keep both in sync - there is no
    // codegen between them. Unlike the shoaling math (which the CPU buoyancy sampler also evaluates)
    // this exists purely so the numerics can be unit-tested off-GPU: ripple displacement is
    // deliberately NOT part of getOceanHeightAt, so nothing floats on it.
    //
    // Model: the damped 2-D wave equation
    //
    //     d2h/dt2 + 2*gamma*dh/dt = c^2 * laplacian(h)
    //
    // integrated with symplectic (semi-implicit) Euler over a 5-point Laplacian. This is
    // deliberately NOT the shallow-water equations: SWE needs an advection term, a CFL condition
    // that depends on the water column, and flux limiting to stay stable, and buys behaviour
    // (bores, dam breaks) that a wake/splash patch never shows. The wave equation is one
    // multiply-add per neighbour and is unconditionally well-behaved as long as the CFL bound
    // below is respected.

    // The patch is a fixed-size camera-following window. Both constants are compile-time so the
    // GPU images can be created exactly once (see WaterRippleSim), which is what keeps the set-9
    // descriptor writes from ever going stale.
    inline constexpr uint32_t RIPPLE_RESOLUTION = 512;
    inline constexpr float RIPPLE_DEFAULT_PATCH_SIZE = 100.0f;   // metres -> ~0.195 m texels

    // Upper bound on impulses consumed by one dispatch. The SSBO is uploaded with vkCmdUpdateBuffer,
    // whose limit is 64 KiB; 64 * 16 B is nowhere near it, the cap is about shader cost.
    inline constexpr uint32_t MAX_WATER_IMPULSES = 64;

    // Symplectic Euler on x'' = -omega^2 x is stable for omega*dt <= 2. The 5-point Laplacian's
    // negation has maximum eigenvalue 8/dx^2, so omega_max = 2*sqrt(2)*c/dx and the bound collapses
    // to the Courant number below. At 512^2 over 100 m and a 1/60 s step that caps c at ~8.29 m/s.
    inline constexpr float RIPPLE_CFL_LIMIT = 0.7071067811865475f;   // 1/sqrt(2)

    // Fixed simulation step. The frame dt is clamped and then consumed in whole steps of this size
    // so the integrator sees a constant dt no matter what the frame rate does - the CFL bound is a
    // function of dt, and a variable dt would make "stable" a frame-rate-dependent property.
    inline constexpr float RIPPLE_SIM_STEP = 1.0f / 60.0f;
    inline constexpr uint32_t RIPPLE_MAX_SUBSTEPS = 4;

    struct WaterImpulse
    {
        glm::vec2 positionXZ{0.0f};
        float radius = 1.0f;
        float strength = 1.0f;
    };

    // One cell of the ping-pong state image (RGBA16F: height, velocity, foam, unused).
    struct RippleTexel
    {
        float height = 0.0f;
        float velocity = 0.0f;
        float foam = 0.0f;
    };

    inline float rippleTexelSize(float patchSize, uint32_t resolution = RIPPLE_RESOLUTION)
    {
        return resolution > 0 ? patchSize / static_cast<float>(resolution) : 0.0f;
    }

    // ---------------------------------------------------------------------------------------------
    // Stability
    // ---------------------------------------------------------------------------------------------

    inline float rippleMaxWaveSpeed(float dt, float texelSize)
    {
        if (dt <= 0.0f || texelSize <= 0.0f)
            return 0.0f;
        return RIPPLE_CFL_LIMIT * texelSize / dt;
    }

    inline bool rippleIsStable(float waveSpeed, float dt, float texelSize)
    {
        if (texelSize <= 0.0f)
            return false;
        return waveSpeed * dt / texelSize <= RIPPLE_CFL_LIMIT;
    }

    // Authored wave speed is a look-and-feel knob, so clamp it rather than trusting it. Called once
    // per frame on the CPU; the shader receives an already-safe value and never re-derives it.
    inline float rippleClampWaveSpeed(float waveSpeed, float dt, float texelSize)
    {
        return std::clamp(waveSpeed, 0.0f, rippleMaxWaveSpeed(dt, texelSize));
    }

    // Per-step multiplier from a per-second decay rate. Expressed this way so the visual damping is
    // independent of the step size (and so the analytic damped-oscillator comparison in the tests
    // has a well-defined gamma = dampingPerSecond / 2).
    inline float rippleDampingFactor(float dampingPerSecond, float dt)
    {
        return std::exp(-std::max(dampingPerSecond, 0.0f) * std::max(dt, 0.0f));
    }

    // ---------------------------------------------------------------------------------------------
    // Integration
    // ---------------------------------------------------------------------------------------------

    inline float rippleLaplacian(float center, float left, float right, float up, float down,
                                 float texelSize)
    {
        if (texelSize <= 0.0f)
            return 0.0f;
        return (left + right + up + down - 4.0f * center) / (texelSize * texelSize);
    }

    // One damped wave-equation step for a single texel. `dampingFactor` is the per-step multiplier
    // from rippleDampingFactor. Velocity is updated first and the new velocity advances the height -
    // that ordering is what makes this symplectic (and therefore stable up to the CFL bound rather
    // than only up to some smaller explicit-Euler bound).
    inline RippleTexel rippleStep(const RippleTexel& center, float laplacian,
                                  float waveSpeed, float dampingFactor, float dt)
    {
        RippleTexel out;
        out.velocity = (center.velocity + waveSpeed * waveSpeed * laplacian * dt) * dampingFactor;
        out.height = center.height + out.velocity * dt;
        out.foam = center.foam;   // composed separately by rippleFoamStep
        return out;
    }

    // ---------------------------------------------------------------------------------------------
    // Impulses
    // ---------------------------------------------------------------------------------------------

    // Compact radial kernel: 1 at the centre, exactly 0 from `radius` outwards, with zero first and
    // second derivative at both ends. A cone or a gaussian-with-cutoff would put a kink or a step at
    // the rim and the wave equation turns any discontinuity into a permanent ring of high-frequency
    // noise that the damping term is too weak to remove.
    inline float rippleImpulseKernel(float distance, float radius)
    {
        if (radius <= 0.0f)
            return 0.0f;
        const float t = std::clamp(distance / radius, 0.0f, 1.0f);
        const float s = 1.0f - t * t;
        return s * s * s;
    }

    // Impulses inject VELOCITY, not height. A velocity push produces the expanding ring everyone
    // expects from a splash; injecting height instead drops a bump that immediately splits into two
    // counter-propagating rings and reads as a double hit.
    inline float rippleImpulseVelocity(const glm::vec2& worldXZ, const WaterImpulse& impulse)
    {
        const float d = glm::length(worldXZ - impulse.positionXZ);
        return impulse.strength * rippleImpulseKernel(d, impulse.radius);
    }

    // ---------------------------------------------------------------------------------------------
    // Foam
    // ---------------------------------------------------------------------------------------------

    // Curvature is what a real surface foams on: crests and the sharp rim of a fresh impulse both
    // show up as a large |laplacian|, while a smooth swell does not. max() rather than += so foam
    // decays from its own history but is instantly re-established by a new disturbance.
    inline float rippleFoamStep(float prevFoam, float laplacian, float impulseFoam,
                                float foamGain, float foamDecayPerSecond, float dt)
    {
        const float decayed = prevFoam * rippleDampingFactor(foamDecayPerSecond, dt);
        const float generated = std::clamp(std::abs(laplacian) * foamGain, 0.0f, 1.0f) + impulseFoam;
        return std::clamp(std::max(decayed, generated), 0.0f, 1.0f);
    }

    // ---------------------------------------------------------------------------------------------
    // Camera-following window
    //
    // The origin is snapped to the texel lattice. Without the snap the field would be resampled at a
    // sub-texel offset every frame and the whole surface would visibly crawl in the opposite
    // direction to the camera. With it, re-indexing is an exact integer shift and costs nothing.
    // ---------------------------------------------------------------------------------------------

    inline glm::vec2 rippleSnapOrigin(const glm::vec2& cameraXZ, float patchSize,
                                      uint32_t resolution = RIPPLE_RESOLUTION)
    {
        const float ts = rippleTexelSize(patchSize, resolution);
        if (ts <= 0.0f)
            return cameraXZ;

        const glm::vec2 raw = cameraXZ - glm::vec2(0.5f * patchSize);
        return glm::vec2(std::floor(raw.x / ts) * ts, std::floor(raw.y / ts) * ts);
    }

    // VK-1607 review: has the patch size changed enough that the existing field is meaningless?
    //
    // Everything about the ping-pong state is expressed in texels, and the texel size IS
    // patchSize / resolution. Change the patch size and every surviving texel silently starts
    // representing a different amount of world: the Laplacian is divided by a spacing the field was
    // never simulated with, and - worse - the scroll re-index compares an origin snapped to the NEW
    // lattice against a previous origin snapped to the OLD one, so what is supposed to be an exact
    // whole-texel shift becomes an arbitrary one that re-indexes every texel to the wrong neighbour.
    // The field has to be dropped, which the sim already knows how to do for free via needsReset.
    [[nodiscard]] inline bool rippleNeedsReset(float previousPatchSize, float newPatchSize)
    {
        return std::abs(newPatchSize - previousPatchSize) > 1.0e-4f;
    }

    inline glm::vec2 rippleTexelCenter(const glm::ivec2& index, const glm::vec2& origin, float texelSize)
    {
        return origin + (glm::vec2(index) + 0.5f) * texelSize;
    }

    // Both origins sit on the same lattice, so this quotient is integral up to float error; lround
    // only removes that error, it is not a resampling approximation.
    inline glm::ivec2 rippleScrollOffset(const glm::vec2& originCurr, const glm::vec2& originPrev,
                                         float texelSize)
    {
        if (texelSize <= 0.0f)
            return glm::ivec2(0);

        const glm::vec2 delta = (originCurr - originPrev) / texelSize;
        return glm::ivec2(static_cast<int>(std::lround(delta.x)),
                          static_cast<int>(std::lround(delta.y)));
    }

    // Where the texel now at `dstIndex` lived in the previous window. False means it has just
    // scrolled in and has no history - the caller must read zero rather than clamp, or the border
    // row would be smeared across the newly exposed water.
    inline bool rippleSourceIndex(const glm::ivec2& dstIndex, const glm::ivec2& scrollOffset,
                                  glm::ivec2& outSource, uint32_t resolution = RIPPLE_RESOLUTION)
    {
        outSource = dstIndex + scrollOffset;
        const int n = static_cast<int>(resolution);
        return outSource.x >= 0 && outSource.x < n && outSource.y >= 0 && outSource.y < n;
    }

    // How many fixed steps to run for this frame. The clamp mirrors OceanFFT's dt clamp (an alt-tab
    // pause must not hand the integrator a one-second delta) and the substep cap stops a hitch from
    // cascading into an even longer frame.
    //
    // The remainder is CARRIED in `accumulator` rather than dropped. Dropping it looks harmless but
    // is not: at 61 fps every frame is a hair short of one step, so a drop-remainder version runs
    // zero steps on most frames and the ripples visibly crawl at a fraction of real time.
    inline uint32_t rippleSubstepCount(float& accumulator, float frameDt)
    {
        accumulator += std::clamp(frameDt, 0.0f, 0.1f);

        uint32_t steps = 0;
        while (accumulator >= RIPPLE_SIM_STEP && steps < RIPPLE_MAX_SUBSTEPS)
        {
            accumulator -= RIPPLE_SIM_STEP;
            ++steps;
        }

        // Hit the ceiling: abandon the backlog instead of paying it off over the following frames,
        // which would turn one hitch into a sustained overload.
        if (accumulator > RIPPLE_SIM_STEP * static_cast<float>(RIPPLE_MAX_SUBSTEPS))
            accumulator = 0.0f;

        return steps;
    }
}
