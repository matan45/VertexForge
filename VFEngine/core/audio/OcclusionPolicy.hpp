#pragma once

// VK-1518 — geometry occlusion policy.
//
// Pure decision core for "is this emitter behind a wall, and what should that do to it".
// A listener->emitter raycast on the main thread produces a binary verdict; the audio
// thread glides it with an attack/release one-pole and folds the result into the existing
// per-source direct filter (AudioSource::updateDistanceFilter).
//
// AL-free / device-free / glm-free (like VoicePolicy.hpp and BusGainPolicy.hpp) so the
// CPU-only Tests project can unit-test the glide and the gain mapping directly. Tests links
// neither Audio nor OpenAL and has no openal-soft includedir, so everything here MUST stay
// inline, header-only, AL-include-free, and must NOT be marked VF_AUDIO_API (that macro
// resolves to dllimport outside the DLL, which would leave Tests with unresolved externals).
//
// The occlusion knobs are CUT AMOUNTS, not gains — 0 = inert, 1 = full cut — matching the
// house style of AudioSource3DComponent::filterIntensity. That makes 0 the neutral value a
// pooled source resets to, which is the inverse of what a gain-based design would use.

#include <algorithm>

namespace core::audio::occlusion
{
    // One-pole glide rates (1/seconds; the time constant is 1/rate). Attack is deliberately
    // slower than release: clipping a pillar for a few frames must not click, but stepping
    // out into the open should open up promptly. Compare updateDistanceFilter's own
    // smoothingRate of 10.
    inline constexpr float kAttackRate = 4.0f;   // tau ~250 ms
    inline constexpr float kReleaseRate = 8.0f;  // tau ~125 ms

    // A one-pole approaches but never reaches its target. Without this snap, a source that
    // stopped being occluded would sit at "0.0001 occluded" forever, and
    // updateDistanceFilter's detach early-out (which tests currentOcclusion <= 0) would
    // never fire — pinning an inert filter on a pooled voice for its whole life.
    inline constexpr float kSnapEpsilon = 1e-4f;

    // Per-emitter ray cadence (seconds). 10 Hz: a 5 m/s listener moves 0.5 m between rays,
    // and the glide above is slower than that anyway, so a faster cadence buys nothing.
    inline constexpr float kRayInterval = 0.1f;

    // Pulls the line-of-sight test short of the emitter so the emitter's OWN collider does
    // not read as a wall. Same trick as BehaviorTreeAdapter::hasLineOfSight's 0.1 skin.
    inline constexpr float kSelfHitSkin = 0.2f;

    // One-pole step toward target. Mirrors updateDistanceFilter's idiom — the
    // min(1, dt * rate) clamp means a frame hitch lands exactly ON the target instead of
    // overshooting and ringing. Snaps within kSnapEpsilon so the target is reached exactly.
    // dt <= 0 (paused / first tick) and rate <= 0 hold the current value.
    [[nodiscard]] constexpr float smoothOcclusion(float current, float target,
                                                  float deltaTime, float rate) noexcept
    {
        current = std::clamp(current, 0.0f, 1.0f);
        target = std::clamp(target, 0.0f, 1.0f);

        if (deltaTime <= 0.0f || rate <= 0.0f)
        {
            return current;
        }

        const float lerpFactor = std::min(1.0f, deltaTime * rate);
        const float next = current + (target - current) * lerpFactor;

        // std::abs is not constexpr for floats until C++23.
        const float diff = next > target ? next - target : target - next;
        if (diff <= kSnapEpsilon)
        {
            return target;
        }
        return std::clamp(next, 0.0f, 1.0f);
    }

    // Maps an occlusion factor and a CUT AMOUNT to a multiplicative gain.
    // amount == 0 is inert; amount == 1 fully cuts at full occlusion.
    //
    // INVARIANT: occlusionGain(0, x) == 1.0f EXACTLY for every x. updateDistanceFilter's
    // behaviour preservation rests on this — with no occlusion its two AL writes must
    // reduce bit-for-bit to the pre-VK-1518 (currentGainHF, 1.0f) pair.
    [[nodiscard]] constexpr float occlusionGain(float occlusion, float amount) noexcept
    {
        occlusion = std::clamp(occlusion, 0.0f, 1.0f);
        amount = std::clamp(amount, 0.0f, 1.0f);
        return 1.0f - amount * occlusion;
    }

    // Binary verdict from a single listener->emitter ray. Anything the ray hits before
    // (emitterDistance - skin) is geometry between the two; a hit at or past that line is
    // the emitter's own collider and must not count.
    //
    // Callers already cap RaycastQuery::maxDistance at (emitterDistance - skin), so the
    // skin test is belt-and-braces there — it is kept so the helper is correct, and
    // testable, standalone.
    [[nodiscard]] constexpr float occlusionFromRay(bool hit, float hitDistance,
                                                   float emitterDistance, float skin) noexcept
    {
        if (!hit)
        {
            return 0.0f;
        }
        // Emitter is closer than the skin — there is no room for a wall between us.
        if (emitterDistance <= skin)
        {
            return 0.0f;
        }
        if (hitDistance >= emitterDistance - skin)
        {
            return 0.0f;
        }
        return 1.0f;
    }
}
