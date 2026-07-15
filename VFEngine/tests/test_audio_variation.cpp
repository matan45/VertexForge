#include <doctest.h>
#include <types/AudioVariationTypes.hpp>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <vector>

// ============================================================
// VK-1520: audio variation containers. types::selectVariant picks which clip a
// source plays (Single / Random with no-immediate-repeat / RoundRobin), and
// types::audioJitterPitch/Volume roll the per-play pitch and volume jitter that
// stops repeated footsteps sounding machine-gun identical.
//
// CPU-only: no OpenAL device, no EnTT registry — the helpers are header-only,
// pure, and take a pre-rolled random draw. That is what lets these cases assert
// EXACT indices and sweep the no-repeat rule exhaustively instead of sampling
// histograms and hoping.
// ============================================================

namespace
{
    constexpr auto NONE = types::AUDIO_VARIANT_NONE;
    constexpr auto SINGLE = types::AudioPlayOrder::Single;
    constexpr auto RANDOM = types::AudioPlayOrder::Random;
    constexpr auto ROUND_ROBIN = types::AudioPlayOrder::RoundRobin;
}

TEST_SUITE("AudioVariation")
{
    // ---------- selection ----------

    TEST_CASE("Single always plays pool[0] (== audioRef), whatever the draw")
    {
        // The back-compat path: every pre-VK-1520 scene defaults to Single, so it
        // must land on audioRef for every possible random draw.
        for (int i = 0; i <= 100; ++i)
        {
            const float u = static_cast<float>(i) / 100.0f;
            CHECK(types::selectVariant(5, NONE, SINGLE, u) == 0);
            CHECK(types::selectVariant(5, 3, SINGLE, u) == 0);
        }
    }

    TEST_CASE("an empty pool yields AUDIO_VARIANT_NONE in every order")
    {
        CHECK(types::selectVariant(0, NONE, SINGLE, 0.5f) == NONE);
        CHECK(types::selectVariant(0, NONE, RANDOM, 0.5f) == NONE);
        CHECK(types::selectVariant(0, NONE, ROUND_ROBIN, 0.5f) == NONE);
    }

    TEST_CASE("first play is a full N-way draw and CAN pick variant 0")
    {
        // Guards the sentinel bug: with lastVariant defaulting to 0 rather than
        // 0xFF, no-immediate-repeat would exclude variant 0 from every first play
        // and a 5-variant footstep loop could never start on its own first clip.
        CHECK(types::selectVariant(5, NONE, RANDOM, 0.0f) == 0);
        CHECK(types::selectVariant(5, NONE, RANDOM, 0.19f) == 0);
        CHECK(types::selectVariant(5, NONE, RANDOM, 0.2f) == 1);
        CHECK(types::selectVariant(5, NONE, RANDOM, 0.99f) == 4);
        // audioVarianceUnit's upper bound is CLOSED, so 1.0f is reachable and
        // must land in the top bucket rather than run off the end.
        CHECK(types::selectVariant(5, NONE, RANDOM, 1.0f) == 4);
    }

    TEST_CASE("Random never returns lastVariant, over every N and every draw")
    {
        // Exhaustive, not sampled — the whole point of the pre-rolled-float
        // signature. This is the invariant the ticket's Verify line rests on.
        for (std::size_t n = 2; n <= 8; ++n)
        {
            for (std::size_t last = 0; last < n; ++last)
            {
                const auto lastU8 = static_cast<uint8_t>(last);
                for (int step = 0; step <= 1024; ++step)
                {
                    const float u = static_cast<float>(step) / 1024.0f;
                    const uint8_t pick = types::selectVariant(n, lastU8, RANDOM, u);
                    REQUIRE(pick != lastU8);
                    REQUIRE(static_cast<std::size_t>(pick) < n);
                }
            }
        }
    }

    TEST_CASE("the exclusion remap stays uniform over the N-1 legal candidates")
    {
        // Proves the bijection empirically: skipping past lastVariant must not
        // bias the survivors (a naive `if (pick == last) pick = 0;` would double
        // index 0's share).
        constexpr int steps = 4096;
        std::vector<int> hits(5, 0);
        for (int step = 0; step < steps; ++step)
        {
            const float u = static_cast<float>(step) / static_cast<float>(steps);
            hits[types::selectVariant(5, 2, RANDOM, u)]++;
        }

        CHECK(hits[2] == 0); // the excluded one, exactly never
        const int expected = steps / 4;
        for (const int idx : {0, 1, 3, 4})
        {
            CHECK(std::abs(hits[idx] - expected) <= 1);
        }
    }

    TEST_CASE("a single-clip pool repeats it rather than going silent")
    {
        // No-immediate-repeat is unsatisfiable at N == 1. Documented behaviour:
        // play the clip. Silence would be the worse surprise.
        CHECK(types::selectVariant(1, NONE, RANDOM, 0.5f) == 0);
        CHECK(types::selectVariant(1, 0, RANDOM, 0.5f) == 0);
        CHECK(types::selectVariant(1, 0, ROUND_ROBIN, 0.5f) == 0);
    }

    TEST_CASE("an out-of-range lastVariant is treated as never-played")
    {
        // Covers all three ways it happens: the sentinel, a list the user shrank
        // in the drawer, and garbage from a hand-edited scene.
        for (const uint8_t last : {NONE, uint8_t{3}, uint8_t{200}})
        {
            CHECK(types::selectVariant(3, last, RANDOM, 0.5f) < 3);
            // A full N-way draw can reach 0; the exclude-last path could not if it
            // wrongly believed last == 0.
            CHECK(types::selectVariant(3, last, RANDOM, 0.0f) == 0);
        }
    }

    TEST_CASE("RoundRobin walks the pool and wraps, starting at 0")
    {
        // NaN as the draw proves RoundRobin ignores it entirely.
        const float nan = std::numeric_limits<float>::quiet_NaN();
        CHECK(types::selectVariant(3, NONE, ROUND_ROBIN, nan) == 0);
        CHECK(types::selectVariant(3, 0, ROUND_ROBIN, nan) == 1);
        CHECK(types::selectVariant(3, 1, ROUND_ROBIN, nan) == 2);
        CHECK(types::selectVariant(3, 2, ROUND_ROBIN, nan) == 0);
    }

    TEST_CASE("RoundRobin restarts at 0 when the pool shrinks under it")
    {
        // Guards the (0xFF + 1) % 3 == 1 trap: the sentinel must be caught BEFORE
        // the modulo, or the very first play skips variant 0.
        CHECK(types::selectVariant(3, 4, ROUND_ROBIN, 0.5f) == 0);
        CHECK(types::selectVariant(3, NONE, ROUND_ROBIN, 0.5f) == 0);
    }

    // ---------- randomness ----------

    TEST_CASE("audioVarianceUnit is deterministic, bounded, and stream-separated")
    {
        const uint32_t seed = 0x12345678u;

        CHECK(types::audioVarianceUnit(seed, types::AudioVarianceStream::Pitch) ==
              types::audioVarianceUnit(seed, types::AudioVarianceStream::Pitch));

        for (uint32_t i = 0; i < 256; ++i)
        {
            const float u = types::audioVarianceUnit(types::audioPcgHash(i),
                                                     types::AudioVarianceStream::ClipSelect);
            REQUIRE(u >= 0.0f);
            REQUIRE(u <= 1.0f);

            const float s = types::audioVarianceSigned(types::audioPcgHash(i),
                                                       types::AudioVarianceStream::Pitch);
            REQUIRE(s >= -1.0f);
            REQUIRE(s <= 1.0f);
        }

        // Guards the copy-paste that reuses one stream for all three draws, which
        // would lock clip choice, pitch and volume in lockstep.
        int distinct = 0;
        for (uint32_t i = 0; i < 64; ++i)
        {
            const uint32_t s = types::audioPlaySeed(1u, i);
            if (types::audioVarianceUnit(s, types::AudioVarianceStream::ClipSelect) !=
                types::audioVarianceUnit(s, types::AudioVarianceStream::Pitch))
            {
                ++distinct;
            }
        }
        // Not `== 64`: audioPcgHash is a bijection on uint32 but the float
        // conversion is lossy, so a freak collision is possible (~2e-6 over 64
        // draws). A shared-stream bug would give 0, so this bound is just as
        // discriminating without the fragility.
        CHECK(distinct >= 60);
    }

    // ---------- jitter ----------

    TEST_CASE("zero variation is bit-exact — the back-compat guarantee")
    {
        // Every pre-VK-1520 scene defaults to variation 0 and MUST play
        // byte-identically, for any draw the roll happens to produce.
        for (int i = -10; i <= 10; ++i)
        {
            const float r = static_cast<float>(i) / 10.0f;
            CHECK(types::audioJitterPitch(1.0f, 0.0f, r) == 1.0f);
            CHECK(types::audioJitterPitch(0.75f, 0.0f, r) == 0.75f);
            CHECK(types::audioJitterVolume(1.0f, 0.0f, r) == 1.0f);
            CHECK(types::audioJitterVolume(0.3f, 0.0f, r) == 0.3f);
        }
    }

    TEST_CASE("jitter is multiplicative and symmetric, not additive")
    {
        CHECK(types::audioJitterPitch(1.0f, 0.1f, 1.0f) == doctest::Approx(1.1f));
        CHECK(types::audioJitterPitch(1.0f, 0.1f, -1.0f) == doctest::Approx(0.9f));
        CHECK(types::audioJitterPitch(1.0f, 0.1f, 0.0f) == doctest::Approx(1.0f));

        // The discriminating case: multiplicative gives 2.2, additive would give
        // 2.1. A designer asking for +/-10% must get 10% at any authored pitch.
        CHECK(types::audioJitterPitch(2.0f, 0.1f, 1.0f) == doctest::Approx(2.2f));
    }

    TEST_CASE("jitter clamps pitch above zero — a zero-pitch voice never ends")
    {
        // alSourcef(AL_PITCH, 0) is ACCEPTED (openal-soft guards `>= 0`), and the mixer
        // then floors the step at 1/65536 speed, so the voice holds its pooled slot for
        // hours. 0.5 * (1 + 1.0*-1) reaches exactly 0, so this clamp is load-bearing.
        const float p = types::audioJitterPitch(0.5f, 1.0f, -1.0f);
        CHECK(p > 0.0f);
        CHECK(p == doctest::Approx(types::AUDIO_MIN_PITCH));

        CHECK(types::audioJitterPitch(3.0f, 1.0f, 1.0f) == doctest::Approx(types::AUDIO_MAX_PITCH));
    }

    TEST_CASE("audioClampPitch rails an authored pitch that never went through jitter")
    {
        // The jitter path is not the only writer: VFX sequence steps, world-sector
        // restore and the service layer all hand a raw authored pitch to PlaySoundCmd.
        // Pitch 0 is the one that matters — AL accepts it, so nothing downstream
        // complains while the voice becomes immortal (its virtual clock scales by pitch
        // and stops advancing).
        CHECK(types::audioClampPitch(0.0f) == doctest::Approx(types::AUDIO_MIN_PITCH));
        // Negative is rejected by AL, which only logs — the source keeps its previous
        // pitch while the record keeps the negative, and they disagree from then on.
        CHECK(types::audioClampPitch(-1.0f) == doctest::Approx(types::AUDIO_MIN_PITCH));
        CHECK(types::audioClampPitch(100.0f) == doctest::Approx(types::AUDIO_MAX_PITCH));

        // In range, untouched — the common case must not be perturbed.
        CHECK(types::audioClampPitch(1.0f) == doctest::Approx(1.0f));
        CHECK(types::audioClampPitch(0.5f) == doctest::Approx(0.5f));
        CHECK(types::audioClampPitch(2.0f) == doctest::Approx(2.0f));

        // NaN is not merely clamped-by-luck: `!(pitch > lo)` is false for NaN, so it
        // lands on the floor rather than propagating into AL_PITCH and the clock.
        CHECK(types::audioClampPitch(std::numeric_limits<float>::quiet_NaN())
              == doctest::Approx(types::AUDIO_MIN_PITCH));
    }

    TEST_CASE("volume jitter respects the [0,1] rails and keeps a muted source muted")
    {
        CHECK(types::audioJitterVolume(1.0f, 0.2f, -1.0f) == doctest::Approx(0.8f));
        // The documented asymmetry: at the authored ceiling, variation only ducks.
        CHECK(types::audioJitterVolume(1.0f, 0.2f, 1.0f) == doctest::Approx(1.0f));
        // Multiplicative keeps 0 at 0 — additive would un-mute a silenced source.
        CHECK(types::audioJitterVolume(0.0f, 0.5f, 1.0f) == doctest::Approx(0.0f));
    }

    // ---------- the ticket's acceptance criterion ----------

    TEST_CASE("VK-1520 verify: a 5-variant footstep loop never repeats a clip twice in a row")
    {
        // The ticket's Verify line, read honestly. "Clip never repeats" is a hard
        // guarantee and is asserted below. "Pitch never repeats" is NOT asserted:
        // pitch is a float from a ~continuous distribution, so pitch[i] !=
        // pitch[i-1] would test an accident of float density rather than a
        // designed property. The line's real intent — "must not sound machine-gun
        // identical" — is (a) no immediate clip repeat and (b) pitch genuinely
        // varying, both asserted here.
        constexpr uint32_t plays = 10000u;
        constexpr uint32_t entityId = 42u;

        uint8_t last = NONE;
        std::vector<int> hits(5, 0);
        std::set<int> distinctPitches;

        for (uint32_t i = 0; i < plays; ++i)
        {
            const uint32_t seed = types::audioPlaySeed(entityId, i);
            const uint8_t pick = types::selectVariant(
                5, last, RANDOM, types::audioVarianceUnit(seed, types::AudioVarianceStream::ClipSelect));
            const float pitch = types::audioJitterPitch(
                1.0f, 0.1f, types::audioVarianceSigned(seed, types::AudioVarianceStream::Pitch));

            REQUIRE(pick < 5);
            REQUIRE(pick != last); // the clip never repeats back-to-back
            REQUIRE(pitch >= 0.9f - 1e-5f);
            REQUIRE(pitch <= 1.1f + 1e-5f); // 1.0 +/- 10%

            hits[pick]++;
            distinctPitches.insert(static_cast<int>(pitch * 1000.0f));
            last = pick;
        }

        // All 5 clips stay reachable — the no-repeat rule must not collapse the
        // set (a buggy remap could strand a variant).
        for (int i = 0; i < 5; ++i)
        {
            CHECK(hits[i] > 0);
        }
        // ...and roughly evenly. Each clip is legal on 4/5 of plays and then one
        // of 4 candidates, so the steady state is ~1/5 each.
        for (int i = 0; i < 5; ++i)
        {
            CHECK(hits[i] > static_cast<int>(plays / 10u));
        }

        // The load-bearing one: without this, a jitter that returned a constant
        // would satisfy every assertion above vacuously while the footsteps still
        // sounded machine-gun identical.
        CHECK(distinctPitches.size() > 100);
    }

    TEST_CASE("consecutive picks do not correlate through lastVariant alone")
    {
        // Regression guard for the seeding bug: seeding from lastVariant instead
        // of a per-play counter makes (last -> next) a FIXED function, so the loop
        // degenerates into a cycle of length <= N (0,3,1,4,2,0,3,1,4,2...) while
        // still passing every no-repeat assertion above.
        std::map<uint8_t, std::set<uint8_t>> successors;
        uint8_t last = NONE;

        for (uint32_t i = 0; i < 1000; ++i)
        {
            const uint32_t seed = types::audioPlaySeed(7u, i);
            const uint8_t pick = types::selectVariant(
                5, last, RANDOM, types::audioVarianceUnit(seed, types::AudioVarianceStream::ClipSelect));
            if (last != NONE)
            {
                successors[last].insert(pick);
            }
            last = pick;
        }

        // With a correct counter seed every `last` reaches all 4 other variants;
        // with a lastVariant-only seed each would map to exactly one successor.
        REQUIRE(successors.size() == 5);
        for (const auto& [from, tos] : successors)
        {
            CHECK(tos.size() >= 2);
            CHECK(tos.count(from) == 0); // and never itself
        }
    }
}
