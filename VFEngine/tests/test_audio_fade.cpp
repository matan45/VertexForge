#include <doctest.h>
#include <audio/FadePolicy.hpp>
#include <cmath>
#include <limits>

// ============================================================
// VK-1521: audio fade-in + streaming source fades. fade::fadeGain is the ONE
// expression of the ramp curve, shared by the pooled fade queue
// (AudioSourceManager) and the streaming one (StreamingAudioManager).
//
// Why these cases are worth more than they look: updateFades writes
// baseVolume * rampGain() straight into AL_GAIN, and getFadeGain() hands the SAME
// number back to publishSnapshot, which re-applies it for the bus meters. Those two
// used to be separate hand-written formulas (three of them, actually). If they ever
// disagree the meters square the ramp. Pinning the curve here is what keeps that a
// single function rather than a comment asking people to keep formulas in step.
//
// CPU-only: no OpenAL device, no AL headers. FadePolicy.hpp is header-only, pure and
// AL-free precisely so this suite can exist — Tests links neither Audio nor OpenAL.
// The queue invariants it feeds (one entry per handle, fade-in living in both
// registries, releaseSource purging the entry) need a live AL source and are NOT
// covered here; they are verified by inspection and in-editor.
// ============================================================

namespace
{
    using core::audio::fade::FadeDirection;
    using core::audio::fade::fadeGain;
    using core::audio::fade::isRampable;
    using core::audio::fade::rampGain;

    constexpr auto IN = FadeDirection::In;
    constexpr auto OUT = FadeDirection::Out;

    constexpr float NAN_F = std::numeric_limits<float>::quiet_NaN();
    constexpr float INF_F = std::numeric_limits<float>::infinity();

    // The queues store remaining time counting DOWN from total, so a ramp that has run
    // for `elapsed` of `total` has this much left. Tests read better in elapsed terms.
    constexpr float remainingAfter(float elapsedMs, float totalMs) { return totalMs - elapsedMs; }
}

