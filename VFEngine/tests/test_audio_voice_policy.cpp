#include <doctest.h>
#include <audio/VoicePolicy.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <vector>

// ============================================================
// VK-1513: audio voice limiting + priority (core/audio/VoicePolicy.hpp).
//
//   * The attenuation model, which mirrors openal-soft's CalcAttnSourceParams
//     (dependencies/openal-soft/alc/alu.cpp ~:1763-1846) rather than the OpenAL 1.1
//     spec prose. Four cases below pin divergences where the spec-intuitive answer is
//     wrong — if openal-soft is ever bumped, these are the tests that will catch drift.
//   * The ranking: priority direction, 128 as an exact neutral, and the deliberate
//     deviation from Unity's strict-priority rule (both halves of the trade).
//   * The decision: Allow / Deny / Steal, victim identity, and determinism.
//
// CPU-only: no OpenAL device, no audio thread — the policy is a pure header. Tests
// links neither Audio nor OpenAL, which is exactly why VoicePolicy.hpp must stay
// header-only and AL-free.
// ============================================================

namespace
{
    using namespace core::audio;

    constexpr float kEps = 1e-5f;

    AttenuationParams atten(types::AudioDistanceModel model, float ref, float max, float rolloff)
    {
        AttenuationParams p;
        p.model = model;
        p.refDistance = ref;
        p.maxDistance = max;
        p.rolloffFactor = rolloff;
        return p;
    }

    VoiceCandidate voice(uint64_t handle, uint8_t priority, float audibleGain)
    {
        VoiceCandidate v;
        v.handle = handle;
        v.priority = priority;
        v.audibleGain = audibleGain;
        return v;
    }

    // A 2D / un-attenuated voice: gain passes straight through.
    AttenuationParams noAtten()
    {
        return atten(types::AudioDistanceModel::None, 1.0f, 100.0f, 1.0f);
    }
}

