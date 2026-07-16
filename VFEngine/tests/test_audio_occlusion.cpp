#include <doctest.h>
#include <audio/OcclusionPolicy.hpp>

// ============================================================
// VK-1518: geometry occlusion policy (core/audio/OcclusionPolicy.hpp).
//
//   * smoothOcclusion   — attack/release one-pole glide of the raycast verdict, run on the
//     audio thread at ~200 Hz. Must land exactly on its target (both the hitch clamp and the
//     snap), because AudioSource::updateDistanceFilter's detach early-out tests for == 0.
//   * occlusionGain     — maps occlusion + CUT AMOUNT to a multiplicative gain. The
//     occlusionGain(0, x) == 1.0f invariant is what keeps the filter path bit-for-bit
//     identical to pre-VK-1518 when occlusion is off.
//   * occlusionFromRay  — binary verdict, with the self-hit skin that stops an emitter's own
//     collider reading as a wall.
//
// CPU-only: no OpenAL device, no physics world — the helpers are pure.
// ============================================================

namespace
{
    namespace occlusion = core::audio::occlusion;

    // The audio thread's tick (AudioThread waits 5 ms per loop => ~200 Hz).
    constexpr float kAudioDt = 1.0f / 200.0f;

    // Steps until the glide lands EXACTLY on its target, or -1 if it never does.
    int stepsToSettle(float from, float to, float dt, float rate)
    {
        float v = from;
        for (int i = 1; i <= 100000; ++i)
        {
            v = occlusion::smoothOcclusion(v, to, dt, rate);
            if (v == to)
            {
                return i;
            }
        }
        return -1;
    }
}

