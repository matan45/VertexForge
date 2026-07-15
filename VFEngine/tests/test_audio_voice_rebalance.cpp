#include <doctest.h>
#include <audio/VoicePolicy.hpp>

#include <algorithm>
#include <limits>
#include <random>
#include <vector>

// ============================================================
// VK-1515: the per-tick half of the voice budget (core/audio/VoicePolicy.hpp).
//
// test_audio_voice_policy.cpp covers decidePlay — who gets a real slot at PLAY time.
// This file covers rebalanceVoices — who KEEPS one as the listener and emitters move,
// which is what makes virtualization more than a fancy word for "dropped".
//
// The property that matters most here is the one hysteresis exists for: two voices of
// near-equal importance at the budget edge must NOT trade places every tick. A swap is
// an AL source release + acquire + seek + play, so flapping at the 200Hz tick rate is
// audible chatter, not a micro-optimisation.
//
// CPU-only, like its sibling: no OpenAL device, no audio thread, no virtual-voice
// registry — rebalanceVoices takes candidates and returns handles, nothing more.
// ============================================================

namespace
{
    using namespace core::audio;

    VoiceCandidate voice(uint64_t handle, uint8_t priority, float audibleGain)
    {
        VoiceCandidate v;
        v.handle = handle;
        v.priority = priority;
        v.audibleGain = audibleGain;
        return v;
    }

    bool contains(const std::vector<uint64_t>& v, uint64_t handle)
    {
        return std::find(v.begin(), v.end(), handle) != v.end();
    }

    // Neutral priority throughout unless a case is specifically about priority, so that
    // voiceScore() == audibleGain and the numbers in each test read as plain gains.
    VoiceCandidate at(uint64_t handle, float gain)
    {
        return voice(handle, kDefaultVoicePriority, gain);
    }
}