TEST_SUITE("AudioVoicePolicy")
{
    // --- Attenuation: fidelity to openal-soft ---------------------------------

    TEST_CASE("attenuation: the None model leaves gain untouched")
    {
        CHECK(distanceAttenuation(noAtten(), 1000.0f) == doctest::Approx(1.0f));
        CHECK(estimateAudibleGain(0.5f, noAtten(), 1000.0f) == doctest::Approx(0.5f));
    }

    TEST_CASE("attenuation: inverse-distance matches the openal-soft formula")
    {
        // alu.cpp:1795-1798 — gain = ref / lerpf(ref, d, rolloff).
        const AttenuationParams p = atten(types::AudioDistanceModel::InverseDistance, 1.0f, 100.0f, 1.0f);
        CHECK(distanceAttenuation(p, 1.0f) == doctest::Approx(1.0f));
        CHECK(distanceAttenuation(p, 2.0f) == doctest::Approx(0.5f));
        CHECK(distanceAttenuation(p, 3.0f) == doctest::Approx(1.0f / 3.0f));
    }

    TEST_CASE("attenuation: inverse-distance-clamped pins distance at the reference")
    {
        const AttenuationParams clamped =
            atten(types::AudioDistanceModel::InverseDistanceClamped, 1.0f, 100.0f, 1.0f);
        const AttenuationParams open =
            atten(types::AudioDistanceModel::InverseDistance, 1.0f, 100.0f, 1.0f);

        // Closer than refDistance: the clamped model holds at unity...
        CHECK(distanceAttenuation(clamped, 0.1f) == doctest::Approx(1.0f));
        // ...while the unclamped one amplifies (10x here) before the final gain clamp.
        CHECK(distanceAttenuation(open, 0.1f) == doctest::Approx(10.0f));

        // Beyond maxDistance the clamped model stops attenuating further.
        CHECK(distanceAttenuation(clamped, 500.0f) == doctest::Approx(distanceAttenuation(clamped, 100.0f)));
    }

    TEST_CASE("attenuation: linear-distance reaches silence at max distance")
    {
        // alu.cpp:1818-1820.
        const AttenuationParams p = atten(types::AudioDistanceModel::LinearDistance, 1.0f, 11.0f, 1.0f);
        CHECK(distanceAttenuation(p, 1.0f) == doctest::Approx(1.0f));
        CHECK(distanceAttenuation(p, 6.0f) == doctest::Approx(0.5f));
        CHECK(distanceAttenuation(p, 11.0f) == doctest::Approx(0.0f));
    }

    TEST_CASE("attenuation: unclamped linear below reference exceeds unity, the estimate clamps it")
    {
        // alu.cpp:1818-1820: scale goes negative, so 1 - scale*rolloff > 1. Real AL
        // behaviour, only ever clipped later by MaxGain.
        const AttenuationParams p = atten(types::AudioDistanceModel::LinearDistance, 1.0f, 11.0f, 1.0f);
        CHECK(distanceAttenuation(p, 0.0f) == doctest::Approx(1.1f));
        CHECK(estimateAudibleGain(1.0f, p, 0.0f) == doctest::Approx(1.0f));
    }

    TEST_CASE("attenuation: exponent-distance halves per doubling at rolloff 1")
    {
        // alu.cpp:1834-1835 — gain = (d / ref) ^ -rolloff.
        CHECK(distanceAttenuation(atten(types::AudioDistanceModel::ExponentDistance, 1.0f, 100.0f, 1.0f), 2.0f)
              == doctest::Approx(0.5f));
        CHECK(distanceAttenuation(atten(types::AudioDistanceModel::ExponentDistance, 1.0f, 100.0f, 2.0f), 2.0f)
              == doctest::Approx(0.25f));
    }

    TEST_CASE("attenuation: maxDistance below refDistance disables a clamped model entirely")
    {
        // alu.cpp:1770-1771 — the counter-intuitive one. An inverted range does NOT clamp
        // oddly, it switches attenuation OFF: a source 1000 m away plays at full volume.
        const AttenuationParams p =
            atten(types::AudioDistanceModel::InverseDistanceClamped, 10.0f, 1.0f, 1.0f);
        CHECK(distanceAttenuation(p, 1000.0f) == doctest::Approx(1.0f));
        CHECK(estimateAudibleGain(1.0f, p, 1000.0f) == doctest::Approx(1.0f));
    }

    TEST_CASE("attenuation: a refDistance of zero disables inverse and exponent")
    {
        // alu.cpp:1793 and :1832.
        CHECK(distanceAttenuation(atten(types::AudioDistanceModel::InverseDistance, 0.0f, 100.0f, 1.0f), 50.0f)
              == doctest::Approx(1.0f));
        CHECK(distanceAttenuation(atten(types::AudioDistanceModel::ExponentDistance, 0.0f, 100.0f, 1.0f), 50.0f)
              == doctest::Approx(1.0f));
    }

    TEST_CASE("attenuation: a maxDistance equal to refDistance disables linear")
    {
        // alu.cpp:1816.
        CHECK(distanceAttenuation(atten(types::AudioDistanceModel::LinearDistance, 5.0f, 5.0f, 1.0f), 50.0f)
              == doctest::Approx(1.0f));
    }

    TEST_CASE("attenuation: estimateAudibleGain clamps into [0, 1]")
    {
        // AL_MIN_GAIN / AL_MAX_GAIN defaults (alu.cpp:1880-1883); the engine never sets them.
        CHECK(estimateAudibleGain(4.0f, noAtten(), 0.0f) == doctest::Approx(1.0f));
        CHECK(estimateAudibleGain(-1.0f, noAtten(), 0.0f) == doctest::Approx(0.0f));
    }

    TEST_CASE("attenuation: non-finite inputs sanitize to silence, diverging from openal-soft")
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const AttenuationParams p = atten(types::AudioDistanceModel::InverseDistanceClamped, 1.0f, 100.0f, 1.0f);

        // Pins WHY the guard in estimateAudibleGain exists. Raw, this mirrors alu.cpp: a NaN
        // makes every `if (dist > 0)` guard false, so it falls through to no attenuation at
        // all — a garbage-positioned voice reads as FULL volume.
        CHECK(distanceAttenuation(p, nan) == doctest::Approx(1.0f));

        // For ranking that is backwards: it would make the broken voice the most audible in
        // the scene and therefore un-stealable. The estimate scores it silent instead, so it
        // is the first thing evicted.
        CHECK(estimateAudibleGain(1.0f, p, nan) == doctest::Approx(0.0f));
        CHECK(estimateAudibleGain(nan, p, 10.0f) == doctest::Approx(0.0f));
    }

    // --- Ranking ---------------------------------------------------------------

    TEST_CASE("priority: 128 is exactly neutral so score equals audible gain")
    {
        // The behaviour-preservation proof: every call site that never authors a priority
        // keeps ranking purely on audibility, as it does today.
        // exp2(0) is exactly 1.0, so this is an equality check on purpose, not an Approx.
        CHECK(priorityWeight(kDefaultVoicePriority) == 1.0f);
        CHECK(voiceScore(voice(1, kDefaultVoicePriority, 0.37f)) == 0.37f);
    }

    TEST_CASE("priority: lower value means more important")
    {
        // Direction pin. 0 = critical, 255 = least. Opposite to ReverbZoneComponent,
        // matching VFXComponent and Unity/FMOD.
        CHECK(priorityWeight(0) > priorityWeight(128));
        CHECK(priorityWeight(128) > priorityWeight(255));
        CHECK(moreImportant(voice(1, 0, 0.5f), voice(2, 255, 0.5f)));
        CHECK_FALSE(moreImportant(voice(2, 255, 0.5f), voice(1, 0, 0.5f)));
    }

    TEST_CASE("priority: the full range spans roughly the dynamic range of 16-bit audio")
    {
        // 6.02 dB per 16 steps => ~96 dB across 0..255. This is what lets priority
        // override any AUDIBLE gain difference while still losing below the noise floor.
        CHECK(priorityWeight(0) == doctest::Approx(256.0f));
        const float rangeDb = 20.0f * std::log10(priorityWeight(0) / priorityWeight(255));
        CHECK(rangeDb > 95.0f);
        CHECK(rangeDb < 97.0f);
    }

    // --- Decisions -------------------------------------------------------------

    TEST_CASE("policy: under the cap a request is always allowed")
    {
        const std::vector<VoiceCandidate> live{voice(1, 128, 1.0f), voice(2, 128, 1.0f)};
        const VoiceDecision d = decidePlay(live, voice(9, 128, 0.01f), 64);
        CHECK(d.kind == VoiceDecisionKind::Allow);
    }

    TEST_CASE("policy: a cap of zero or less disables the limit")
    {
        std::vector<VoiceCandidate> live;
        for (uint64_t i = 1; i <= 200; ++i)
            live.push_back(voice(i, 128, 1.0f));

        CHECK(decidePlay(live, voice(999, 255, 0.0f), 0).kind == VoiceDecisionKind::Allow);
        CHECK(decidePlay(live, voice(999, 255, 0.0f), -1).kind == VoiceDecisionKind::Allow);
    }

    TEST_CASE("policy: at the cap a louder request steals the quietest voice")
    {
        const std::vector<VoiceCandidate> live{
            voice(1, 128, 1.00f), voice(2, 128, 0.75f), voice(3, 128, 0.10f), voice(4, 128, 0.50f)};

        const VoiceDecision d = decidePlay(live, voice(9, 128, 0.90f), 4);
        CHECK(d.kind == VoiceDecisionKind::Steal);
        // The victim is the least important overall, not merely the last one gathered.
        CHECK(d.victim == 3);
    }

    TEST_CASE("policy: at the cap a quieter request is denied")
    {
        const std::vector<VoiceCandidate> live{
            voice(1, 128, 1.00f), voice(2, 128, 0.75f), voice(3, 128, 0.50f)};

        const VoiceDecision d = decidePlay(live, voice(9, 128, 0.20f), 3);
        CHECK(d.kind == VoiceDecisionKind::Deny);
        CHECK(d.victim == kInvalidVoiceHandle);
    }

    TEST_CASE("policy: priority overrides gain within its authority")
    {
        // A near-silent critical sound (0.05 * 256 = 12.8) still outranks a full-volume
        // default one (1.0). This is the case that keeps a quiet music bed un-stealable.
        const std::vector<VoiceCandidate> live{voice(1, 128, 1.0f)};
        const VoiceDecision d = decidePlay(live, voice(9, 0, 0.05f), 1);
        CHECK(d.kind == VoiceDecisionKind::Steal);
        CHECK(d.victim == 1);
    }

    TEST_CASE("policy: gain wins beyond priority's authority (deliberate Unity deviation)")
    {
        // Unity would let this critical-but-inaudible sound (1e-5 * 256 = 2.6e-3) win on
        // strict priority, starving the audible one. We rank it below. Documented
        // divergence — a horde of distant high-priority sounds must not starve nearby ones.
        const std::vector<VoiceCandidate> live{voice(1, 128, 1.0f)};
        const VoiceDecision d = decidePlay(live, voice(9, 0, 1e-5f), 1);
        CHECK(d.kind == VoiceDecisionKind::Deny);
    }

    TEST_CASE("policy: 2D voices outrank attenuated 3D voices with no special casing")
    {
        // A 2D voice is scored at distance 0 (it is AL_SOURCE_RELATIVE at the origin), so
        // its gain passes through undiminished and it floats to the top naturally.
        const AttenuationParams p = atten(types::AudioDistanceModel::InverseDistanceClamped, 1.0f, 100.0f, 1.0f);
        const float ui = estimateAudibleGain(1.0f, noAtten(), 0.0f);       // 2D UI click
        const float distant3D = estimateAudibleGain(1.0f, p, 80.0f);       // far 3D sound

        CHECK(ui > distant3D);
        CHECK(moreImportant(voice(1, 128, ui), voice(2, 128, distant3D)));
    }

    TEST_CASE("policy: equal scores tie-break on handle, deterministically under shuffle")
    {
        // All-identical voices (a retrigger flood): selection must not depend on the
        // caller's gather order, which comes off an unordered_map.
        std::vector<VoiceCandidate> live;
        for (uint64_t i = 1; i <= 8; ++i)
            live.push_back(voice(i, 128, 0.5f));

        std::mt19937 rng(1513);
        for (int trial = 0; trial < 16; ++trial)
        {
            std::shuffle(live.begin(), live.end(), rng);
            const VoiceDecision d = decidePlay(live, voice(99, 128, 0.9f), 8);
            CHECK(d.kind == VoiceDecisionKind::Steal);
            // Highest handle loses the tie: the oldest (lowest-handled) voice survives.
            CHECK(d.victim == 8);
        }
    }

    TEST_CASE("policy: an exact score tie does not steal")
    {
        // Incoming handles are minted monotonically, so an incoming voice always has the
        // highest handle and therefore loses an exact tie. No churn on identical sounds.
        const std::vector<VoiceCandidate> live{voice(1, 128, 0.5f)};
        CHECK(decidePlay(live, voice(9, 128, 0.5f), 1).kind == VoiceDecisionKind::Deny);
    }

    TEST_CASE("policy: a NaN gain never wins and never breaks the ordering")
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        CHECK(voiceScore(voice(1, 0, nan)) == doctest::Approx(0.0f));

        // Strict-weak-ordering sanity: a sanitized NaN must not compare "more important"
        // in both directions, which would be UB under std::sort.
        const VoiceCandidate bad = voice(1, 0, nan);
        const VoiceCandidate good = voice(2, 128, 0.5f);
        CHECK_FALSE(moreImportant(bad, good));
        CHECK(moreImportant(good, bad));

        // A garbage-positioned voice is the first thing stolen.
        const std::vector<VoiceCandidate> live{voice(1, 128, 0.5f), voice(2, 0, nan)};
        const VoiceDecision d = decidePlay(live, voice(9, 128, 0.4f), 2);
        CHECK(d.kind == VoiceDecisionKind::Steal);
        CHECK(d.victim == 2);
    }
}
