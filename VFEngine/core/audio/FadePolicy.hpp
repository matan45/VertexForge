#pragma once

// VK-1521 — audio fade-in + streaming source fades.
//
// Pure, header-only ramp core shared by BOTH fade paths: AudioSourceManager's pooled
// fadingQueue and StreamingAudioManager's fadingStreams. It answers one question —
// "given a ramp that has `remainingMs` left of `totalMs`, what multiplier is the source
// at right now?" — and nothing else. Queue state, pool slots and AL calls stay with the
// managers, exactly as VoicePolicy leaves the voice registry to AudioThread.
//
// AL-free / device-free (like VoicePolicy.hpp, BusDucking.hpp and OcclusionPolicy.hpp
// beside it) so the CPU-only Tests project can unit-test the ramp directly. Tests links
// neither Audio nor OpenAL and has no openal-soft includedir, so everything here MUST
// stay inline, header-only, AL-include-free, and must NOT be marked VF_AUDIO_API (that
// macro resolves to dllimport outside the DLL, which would leave Tests with unresolved
// externals).
//
// WHY THIS HEADER EXISTS AT ALL — the curve used to be written out three separate times
// (AudioSourceManager's updateFades, getFadeGain and setVolume each re-derived
// remainingMs/totalMs by hand). That is not a tidiness complaint: getFadeGain's result
// MUST equal the multiplier updateFades wrote into AL_GAIN, because AudioThread's
// publishSnapshot re-applies it explicitly for the bus meters (it passes sourceGain=1.0f
// and so never reads AL_GAIN) while the VK-1515 overlay row reads AL_GAIN back and must
// not. If the two expressions ever drift, the meters square the ramp. Routing every
// caller through fadeGain() below makes that invariant hold by construction rather than
// by a comment asking people to keep three formulas in step.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace core::audio::fade
{
    enum class FadeDirection : uint8_t
    {
        In,  // silent -> baseVolume; the ramp ENDS the fade, not the voice
        Out  // baseVolume -> silent; the ramp ends by releasing the voice
    };

    // Both directions share ONE storage layout: remainingMs counts DOWN from totalMs, so
    // t = remainingMs/totalMs runs 1 -> 0 over the ramp. Out maps straight to t (starts
    // full, ends silent); In is its exact mirror, 1 - t. Keeping one layout is what lets
    // a fade-out replace an in-flight fade-in in place, and what makes "fade-in is the
    // mirror of fade-out" a testable identity rather than a coincidence.
    //
    // The returned gain is the multiplier to write as `baseVolume * gain`, and is exactly
    // what getFadeGain must hand back (see the header note above).
    //
    // Linear in amplitude, deliberately: this is the pre-VK-1521 fade-out curve, so
    // existing callers are unchanged. A linear crossfade holds unity amplitude but dips
    // ~3 dB in power at the midpoint for uncorrelated sources; equal-power would fix that
    // and is a cheap follow-up precisely because the curve now lives in one function.
    [[nodiscard]] inline float fadeGain(FadeDirection dir, float remainingMs, float totalMs) noexcept
    {
        // Degenerate ramp (zero/negative/NaN duration, or a corrupted remainder): collapse
        // to the direction's END state. An un-rampable fade-in means "no fade" = full gain,
        // which is what fadeInMs = 0 must mean for every pre-VK-1521 scene; an un-rampable
        // fade-out means "already done" = silent, matching startFadeOut's release-now guard.
        if (!std::isfinite(totalMs) || totalMs <= 0.0f || !std::isfinite(remainingMs))
            return dir == FadeDirection::In ? 1.0f : 0.0f;

        const float t = std::clamp(remainingMs / totalMs, 0.0f, 1.0f);
        return dir == FadeDirection::In ? 1.0f - t : t;
    }

    // The full multiplier, including the ramp's starting point.
    //
    // startGain exists to keep TWO different things apart, whose conflation is a real bug:
    // the owner's `baseVolume` is the BUS-MULTIPLIED TARGET (userVolume * effectiveBusVolume),
    // rewritten by setVolume on every bus flush; startGain is where the RAMP began, as a
    // multiplier. It is 1.0 for an ordinary fade, and only differs when one ramp hands off to
    // another — a fade-out requested mid-fade-in must continue from the level the voice
    // audibly reached (say 0.3) rather than jumping to full and falling.
    //
    // The tempting shortcut — capture AL_GAIN into baseVolume at hand-off — is wrong, and
    // silently so: AL_GAIN is base*gain, so the next bus flush overwrites baseVolume with the
    // real target and the ramp jumps. That read-back only ever *looked* right because no ramp
    // was ever in flight when it ran.
    //
    // Whatever this returns is written as `baseVolume * rampGain(...)` AND handed back by
    // getFadeGain — the two must be this one call, never two expressions (see the header note).
    [[nodiscard]] inline float rampGain(FadeDirection dir, float startGain, float remainingMs,
                                        float totalMs) noexcept
    {
        return std::clamp(startGain, 0.0f, 1.0f) * fadeGain(dir, remainingMs, totalMs);
    }

    // The single admission test for "is this duration worth arming a ramp for?". Mirrors
    // the guard startFadeOut has always applied to fadeDurationMs (AudioSourceManager.cpp),
    // so fade-in and fade-out reject the same inputs rather than each inventing a rule.
    [[nodiscard]] inline bool isRampable(float durationMs) noexcept
    {
        return std::isfinite(durationMs) && durationMs > 0.0f;
    }
}