TEST_SUITE("AudioVoiceRebalance")
{
    // --- Free slots -----------------------------------------------------------

    TEST_CASE("a free slot promotes the best virtual voice with no margin required")
    {
        // One slot free, two virtual voices whose scores are a hair apart. Hysteresis
        // must NOT apply: it protects an incumbent, and an empty slot has none.
        const std::vector<VoiceCandidate> real{at(1, 0.9f)};
        const std::vector<VoiceCandidate> virt{at(2, 0.5f), at(3, 0.5001f)};

        const auto r = rebalanceVoices(real, virt, 2);

        CHECK(r.demote.empty());
        REQUIRE(r.promote.size() == 1);
        CHECK(r.promote[0] == 3); // the better of the two
    }

    TEST_CASE("free slots promote in importance order and stop at the cap")
    {
        const std::vector<VoiceCandidate> real{at(1, 0.9f)};
        const std::vector<VoiceCandidate> virt{at(2, 0.1f), at(3, 0.8f), at(4, 0.4f)};

        const auto r = rebalanceVoices(real, virt, 3);

        CHECK(r.demote.empty());
        REQUIRE(r.promote.size() == 2); // 2 free slots, 3 candidates
        CHECK(r.promote[0] == 3);
        CHECK(r.promote[1] == 4);
        CHECK_FALSE(contains(r.promote, 2)); // the quietest stays virtual
    }

    TEST_CASE("nothing to do: no virtual voices and room to spare")
    {
        const std::vector<VoiceCandidate> real{at(1, 0.9f), at(2, 0.5f)};

        const auto r = rebalanceVoices(real, {}, 8);

        CHECK(r.demote.empty());
        CHECK(r.promote.empty());
    }

    // --- Hysteresis: the anti-flap property -----------------------------------

    TEST_CASE("equal scores do not swap, so a voice at the budget edge cannot flap")
    {
        // The regression this whole term exists for. Without the margin these two trade
        // places every tick: each swap releases and re-acquires an AL source at 200Hz.
        const std::vector<VoiceCandidate> real{at(1, 0.5f)};
        const std::vector<VoiceCandidate> virt{at(2, 0.5f)};

        const auto r = rebalanceVoices(real, virt, 1);

        CHECK(r.demote.empty());
        CHECK(r.promote.empty());
    }

    TEST_CASE("a marginal win does not take an occupied slot")
    {
        // 20% better, but the margin is 25%.
        const std::vector<VoiceCandidate> real{at(1, 0.5f)};
        const std::vector<VoiceCandidate> virt{at(2, 0.6f)};

        const auto r = rebalanceVoices(real, virt, 1, kVoiceHysteresisRatio);

        CHECK(r.demote.empty());
        CHECK(r.promote.empty());
    }

    TEST_CASE("a decisive win takes the slot")
    {
        // 2x better, comfortably past the 25% margin.
        const std::vector<VoiceCandidate> real{at(1, 0.5f)};
        const std::vector<VoiceCandidate> virt{at(2, 1.0f)};

        const auto r = rebalanceVoices(real, virt, 1, kVoiceHysteresisRatio);

        REQUIRE(r.demote.size() == 1);
        REQUIRE(r.promote.size() == 1);
        CHECK(r.demote[0] == 1);
        CHECK(r.promote[0] == 2);
    }

    TEST_CASE("the margin is applied about the incumbent, both sides of the boundary")
    {
        const std::vector<VoiceCandidate> real{at(1, 0.4f)};

        // Just under 0.4 * 1.25 == 0.5 -> incumbent keeps the slot.
        CHECK(rebalanceVoices(real, std::vector<VoiceCandidate>{at(2, 0.499f)}, 1, 1.25f)
                  .promote.empty());
        // Just over -> it yields.
        CHECK(rebalanceVoices(real, std::vector<VoiceCandidate>{at(2, 0.501f)}, 1, 1.25f)
                  .promote.size() == 1);
    }

    TEST_CASE("a silent incumbent yields to any audible voice")
    {
        // worst * ratio == 0, so any gain at all clears it. A voice on a muted bus scores
        // 0 (AL_GAIN carries bus volume), and holding a slot for something inaudible is
        // exactly what the budget is meant to prevent.
        const std::vector<VoiceCandidate> real{at(1, 0.0f)};
        const std::vector<VoiceCandidate> virt{at(2, 0.001f)};

        const auto r = rebalanceVoices(real, virt, 1);

        REQUIRE(r.demote.size() == 1);
        CHECK(r.demote[0] == 1);
        REQUIRE(r.promote.size() == 1);
        CHECK(r.promote[0] == 2);
    }

    TEST_CASE("two silent voices do not swap")
    {
        // 0 > 0 * ratio is false — silence must not flap against silence.
        const std::vector<VoiceCandidate> real{at(1, 0.0f)};
        const std::vector<VoiceCandidate> virt{at(2, 0.0f)};

        const auto r = rebalanceVoices(real, virt, 1);

        CHECK(r.demote.empty());
        CHECK(r.promote.empty());
    }

    // --- Priority participates in the ranking ---------------------------------

    TEST_CASE("priority lets a quieter virtual voice reclaim a slot")
    {
        // Same gain, but 32 priority steps = two octaves = 4x weight (kPriorityStepsPerOctave
        // is 16). Well past the margin, so authored importance wins.
        const std::vector<VoiceCandidate> real{voice(1, kDefaultVoicePriority, 0.5f)};
        const std::vector<VoiceCandidate> virt{voice(2, kDefaultVoicePriority - 32, 0.5f)};

        const auto r = rebalanceVoices(real, virt, 1);

        REQUIRE(r.promote.size() == 1);
        CHECK(r.promote[0] == 2);
        REQUIRE(r.demote.size() == 1);
        CHECK(r.demote[0] == 1);
    }

    // --- Over-budget shedding -------------------------------------------------

    TEST_CASE("lowering the cap under live voices sheds the worst first")
    {
        // Reachable in normal use: ApplySettingsCmd can lower maxRealVoices at runtime.
        const std::vector<VoiceCandidate> real{at(1, 0.9f), at(2, 0.1f), at(3, 0.5f), at(4, 0.2f)};

        const auto r = rebalanceVoices(real, {}, 2);

        REQUIRE(r.demote.size() == 2);
        CHECK(contains(r.demote, 2)); // 0.1 — quietest
        CHECK(contains(r.demote, 4)); // 0.2
        CHECK_FALSE(contains(r.demote, 1));
        CHECK_FALSE(contains(r.demote, 3));
        CHECK(r.promote.empty());
    }

    TEST_CASE("shedding to the cap does not then refill from the virtual set")
    {
        // Over budget AND virtual voices waiting: the freed slots are not free, they are
        // being reclaimed. A promotion here would defeat the point of lowering the cap.
        const std::vector<VoiceCandidate> real{at(1, 0.9f), at(2, 0.8f), at(3, 0.7f)};
        const std::vector<VoiceCandidate> virt{at(4, 0.6f)};

        const auto r = rebalanceVoices(real, virt, 2);

        REQUIRE(r.demote.size() == 1);
        CHECK(r.demote[0] == 3); // the worst real voice
        CHECK(r.promote.empty()); // 0.6 is not decisively better than the new worst (0.8)
    }

    // --- Cap disabled ---------------------------------------------------------

    TEST_CASE("a cap of zero or less revives every virtual voice and demotes none")
    {
        const std::vector<VoiceCandidate> real{at(1, 0.9f), at(2, 0.1f)};
        const std::vector<VoiceCandidate> virt{at(3, 0.5f), at(4, 0.001f)};

        for (const int cap : {0, -1})
        {
            const auto r = rebalanceVoices(real, virt, cap);
            CHECK(r.demote.empty());
            CHECK(r.promote.size() == 2);
            CHECK(contains(r.promote, 3));
            CHECK(contains(r.promote, 4));
        }
    }

    // --- Determinism ----------------------------------------------------------

    TEST_CASE("the outcome does not depend on gather order")
    {
        // Candidates are gathered by iterating unordered_maps, so the incoming order is
        // arbitrary and may differ run to run. moreImportant is a total order (score, then
        // handle), so the result must not.
        std::vector<VoiceCandidate> real{at(1, 0.5f), at(2, 0.5f), at(3, 0.5f), at(4, 0.5f)};
        std::vector<VoiceCandidate> virt{at(5, 0.9f), at(6, 0.9f), at(7, 0.2f)};

        const auto expected = rebalanceVoices(real, virt, 4);
        REQUIRE_FALSE(expected.promote.empty()); // the case would be vacuous otherwise

        std::mt19937 rng(1515);
        for (int i = 0; i < 32; ++i)
        {
            std::shuffle(real.begin(), real.end(), rng);
            std::shuffle(virt.begin(), virt.end(), rng);

            const auto r = rebalanceVoices(real, virt, 4);
            CHECK(r.demote == expected.demote);
            CHECK(r.promote == expected.promote);
        }
    }

    TEST_CASE("equal scores tie-break on handle, so the older voice keeps its slot")
    {
        // All four reals tie at 0.5; two virtuals decisively beat them. The victims must be
        // the highest handles — same rule as decidePlay's, so a voice cannot be stolen by
        // one policy and preferred by the other.
        const std::vector<VoiceCandidate> real{at(1, 0.5f), at(2, 0.5f), at(3, 0.5f), at(4, 0.5f)};
        const std::vector<VoiceCandidate> virt{at(5, 1.0f), at(6, 1.0f)};

        const auto r = rebalanceVoices(real, virt, 4);

        REQUIRE(r.demote.size() == 2);
        CHECK(contains(r.demote, 4));
        CHECK(contains(r.demote, 3));
        REQUIRE(r.promote.size() == 2);
        CHECK(r.promote[0] == 5); // lower handle promoted first
        CHECK(r.promote[1] == 6);
    }

    // --- Robustness -----------------------------------------------------------

    TEST_CASE("a NaN gain never promotes and never breaks the ordering")
    {
        // voiceScore() sanitizes non-finite gains to 0 so the sort keeps a strict weak
        // ordering (a NaN comparator is UB in std::sort). A voice whose position has gone
        // bad must lose, not win.
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const std::vector<VoiceCandidate> real{at(1, 0.5f)};
        const std::vector<VoiceCandidate> virt{at(2, nan)};

        const auto r = rebalanceVoices(real, virt, 1);

        CHECK(r.demote.empty());
        CHECK(r.promote.empty());
    }

    TEST_CASE("a NaN incumbent is shed rather than held forever")
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const std::vector<VoiceCandidate> real{at(1, nan)};
        const std::vector<VoiceCandidate> virt{at(2, 0.5f)};

        const auto r = rebalanceVoices(real, virt, 1);

        REQUIRE(r.demote.size() == 1);
        CHECK(r.demote[0] == 1);
        REQUIRE(r.promote.size() == 1);
        CHECK(r.promote[0] == 2);
    }

    TEST_CASE("a NaN hysteresis ratio stops swaps instead of allowing them all")
    {
        // Spelled !(a > b) rather than (a <= b) precisely so this lands on "no swap".
        const std::vector<VoiceCandidate> real{at(1, 0.1f)};
        const std::vector<VoiceCandidate> virt{at(2, 1.0f)};

        const auto r = rebalanceVoices(real, virt, 1,
                                       std::numeric_limits<float>::quiet_NaN());

        CHECK(r.demote.empty());
        CHECK(r.promote.empty());
    }

    TEST_CASE("no voice is both demoted and promoted in one pass")
    {
        // Voices promoted into free slots are not eviction candidates in the same tick,
        // or a voice could flip twice and the caller's registry would corrupt.
        const std::vector<VoiceCandidate> real{at(1, 0.1f), at(2, 0.05f)};
        const std::vector<VoiceCandidate> virt{at(3, 1.0f), at(4, 0.9f), at(5, 0.8f)};

        const auto r = rebalanceVoices(real, virt, 4);

        for (const uint64_t h : r.demote)
            CHECK_FALSE(contains(r.promote, h));
        for (const uint64_t h : r.promote)
            CHECK_FALSE(contains(r.demote, h));
    }

    TEST_CASE("an empty scene is a no-op")
    {
        const auto r = rebalanceVoices({}, {}, 64);
        CHECK(r.demote.empty());
        CHECK(r.promote.empty());
    }
}