TEST_SUITE("AudioFade")
{
    // ---------- back-compat: an un-armed fade must change nothing ----------

    TEST_CASE("a zero fade-in duration is inert and yields full gain")
    {
        // Every pre-VK-1521 scene plays with fadeInMs = 0. If this ever returned 0 the
        // whole engine would go silent, so it is the single most load-bearing case here.
        CHECK(fadeGain(IN, 0.0f, 0.0f) == doctest::Approx(1.0f));
        CHECK(isRampable(0.0f) == false);
    }

    TEST_CASE("a negative or non-finite fade duration is inert")
    {
        // Mirrors the guard startFadeOut has always applied to fadeDurationMs.
        CHECK(isRampable(-1.0f) == false);
        CHECK(isRampable(NAN_F) == false);
        CHECK(isRampable(INF_F) == false);
        CHECK(isRampable(0.001f) == true);
        CHECK(isRampable(300.0f) == true);
    }

    // ---------- endpoints ----------

    TEST_CASE("a fade-in starts at exactly silence and a fade-out starts at exactly full")
    {
        CHECK(fadeGain(IN, remainingAfter(0.0f, 1000.0f), 1000.0f) == doctest::Approx(0.0f));
        CHECK(fadeGain(OUT, remainingAfter(0.0f, 1000.0f), 1000.0f) == doctest::Approx(1.0f));
    }

    TEST_CASE("a completed ramp lands exactly on its endpoint")
    {
        // Exactness matters: updateFades settles a finished fade-in on baseVolume via this
        // value. 1.0 - epsilon would strand the voice fractionally quiet until the next
        // bus flush happened to rewrite it.
        CHECK(fadeGain(IN, remainingAfter(1000.0f, 1000.0f), 1000.0f) == 1.0f);
        CHECK(fadeGain(OUT, remainingAfter(1000.0f, 1000.0f), 1000.0f) == 0.0f);
    }

    // ---------- the curve itself ----------

    TEST_CASE("the ramp is linear in amplitude")
    {
        // VK-1521 deliberately keeps the pre-existing linear fade-out curve rather than
        // moving to equal-power. Half-elapsed is half-amplitude, both directions.
        CHECK(fadeGain(IN, remainingAfter(500.0f, 1000.0f), 1000.0f) == doctest::Approx(0.5f));
        CHECK(fadeGain(OUT, remainingAfter(500.0f, 1000.0f), 1000.0f) == doctest::Approx(0.5f));
        CHECK(fadeGain(IN, remainingAfter(250.0f, 1000.0f), 1000.0f) == doctest::Approx(0.25f));
        CHECK(fadeGain(OUT, remainingAfter(750.0f, 1000.0f), 1000.0f) == doctest::Approx(0.25f));
    }

    TEST_CASE("fade-out preserves the pre-VK-1521 remaining-over-total ramp exactly")
    {
        // The old updateFades wrote `baseVolume * (remainingMs / totalMs)` and the old
        // getFadeGain returned that same ratio. This is the "existing callers are
        // unchanged" guarantee, swept across the ramp.
        constexpr float total = 800.0f;
        for (int step = 0; step <= 10; ++step)
        {
            const float remaining = total * (static_cast<float>(step) / 10.0f);
            CHECK(fadeGain(OUT, remaining, total) == doctest::Approx(remaining / total));
        }
    }

    TEST_CASE("fade-in is the exact mirror of fade-out")
    {
        // The identity that lets one stored layout serve both directions, and the reason
        // a fade-out can replace an in-flight fade-in in place.
        constexpr float total = 640.0f;
        for (int step = 0; step <= 16; ++step)
        {
            const float remaining = total * (static_cast<float>(step) / 16.0f);
            CHECK((fadeGain(IN, remaining, total) + fadeGain(OUT, remaining, total))
                  == doctest::Approx(1.0f));
        }
    }

    TEST_CASE("a symmetric linear crossfade holds unity amplitude throughout")
    {
        // Documents what linear-only buys and costs: the amplitudes always sum to 1, but
        // for uncorrelated sources the POWER dips ~3 dB at the midpoint (0.5^2 + 0.5^2 =
        // 0.5). That dip is the known, accepted trade of not going equal-power.
        constexpr float total = 2000.0f;
        for (int step = 0; step <= 20; ++step)
        {
            const float remaining = total * (static_cast<float>(step) / 20.0f);
            const float incoming = fadeGain(IN, remaining, total);
            const float outgoing = fadeGain(OUT, remaining, total);
            CHECK((incoming + outgoing) == doctest::Approx(1.0f));
        }
        // The midpoint power dip, stated explicitly so a future equal-power change has a
        // test that visibly flips rather than one that silently still passes.
        const float mid = fadeGain(IN, remainingAfter(1000.0f, total), total);
        CHECK((mid * mid + mid * mid) == doctest::Approx(0.5f));
    }

    TEST_CASE("gain is monotonic across a fine sweep in both directions")
    {
        constexpr float total = 1000.0f;
        constexpr int steps = 1000;
        float prevIn = -1.0f;
        float prevOut = 2.0f;
        for (int step = 0; step <= steps; ++step)
        {
            const float elapsed = total * (static_cast<float>(step) / steps);
            const float remaining = remainingAfter(elapsed, total);
            const float gIn = fadeGain(IN, remaining, total);
            const float gOut = fadeGain(OUT, remaining, total);
            CHECK(gIn >= prevIn);   // non-decreasing as it fades in
            CHECK(gOut <= prevOut); // non-increasing as it fades out
            prevIn = gIn;
            prevOut = gOut;
        }
        CHECK(prevIn == doctest::Approx(1.0f));
        CHECK(prevOut == doctest::Approx(0.0f));
    }

    // ---------- robustness: nothing bad may reach AL_GAIN ----------

    TEST_CASE("gain is clamped to the unit range beyond the ramp")
    {
        // A frame hitch (a Development-build breakpoint is a multi-second frame) drives
        // remainingMs negative in one tick. It must land on the endpoint, not overshoot.
        CHECK(fadeGain(OUT, -500.0f, 1000.0f) == doctest::Approx(0.0f));
        CHECK(fadeGain(IN, -500.0f, 1000.0f) == doctest::Approx(1.0f));
        // remaining > total should never happen, but must not overshoot either.
        CHECK(fadeGain(OUT, 5000.0f, 1000.0f) == doctest::Approx(1.0f));
        CHECK(fadeGain(IN, 5000.0f, 1000.0f) == doctest::Approx(0.0f));
    }

    TEST_CASE("a degenerate duration collapses to the direction's end state")
    {
        // An un-rampable fade-in means "no fade" = full; an un-rampable fade-out means
        // "already done" = silent, matching startFadeOut's release-immediately guard.
        for (const float total : {0.0f, -1.0f, NAN_F, INF_F})
        {
            CHECK(fadeGain(IN, 100.0f, total) == doctest::Approx(1.0f));
            CHECK(fadeGain(OUT, 100.0f, total) == doctest::Approx(0.0f));
        }
    }

    TEST_CASE("a non-finite remainder never leaks a NaN into the gain")
    {
        for (const float remaining : {NAN_F, INF_F, -INF_F})
        {
            const float gIn = fadeGain(IN, remaining, 1000.0f);
            const float gOut = fadeGain(OUT, remaining, 1000.0f);
            CHECK(std::isfinite(gIn));
            CHECK(std::isfinite(gOut));
        }
    }

    TEST_CASE("a very long duration still yields a finite in-range gain")
    {
        const float g = fadeGain(IN, 1.0e9f, 1.0e9f);
        CHECK(std::isfinite(g));
        CHECK(g >= 0.0f);
        CHECK(g <= 1.0f);
    }

    // ---------- rampGain: base volume vs start gain ----------

    TEST_CASE("an ordinary ramp has a start gain of one and matches the bare curve")
    {
        constexpr float total = 1000.0f;
        const float remaining = remainingAfter(300.0f, total);
        CHECK(rampGain(IN, 1.0f, remaining, total) == doctest::Approx(fadeGain(IN, remaining, total)));
        CHECK(rampGain(OUT, 1.0f, remaining, total) == doctest::Approx(fadeGain(OUT, remaining, total)));
    }

    TEST_CASE("base volume and ramp gain compose multiplicatively")
    {
        // THE guard on the bus-flush stomp. AL_GAIN is baseVolume * rampGain, and the bus
        // rewrites baseVolume mid-ramp on every tick it is dirty (continuously, while a bus
        // ducks). The ramp must scale whatever base it is handed rather than being replaced
        // by it — so re-basing to a new bus volume changes the level but never the shape.
        constexpr float total = 1000.0f;
        const float remaining = remainingAfter(500.0f, total);
        const float g = rampGain(OUT, 1.0f, remaining, total);

        CHECK((0.8f * g) == doctest::Approx(0.4f)); // half-way through, at base 0.8
        CHECK((0.2f * g) == doctest::Approx(0.1f)); // bus ducked the base; same ramp position

        // The caller-side half of this (that updateFades/setVolume actually multiply rather
        // than overwrite) needs a live AL source and is NOT covered here — see the header
        // note. What IS pinned: rampGain takes no base, so it cannot conflate the two. That
        // separation is the fix — the old hand-off captured AL_GAIN (= base * gain) AS the
        // new base, which the next bus flush then silently overwrote, jumping the ramp.
        CHECK(g == doctest::Approx(0.5f));
    }

    TEST_CASE("a fade-out handed off mid-fade-in starts from the gain already reached")
    {
        // The no-pop guarantee for a crossfade interrupted early. A fade-in 30% of the way
        // up sits at gain 0.3; the fade-out that replaces it must begin at exactly 0.3 and
        // reach 0 — never jumping to full first.
        constexpr float inTotal = 1000.0f;
        const float reached = fadeGain(IN, remainingAfter(300.0f, inTotal), inTotal);
        CHECK(reached == doctest::Approx(0.3f));

        constexpr float outTotal = 500.0f;
        CHECK(rampGain(OUT, reached, outTotal, outTotal) == doctest::Approx(0.3f));
        CHECK(rampGain(OUT, reached, remainingAfter(250.0f, outTotal), outTotal)
              == doctest::Approx(0.15f));
        CHECK(rampGain(OUT, reached, remainingAfter(outTotal, outTotal), outTotal)
              == doctest::Approx(0.0f));
    }

    TEST_CASE("a hand-off never rises above the gain it started from")
    {
        constexpr float outTotal = 400.0f;
        constexpr float reached = 0.42f;
        float prev = reached + 0.001f;
        for (int step = 0; step <= 20; ++step)
        {
            const float rem = outTotal * (1.0f - static_cast<float>(step) / 20.0f);
            const float g = rampGain(OUT, reached, rem, outTotal);
            CHECK(g <= doctest::Approx(reached));
            CHECK(g <= prev);
            prev = g;
        }
        CHECK(prev == doctest::Approx(0.0f));
    }

    TEST_CASE("a hand-off from a completed fade-in behaves as an ordinary fade-out")
    {
        // Guarantees the everyday "play with a fade-in, later fade it out" case is not
        // perturbed by the hand-off path existing.
        constexpr float inTotal = 1000.0f;
        const float reached = fadeGain(IN, remainingAfter(inTotal, inTotal), inTotal);
        CHECK(reached == 1.0f);

        constexpr float outTotal = 300.0f;
        for (int step = 0; step <= 6; ++step)
        {
            const float rem = outTotal * (static_cast<float>(step) / 6.0f);
            CHECK(rampGain(OUT, reached, rem, outTotal) == doctest::Approx(fadeGain(OUT, rem, outTotal)));
        }
    }

    TEST_CASE("a start gain outside the unit range is clamped rather than amplifying")
    {
        constexpr float total = 100.0f;
        CHECK(rampGain(OUT, 5.0f, total, total) == doctest::Approx(1.0f));
        CHECK(rampGain(OUT, -1.0f, total, total) == doctest::Approx(0.0f));
    }
}
