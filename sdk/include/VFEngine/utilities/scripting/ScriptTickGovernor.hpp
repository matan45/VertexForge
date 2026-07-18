#pragma once

#include <cmath>
#include <cstdint>

namespace scripting
{
    // VK-1536: pure script tick-governor math, mirroring the RTShadowBudget.hpp /
    // DynamicResolutionBudget.hpp pattern (Inputs -> pure evaluate*Core -> Decision) so the CPU-only
    // Tests project can validate the dt accumulation and the stagger with no
    // registry, no mType interpreter and no Vulkan device (test_script_tick_governor). Header-only
    // and dependency-free, and the single source of truth ScriptingServiceImpl::updateScripts calls
    // into, so the behavior the tick applies is exactly the behavior the tests pin down.
    //
    // The governor reduces how often an opted-in script's onUpdate runs. It is strictly opt-in:
    // updateInterval == 0 (the default) bypasses everything and ticks every frame, matching the
    // engine-wide "0 disables the cap" convention (vfx::kDefaultMaxLiveInstances,
    // VFXSceneRenderer::significanceBudget) so unopted scenes stay byte-identical.
    //
    // CONTRACT: a throttled script MUST integrate against the deltaTime it is handed. The governor
    // passes the ACCUMULATED dt (real elapsed time since that script's last tick), not the frame dt.
    // A script that ignores deltaTime and assumes per-frame cadence will run in slow motion, and a
    // script that polls level-triggered input (Input::isKeyDown) can miss presses shorter than its
    // interval — such scripts must not be throttled.

    // Deterministic [0, 1) hash (splitmix64 finalizer). Used to stagger the first tick of scripts
    // that share an interval. Must stay deterministic across runs and platforms: a random phase
    // would make frame timing irreproducible and the stagger test flaky.
    [[nodiscard]] inline float unitHash(uint64_t x) noexcept
    {
        x += 0x9E3779B97F4A7C15ull;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
        x ^= (x >> 31);
        // Top 24 bits / 2^24 — exactly representable in a float, so the result is in [0, 1).
        return static_cast<float>(x >> 40) * (1.0f / 16777216.0f);
    }

    struct ScriptTickInputs
    {
        // Carried across frames by the caller (ScriptEntry::tickAccumulator).
        float accumulator = 0.0f;
        // GAME delta (engineTime::Timer::getGameDeltaTime) — already pause/slow-mo scaled, so a
        // throttled script's cadence freezes and stretches with the sim, as it should.
        float dt = 0.0f;
        // Effective interval in seconds after distance scaling. <= 0 => tick every frame.
        float interval = 0.0f;
        // False until this script has ticked once under the governor; drives the stagger below.
        bool firstTickDone = false;
        // Stable per-script handle, hashed for the stagger phase. Only read when !firstTickDone.
        uint64_t instanceId = 0;
    };

    struct ScriptTickDecision
    {
        bool shouldTick = false;
        // The dt to hand onUpdate: the REAL elapsed time since this script's last tick, not the
        // frame dt and not the nominal interval.
        float tickDt = 0.0f;
        // Values to store back into the caller's ScriptEntry.
        float accumulator = 0.0f;
        bool firstTickDone = false;
    };

    // Decide whether an opted-in script ticks this frame.
    //
    // Accumulates elapsed time and fires once it reaches the deadline, handing over the accumulated
    // dt and dropping the remainder. Dropping is correct here precisely BECAUSE tickDt carries the
    // true elapsed time: no time is lost, it is delivered inside tickDt, so sum(tickDt) over any run
    // equals total elapsed game time and a throttled script integrates to the same place as an
    // unthrottled one. (The alternative — carry the remainder and pass a fixed `interval` — is the
    // fixed-timestep model; it needs a catch-up loop and can spiral after a hitch. Wrong tool: this
    // is a rate reducer, not a determinism mechanism. onFixedUpdate remains the fixed-step path.)
    //
    // Fails open: a non-positive or non-finite interval ticks every frame. The failure mode of an
    // unexpected value must be "runs as it does today", never "silently stops running".
    [[nodiscard]] inline ScriptTickDecision evaluateScriptTick(const ScriptTickInputs& in) noexcept
    {
        ScriptTickDecision d{};
        d.firstTickDone = in.firstTickDone;

        if (!(in.interval > 0.0f) || !std::isfinite(in.interval))
        {
            d.shouldTick = true;
            d.tickDt = in.dt;
            d.accumulator = 0.0f;
            d.firstTickDone = true;
            return d;
        }

        // Stagger. N scripts sharing an interval would otherwise accumulate in lockstep and all
        // cross the threshold on the SAME frame, converting a steady per-frame cost into an Nx
        // sawtooth spike every `interval` seconds — worse than not throttling at all. So the FIRST
        // deadline is a deterministic fraction of the interval; from then on each script ticks on
        // its own offset cadence. Time-domain equivalent of VFXSceneRenderer's
        // `updatePhase = id % interval`.
        //
        // Note this shortens the first DEADLINE rather than pre-filling the accumulator with a phase
        // offset. Pre-filling would hand the first tick fake time (accumulator + dt) when only dt
        // actually elapsed, making a movement script jump on its first update.
        const float deadline = in.firstTickDone ? in.interval
                                                : in.interval * unitHash(in.instanceId);

        const float acc = in.accumulator + in.dt;

        // A hitch (dt >= deadline) fires exactly once with the whole elapsed time rather than
        // bursting N catch-up calls — one big dt is what the script's own integration expects.
        if (acc >= deadline)
        {
            d.shouldTick = true;
            d.tickDt = acc;
            d.accumulator = 0.0f;
            d.firstTickDone = true;
            return d;
        }

        d.shouldTick = false;
        d.tickDt = 0.0f;
        d.accumulator = acc;
        return d;
    }
}
