#pragma once

// VK-1521 — the weather loop start/stop decision, extracted pure.
//
// A weather loop is a STREAM with an uncancellable tail: fadeOutLoop arms a 2s
// FadeOutAndReleaseSoundCommand and drops the handle, and there is no way to call that
// back. So the decision to stop is irreversible for two seconds, and any target that dips
// below the threshold for one frame and recovers leaves a fading stream behind while the
// start gate immediately mints a second one. Two copies of the same loop then play at
// different offsets on the Weather bus.
//
// Before VK-1521 this could not happen, but only by accident: the stop gate watched the
// SMOOTHED follower rather than the raw target, so the follower's decay rate was an
// implicit debounce. That debounce was never designed — its length was whatever
// (volume - 0.01) / VOLUME_RAMP_SPEED happened to be, i.e. ~460ms at full rain and ~0 at a
// whisper. VK-1521 moved the gate onto the target (correctly — the follower no longer
// climbs from silence) and the accident went with it.
//
// This makes the debounce explicit and, deliberately, a HOLD rather than a follower
// threshold: how long the target has actually been silent, independent of how loud the loop
// happened to be when it went quiet.
//
// Header-only and dependency-free, following FadePolicy.hpp. That is a requirement, not a
// preference: Tests does not link Weather (see premake5.lua) but does carry
// VFEngine/utilities on its include path, so this is the only shape in which the decision
// can be tested at all.

namespace weather
{
    // Below this the target counts as silence. Matches the pre-existing gate.
    inline constexpr float LOOP_SILENCE_THRESHOLD = 0.01f;

    // How long the target must stay silent before the loop is released.
    //
    // 0.5s covers the LOUDEST implicit hold the pre-VK-1521 follower gave, so no content
    // that was stable before can start stacking now: rain at RAIN_MAX_VOLUME = 0.7 decayed
    // to the 0.01 threshold in (0.7 - 0.01) / VOLUME_RAMP_SPEED(1.5) = 0.46s. Snow tops out
    // at 0.19s, and wind never stops at all (windTarget >= WIND_BASE_VOLUME = 0.1).
    //
    // The cost of being generous is small and one-sided: a genuine weather stop is delayed
    // by at most this before a 2s fade the listener was going to sit through anyway. The
    // cost of being stingy is a second concurrent stream, which is audible immediately.
    inline constexpr float LOOP_STOP_HOLD_SECONDS = 0.5f;

    // The follower's state, owned by the caller and carried between frames.
    struct LoopGateState
    {
        float volume = 0.0f;     // the smoothed follower chasing the target
        float silentTime = 0.0f; // seconds the target has been continuously silent
    };

    // What the caller should do to the actual stream this frame.
    struct LoopGateDecision
    {
        bool start = false;       // begin the loop (caller seeds volume from appliedVolume)
        bool stop = false;        // release it — irreversible, 2s tail
        bool applyVolume = false; // push appliedVolume to the live handle
        float appliedVolume = 0.0f;
    };

    // `hasHandle` / `hasPath` are the caller's world; everything else is decided here.
    //
    // The volume is FROZEN while silent-but-uncommitted, never ramped down. That is not an
    // optimisation: StreamingAudioManager::startFadeOut captures baseVolume by reading back
    // the live AL_GAIN, so letting the follower decay first would base the 2s tail on a
    // near-zero gain and make the fade inaudible — exactly what seeding `volume = target` at
    // start was added to prevent at the other end of the loop's life.
    inline LoopGateDecision evaluateLoopGate(LoopGateState& state, float targetVolume,
                                             float deltaTime, bool hasHandle, bool hasPath,
                                             float rampSpeed)
    {
        LoopGateDecision out;

        const bool wantsLoop = targetVolume > LOOP_SILENCE_THRESHOLD;
        state.silentTime = wantsLoop ? 0.0f : state.silentTime + deltaTime;

        if (wantsLoop && !hasHandle && hasPath)
        {
            out.start = true;
            // Seeding is load-bearing: leave the follower at 0 and it would ramp 0 -> target
            // WHILE the engine ramps its fade-in 0 -> 1 over the same window, and the two
            // multiply into a squared onset. The engine owns the onset; the follower must
            // start already AT the target and only track it from here.
            state.volume = targetVolume;
        }

        if (wantsLoop)
        {
            const float diff = targetVolume - state.volume;
            const float step = rampSpeed * deltaTime;
            state.volume += diff < -step ? -step : (diff > step ? step : diff);

            out.applyVolume = hasHandle || out.start;
            out.appliedVolume = state.volume;
            return out;
        }

        // Silent. Hold the volume where it is and wait: a dip that recovers inside the hold
        // costs nothing, and one that does not gets a fade from a level worth fading from.
        out.appliedVolume = state.volume;
        if (state.silentTime >= LOOP_STOP_HOLD_SECONDS && hasHandle)
            out.stop = true;

        return out;
    }
}