TEST_SUITE("AudioOcclusion")
{
    TEST_CASE("smoothOcclusion: dt <= 0 holds the current value")
    {
        // The audio thread's first tick has dt ~0; a paused frame has dt == 0.
        CHECK(occlusion::smoothOcclusion(0.5f, 1.0f, 0.0f, 8.0f) == 0.5f);
        CHECK(occlusion::smoothOcclusion(0.5f, 1.0f, -0.1f, 8.0f) == 0.5f);
    }

    TEST_CASE("smoothOcclusion: rate <= 0 holds the current value")
    {
        CHECK(occlusion::smoothOcclusion(0.5f, 1.0f, kAudioDt, 0.0f) == 0.5f);
        CHECK(occlusion::smoothOcclusion(0.5f, 1.0f, kAudioDt, -1.0f) == 0.5f);
    }

    TEST_CASE("smoothOcclusion: approaches monotonically and never overshoots")
    {
        float v = 0.0f;
        for (int i = 0; i < 500; ++i)
        {
            const float next = occlusion::smoothOcclusion(v, 1.0f, kAudioDt, occlusion::kAttackRate);
            CHECK(next >= v);
            CHECK(next <= 1.0f);
            v = next;
        }

        v = 1.0f;
        for (int i = 0; i < 500; ++i)
        {
            const float next = occlusion::smoothOcclusion(v, 0.0f, kAudioDt, occlusion::kReleaseRate);
            CHECK(next <= v);
            CHECK(next >= 0.0f);
            v = next;
        }
    }

    TEST_CASE("smoothOcclusion: a frame hitch lands exactly on target, it does not ring")
    {
        // 300 ms at rate 8 => dt * rate == 2.4, so the lerp factor clamps to 1. Without that
        // clamp the step would overshoot to 1.4 and oscillate.
        CHECK(occlusion::smoothOcclusion(0.0f, 1.0f, 0.3f, 8.0f) == 1.0f);
        CHECK(occlusion::smoothOcclusion(1.0f, 0.0f, 0.3f, 8.0f) == 0.0f);
    }

    TEST_CASE("smoothOcclusion: snaps to the target exactly, not asymptotically")
    {
        // Load-bearing: updateDistanceFilter only detaches the direct filter once occlusion
        // reads <= 0. A one-pole alone would never get there.
        CHECK(stepsToSettle(1.0f, 0.0f, kAudioDt, occlusion::kReleaseRate) > 0);
        CHECK(stepsToSettle(0.0f, 1.0f, kAudioDt, occlusion::kAttackRate) > 0);
    }

    TEST_CASE("smoothOcclusion: release is faster than attack")
    {
        const int attackSteps = stepsToSettle(0.0f, 1.0f, kAudioDt, occlusion::kAttackRate);
        const int releaseSteps = stepsToSettle(1.0f, 0.0f, kAudioDt, occlusion::kReleaseRate);
        REQUIRE(attackSteps > 0);
        REQUIRE(releaseSteps > 0);
        CHECK(releaseSteps < attackSteps);
    }

    TEST_CASE("smoothOcclusion: already-at-target is a fixed point")
    {
        CHECK(occlusion::smoothOcclusion(0.0f, 0.0f, kAudioDt, occlusion::kAttackRate) == 0.0f);
        CHECK(occlusion::smoothOcclusion(1.0f, 1.0f, kAudioDt, occlusion::kAttackRate) == 1.0f);
        CHECK(occlusion::smoothOcclusion(0.42f, 0.42f, kAudioDt, occlusion::kAttackRate) == 0.42f);
    }

    TEST_CASE("smoothOcclusion: out-of-range inputs are clamped")
    {
        CHECK(occlusion::smoothOcclusion(2.0f, 2.0f, kAudioDt, 8.0f) == 1.0f);
        CHECK(occlusion::smoothOcclusion(-1.0f, -1.0f, kAudioDt, 8.0f) == 0.0f);
        CHECK(occlusion::smoothOcclusion(-1.0f, 1.0f, 0.0f, 8.0f) == 0.0f);
    }

    // ---- occlusionGain: the knobs are CUT AMOUNTS (0 = inert), not gains ----

    TEST_CASE("occlusionGain: zero occlusion is exactly inert for every amount")
    {
        // THE invariant. updateDistanceFilter multiplies its two AL writes by this, so an
        // un-occluded source must reduce bit-for-bit to the pre-VK-1518 (currentGainHF, 1.0f).
        for (int i = 0; i <= 20; ++i)
        {
            const float amount = static_cast<float>(i) * 0.05f;
            CHECK(occlusion::occlusionGain(0.0f, amount) == 1.0f);
        }
    }

    TEST_CASE("occlusionGain: a zero amount is inert at every occlusion")
    {
        // Pins the pooled-source reset value. The amounts reset to 0 (no cut) on recycle —
        // NOT to 1, which a gain-based reading would use and which would leave every
        // recycled voice fully muffled.
        for (int i = 0; i <= 20; ++i)
        {
            const float occ = static_cast<float>(i) * 0.05f;
            CHECK(occlusion::occlusionGain(occ, 0.0f) == 1.0f);
        }
    }

    TEST_CASE("occlusionGain: full occlusion cuts by exactly the amount")
    {
        // The component defaults: occlusionLpf 0.7 => HF gain 0.30 (-10.5 dB, audibly
        // muffled); occlusionVolume 0.3 => gain 0.70 (-3.1 dB, slightly quieter).
        CHECK(occlusion::occlusionGain(1.0f, 0.7f) == doctest::Approx(0.3f));
        CHECK(occlusion::occlusionGain(1.0f, 0.3f) == doctest::Approx(0.7f));
        CHECK(occlusion::occlusionGain(1.0f, 1.0f) == 0.0f);
    }

    TEST_CASE("occlusionGain: partial occlusion is linear")
    {
        CHECK(occlusion::occlusionGain(0.5f, 0.7f) == doctest::Approx(0.65f));
        CHECK(occlusion::occlusionGain(0.25f, 0.8f) == doctest::Approx(0.8f));
    }

    TEST_CASE("occlusionGain: out-of-range inputs are clamped")
    {
        CHECK(occlusion::occlusionGain(2.0f, 0.7f) == occlusion::occlusionGain(1.0f, 0.7f));
        CHECK(occlusion::occlusionGain(-1.0f, 0.7f) == 1.0f);
        CHECK(occlusion::occlusionGain(0.5f, 2.0f) == occlusion::occlusionGain(0.5f, 1.0f));
        CHECK(occlusion::occlusionGain(0.5f, -1.0f) == 1.0f);
    }

    // ---- occlusionFromRay ----

    TEST_CASE("occlusionFromRay: a clear line of sight is not occluded")
    {
        CHECK(occlusion::occlusionFromRay(false, 0.0f, 10.0f, occlusion::kSelfHitSkin) == 0.0f);
    }

    TEST_CASE("occlusionFromRay: geometry between listener and emitter occludes")
    {
        CHECK(occlusion::occlusionFromRay(true, 5.0f, 10.0f, occlusion::kSelfHitSkin) == 1.0f);
    }

    TEST_CASE("occlusionFromRay: the emitter's own collider does not occlude it")
    {
        // Without the skin every emitter that owns a collider would read as fully occluded.
        CHECK(occlusion::occlusionFromRay(true, 10.0f, 10.0f, occlusion::kSelfHitSkin) == 0.0f);
        CHECK(occlusion::occlusionFromRay(true, 9.9f, 10.0f, occlusion::kSelfHitSkin) == 0.0f);
    }

    TEST_CASE("occlusionFromRay: the skin boundary is exclusive")
    {
        constexpr float skin = 0.2f;
        CHECK(occlusion::occlusionFromRay(true, 10.0f - skin, 10.0f, skin) == 0.0f);
        CHECK(occlusion::occlusionFromRay(true, 9.7f, 10.0f, skin) == 1.0f);
    }

    TEST_CASE("occlusionFromRay: an emitter closer than the skin is never occluded")
    {
        // No room for a wall between us, and (emitterDistance - skin) would be negative.
        CHECK(occlusion::occlusionFromRay(true, 0.05f, 0.1f, 0.2f) == 0.0f);
        CHECK(occlusion::occlusionFromRay(true, 0.0f, 0.2f, 0.2f) == 0.0f);
    }

    // ---- golden: VK-1518 changes nothing when occlusion is off ----

    TEST_CASE("golden: an un-occluded source's filter math is bit-for-bit unchanged")
    {
        // updateDistanceFilter writes clamp(currentGainHF * occlusionGain(occ, lpf)) and
        // occlusionGain(occ, volume). At occ == 0 those must collapse to the old pair,
        // (currentGainHF, 1.0f), for every authored amount.
        for (int g = 0; g <= 20; ++g)
        {
            const float currentGainHF = 0.1f + static_cast<float>(g) * 0.045f;
            for (int a = 0; a <= 10; ++a)
            {
                const float amount = static_cast<float>(a) * 0.1f;
                CHECK(currentGainHF * occlusion::occlusionGain(0.0f, amount) == currentGainHF);
                CHECK(occlusion::occlusionGain(0.0f, amount) == 1.0f);
            }
        }
    }
}
