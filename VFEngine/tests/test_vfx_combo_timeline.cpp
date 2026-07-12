#include <doctest.h>

#include <vfx/VFXComboTimeline.hpp>
#include <vfx/VFXSequenceTypes.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <string>
#include <vector>

// ============================================================
// VK-1451 (Part 1): VFXComboTimeline is the pure, entt-free scheduler shared by the
// runtime combo service and the editor composited preview. It is the single source
// of truth for "which steps spawn/stop/fire at simulated time t, with which derived
// seed/local transform". These tests exercise it directly — no dispatcher, no Vulkan.
// ============================================================

namespace
{
    using namespace vfx;

    VFXSequenceStep timeStep(float startTime, bool loop = false, float duration = 0.0f,
                             VFXStepStopMode stopMode = VFXStepStopMode::PlayToCompletion)
    {
        VFXSequenceStep s;
        s.startTime = startTime;
        s.loop = loop;
        s.duration = duration;
        s.stopMode = stopMode;
        return s;
    }

    VFXSequenceStep cueStep(const std::string& cue)
    {
        VFXSequenceStep s;
        s.cueName = cue;
        return s;
    }

    // Collect only the spawned step indices (in emission order) from an event list.
    std::vector<int> spawnIndices(const std::vector<ComboEvent>& events)
    {
        std::vector<int> out;
        for (const auto& e : events)
            if (e.kind == ComboEventKind::SpawnStep)
                out.push_back(e.stepIndex);
        return out;
    }
}

TEST_SUITE("VFXComboTimeline")
{
    TEST_CASE("deriveSeed is stable, distinct per step, and never zero")
    {
        CHECK(VFXComboTimeline::deriveSeed(12345u, 0) == VFXComboTimeline::deriveSeed(12345u, 0));
        CHECK(VFXComboTimeline::deriveSeed(12345u, 0) != VFXComboTimeline::deriveSeed(12345u, 1));
        CHECK(VFXComboTimeline::deriveSeed(1u, 0) != VFXComboTimeline::deriveSeed(2u, 0));

        // 0 combo seed must still derive usable, non-zero, distinct per-step seeds.
        CHECK(VFXComboTimeline::deriveSeed(0u, 0) != 0u);
        CHECK(VFXComboTimeline::deriveSeed(0u, 1) != 0u);
        CHECK(VFXComboTimeline::deriveSeed(0u, 0) != VFXComboTimeline::deriveSeed(0u, 1));
        for (int i = 0; i < 64; ++i)
            CHECK(VFXComboTimeline::deriveSeed(99u, i) != 0u);
    }

    TEST_CASE("time-driven steps spawn in time order as the clock crosses their start")
    {
        VFXSequenceData data;
        data.steps.push_back(timeStep(0.0f));
        data.steps.push_back(timeStep(0.5f));
        data.steps.push_back(timeStep(1.0f));

        VFXComboTimeline tl;
        tl.reset(data, 42u);

        std::vector<ComboEvent> ev;
        tl.advance(0.6f, ev); // crosses step 0 (t=0) and step 1 (t=0.5)
        CHECK(spawnIndices(ev) == std::vector<int>{0, 1});

        ev.clear();
        tl.advance(0.5f, ev); // elapsed 1.1 crosses step 2
        CHECK(spawnIndices(ev) == std::vector<int>{2});

        CHECK(tl.allStepsSpawned());
    }

    TEST_CASE("deterministic replay: chunking dt differently yields the same spawn stream")
    {
        VFXSequenceData data;
        data.steps.push_back(timeStep(0.0f));
        data.steps.push_back(timeStep(0.5f));
        data.steps.push_back(timeStep(1.0f));

        // One big advance.
        VFXComboTimeline a;
        a.reset(data, 7u);
        std::vector<ComboEvent> evA;
        a.advance(2.0f, evA);

        // Many small advances to the same elapsed.
        VFXComboTimeline b;
        b.reset(data, 7u);
        std::vector<ComboEvent> evB;
        for (int i = 0; i < 20; ++i)
            b.advance(0.1f, evB);

        CHECK(spawnIndices(evA) == std::vector<int>{0, 1, 2});
        CHECK(spawnIndices(evB) == std::vector<int>{0, 1, 2});

        // Same seed => identical derived child seeds; both fully spawned.
        for (int i = 0; i < 3; ++i)
            CHECK(a.derivedSeed(i) == b.derivedSeed(i));
        CHECK(a.allStepsSpawned());
        CHECK(b.allStepsSpawned());
    }

    TEST_CASE("rewind clears crossing state so a re-advance re-derives the same schedule")
    {
        VFXSequenceData data;
        data.steps.push_back(timeStep(0.0f));
        data.steps.push_back(timeStep(0.5f));
        data.steps.push_back(timeStep(1.0f));

        VFXComboTimeline tl;
        tl.reset(data, 1u);

        std::vector<ComboEvent> ev;
        tl.advance(2.0f, ev);
        REQUIRE(tl.allStepsSpawned());

        tl.rewind();
        CHECK(tl.elapsed() == doctest::Approx(0.0f));
        CHECK_FALSE(tl.isSpawned(0));
        CHECK_FALSE(tl.allStepsSpawned());

        ev.clear();
        tl.advance(0.6f, ev);
        CHECK(spawnIndices(ev) == std::vector<int>{0, 1});
        CHECK(tl.isSpawned(0));
        CHECK(tl.isSpawned(1));
        CHECK_FALSE(tl.isSpawned(2));
    }

    TEST_CASE("StopAfterDuration emits a StopStep once when the window ends")
    {
        VFXSequenceData data;
        data.steps.push_back(timeStep(0.0f, /*loop*/ false, /*duration*/ 0.5f,
                                      VFXStepStopMode::StopAfterDuration));

        VFXComboTimeline tl;
        tl.reset(data, 3u);

        std::vector<ComboEvent> ev;
        tl.advance(0.1f, ev); // spawn only
        CHECK(spawnIndices(ev) == std::vector<int>{0});
        CHECK(std::count_if(ev.begin(), ev.end(),
                            [](const ComboEvent& e) { return e.kind == ComboEventKind::StopStep; }) == 0);

        ev.clear();
        tl.advance(0.5f, ev); // elapsed 0.6 >= 0 + 0.5 => stop
        REQUIRE(ev.size() == 1);
        CHECK(ev[0].kind == ComboEventKind::StopStep);
        CHECK(ev[0].stepIndex == 0);
        CHECK(tl.isStopped(0));

        ev.clear();
        tl.advance(1.0f, ev); // no second stop
        CHECK(ev.empty());
    }

    TEST_CASE("event markers fire matching cues once and re-fire after rewind")
    {
        VFXSequenceData data;
        data.steps.push_back(cueStep("boom"));     // index 0, cue-driven
        data.steps.push_back(timeStep(0.0f));      // index 1, time-driven (control)
        data.eventMarkers.push_back(VFXSequenceEventMarker{0.3f, "boom"});

        VFXComboTimeline tl;
        tl.reset(data, 5u);

        std::vector<ComboEvent> ev;
        tl.advance(0.1f, ev); // marker at 0.3 not crossed; only the time step spawns
        CHECK(spawnIndices(ev) == std::vector<int>{1});
        CHECK_FALSE(tl.isSpawned(0));

        ev.clear();
        tl.advance(0.3f, ev); // elapsed 0.4 crosses the marker => cue step 0 spawns
        CHECK(spawnIndices(ev) == std::vector<int>{0});
        REQUIRE(ev.size() == 1);
        CHECK(ev[0].sourceMarker == 0);
        REQUIRE(tl.firedMarkers().size() == 1);
        CHECK(tl.firedMarkers()[0]);
        CHECK(tl.isSpawned(0));

        ev.clear();
        tl.advance(1.0f, ev); // marker already fired => nothing
        CHECK(ev.empty());

        tl.rewind();
        ev.clear();
        tl.advance(0.4f, ev); // marker fires again after rewind
        std::vector<int> idx = spawnIndices(ev);
        CHECK(std::find(idx.begin(), idx.end(), 0) != idx.end());
    }

    TEST_CASE("a marker whose cue matches no step spawns nothing")
    {
        VFXSequenceData data;
        data.steps.push_back(cueStep("boom"));
        data.eventMarkers.push_back(VFXSequenceEventMarker{0.1f, "nomatch"});

        VFXComboTimeline tl;
        tl.reset(data, 9u);

        std::vector<ComboEvent> ev;
        tl.advance(0.5f, ev);
        CHECK(ev.empty());
        CHECK_FALSE(tl.isSpawned(0));
        REQUIRE(tl.firedMarkers().size() == 1);
        CHECK(tl.firedMarkers()[0]);
    }

    TEST_CASE("fireCue spawns matching not-yet-spawned cue steps and is idempotent")
    {
        VFXSequenceData data;
        data.steps.push_back(cueStep("hit"));   // 0
        data.steps.push_back(cueStep("hit"));   // 1, same cue (fan-out)
        data.steps.push_back(cueStep("other")); // 2

        VFXComboTimeline tl;
        tl.reset(data, 11u);

        std::vector<ComboEvent> ev;
        tl.fireCue("hit", ev);
        CHECK(spawnIndices(ev) == std::vector<int>{0, 1});
        REQUIRE(ev.size() == 2);
        CHECK(ev[0].sourceMarker == -1);
        CHECK(ev[1].sourceMarker == -1);

        ev.clear();
        tl.fireCue("hit", ev); // already spawned => nothing
        CHECK(ev.empty());

        ev.clear();
        tl.fireCue("other", ev);
        CHECK(spawnIndices(ev) == std::vector<int>{2});
    }

    TEST_CASE("localTransform matches the canonical step TRS composition")
    {
        VFXSequenceData data;
        VFXSequenceStep s = timeStep(0.0f);
        s.localPosition = glm::vec3(2.0f, 3.0f, 4.0f);
        s.localEulerDeg = glm::vec3(0.0f, 90.0f, 0.0f);
        s.localScale = glm::vec3(2.0f, 2.0f, 2.0f);
        data.steps.push_back(s);

        VFXComboTimeline tl;
        tl.reset(data, 1u);

        const glm::mat4 expected =
            glm::translate(glm::mat4(1.0f), s.localPosition) *
            glm::mat4_cast(glm::quat(glm::radians(s.localEulerDeg))) *
            glm::scale(glm::mat4(1.0f), s.localScale);

        const glm::mat4 got = tl.localTransform(0);
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                CHECK(got[c][r] == doctest::Approx(expected[c][r]));
    }

    // ============================================================
    // VK-1497 — deterministic per-step variety (probability + variant groups).
    // ============================================================

    TEST_CASE("VK-1497: probability 0 and 1 are exact play/skip edges")
    {
        VFXSequenceData data;
        VFXSequenceStep always = timeStep(0.0f);
        always.probability = 1.0f;
        VFXSequenceStep never = timeStep(0.0f);
        never.probability = 0.0f;
        data.steps.push_back(always); // 0
        data.steps.push_back(never);  // 1

        const std::vector<bool> plays = VFXComboTimeline::resolvePlays(data, 42u);
        REQUIRE(plays.size() == 2);
        CHECK(plays[0]);
        CHECK_FALSE(plays[1]);

        VFXComboTimeline tl;
        tl.reset(data, 42u);
        std::vector<ComboEvent> ev;
        tl.advance(1.0f, ev);
        CHECK(spawnIndices(ev) == std::vector<int>{0}); // the p=0 step never spawns
        CHECK_FALSE(tl.isPlaying(1));
        CHECK(tl.allStepsSpawned());                    // the skipped step must not stall completion
    }

    TEST_CASE("VK-1497: variety resolves identically across dt-chunking and rewind")
    {
        VFXSequenceData data;
        VFXSequenceStep a = timeStep(0.0f);
        a.probability = 0.5f;                 // 0 — ungrouped mid-probability
        VFXSequenceStep g0 = timeStep(0.2f);
        g0.variantGroup = 7;                  // 1 } variant group 7
        VFXSequenceStep g1 = timeStep(0.4f);
        g1.variantGroup = 7;                  // 2 }
        VFXSequenceStep g2 = timeStep(0.6f);
        g2.variantGroup = 7;                  // 3 }
        VFXSequenceStep plain = timeStep(0.8f); // 4 — always plays
        data.steps.push_back(a);
        data.steps.push_back(g0);
        data.steps.push_back(g1);
        data.steps.push_back(g2);
        data.steps.push_back(plain);

        const uint32_t seed = 20260712u;

        auto sortedSpawns = [](const std::vector<ComboEvent>& ev) {
            std::vector<int> idx = spawnIndices(ev);
            std::sort(idx.begin(), idx.end());
            return idx;
        };

        // One big advance.
        VFXComboTimeline big;
        big.reset(data, seed);
        std::vector<ComboEvent> evBig;
        big.advance(5.0f, evBig);

        // Many small advances to the same elapsed.
        VFXComboTimeline small;
        small.reset(data, seed);
        std::vector<ComboEvent> evSmall;
        for (int i = 0; i < 100; ++i)
            small.advance(0.05f, evSmall);

        // Play, then rewind and replay from t=0 (the seek / prewarm path).
        VFXComboTimeline re;
        re.reset(data, seed);
        std::vector<ComboEvent> junk;
        re.advance(5.0f, junk);
        re.rewind();
        std::vector<ComboEvent> evRe;
        re.advance(5.0f, evRe);

        const std::vector<int> idxBig = sortedSpawns(evBig);
        CHECK(idxBig == sortedSpawns(evSmall));
        CHECK(idxBig == sortedSpawns(evRe));

        // Exactly one group-7 member {1,2,3} plays; the always-play step 4 is present.
        int groupCount = 0;
        for (int i : idxBig)
            if (i == 1 || i == 2 || i == 3)
                ++groupCount;
        CHECK(groupCount == 1);
        CHECK(std::find(idxBig.begin(), idxBig.end(), 4) != idxBig.end());
    }

    TEST_CASE("VK-1497: a variant group yields exactly one winner for every seed")
    {
        VFXSequenceData data;
        for (int k = 0; k < 3; ++k)
        {
            VFXSequenceStep s = timeStep(0.0f);
            s.variantGroup = 0;
            data.steps.push_back(s);
        }

        int winnerCounts[3] = {0, 0, 0};
        for (uint32_t seed = 1; seed <= 200; ++seed)
        {
            const std::vector<bool> plays = VFXComboTimeline::resolvePlays(data, seed);
            REQUIRE(plays.size() == 3);
            int count = 0;
            int winner = -1;
            for (int i = 0; i < 3; ++i)
                if (plays[i])
                {
                    ++count;
                    winner = i;
                }
            CHECK(count == 1); // exactly one member plays
            if (winner >= 0)
                ++winnerCounts[winner];
        }
        // Non-degenerate: over many seeds every member wins at least once.
        CHECK(winnerCounts[0] > 0);
        CHECK(winnerCounts[1] > 0);
        CHECK(winnerCounts[2] > 0);
    }

    TEST_CASE("VK-1497: grouped members ignore probability — one still plays when all are 0")
    {
        VFXSequenceData data;
        for (int k = 0; k < 3; ++k)
        {
            VFXSequenceStep s = timeStep(0.0f);
            s.variantGroup = 2;
            s.probability = 0.0f; // ignored inside a group
            data.steps.push_back(s);
        }

        for (uint32_t seed = 1; seed <= 50; ++seed)
        {
            const std::vector<bool> plays = VFXComboTimeline::resolvePlays(data, seed);
            int count = 0;
            for (bool b : plays)
                if (b)
                    ++count;
            CHECK(count == 1);
        }
    }

    TEST_CASE("VK-1497: different seeds vary both the group winner and the ungrouped roll")
    {
        VFXSequenceData data;
        VFXSequenceStep mid = timeStep(0.0f);
        mid.probability = 0.5f;              // 0 — ungrouped
        VFXSequenceStep g0 = timeStep(0.0f);
        g0.variantGroup = 1;                 // 1 }
        VFXSequenceStep g1 = timeStep(0.0f);
        g1.variantGroup = 1;                 // 2 } group 1
        VFXSequenceStep g2 = timeStep(0.0f);
        g2.variantGroup = 1;                 // 3 }
        data.steps.push_back(mid);
        data.steps.push_back(g0);
        data.steps.push_back(g1);
        data.steps.push_back(g2);

        bool sawPlay = false;
        bool sawSkip = false;
        int firstWinner = -1;
        bool winnerVaried = false;
        for (uint32_t seed = 1; seed <= 200; ++seed)
        {
            const std::vector<bool> plays = VFXComboTimeline::resolvePlays(data, seed);
            if (plays[0])
                sawPlay = true;
            else
                sawSkip = true;

            int winner = -1;
            for (int i = 1; i <= 3; ++i)
                if (plays[i])
                    winner = i;
            if (firstWinner < 0)
                firstWinner = winner;
            else if (winner != firstWinner)
                winnerVaried = true;
        }
        CHECK(sawPlay);      // the p=0.5 step plays for some seeds
        CHECK(sawSkip);      // and is skipped for others
        CHECK(winnerVaried); // the group winner isn't constant across seeds
    }

    // ============================================================
    // VK-1498 — whole-sequence looping. The loop lives in the runtime service
    // (restartComboForLoop) but its two primitives are the timeline's rewind() (stableLoop) and
    // deriveLoopSeed()+reset() (variety). These verify markers re-fire exactly once per iteration
    // and that the per-iteration seed stream is deterministic yet varied.
    // ============================================================

    TEST_CASE("VK-1498: deriveLoopSeed is stable, distinct per iteration, never zero, decorrelated")
    {
        CHECK(VFXComboTimeline::deriveLoopSeed(1234u, 1) == VFXComboTimeline::deriveLoopSeed(1234u, 1));
        CHECK(VFXComboTimeline::deriveLoopSeed(1234u, 1) != VFXComboTimeline::deriveLoopSeed(1234u, 2));
        CHECK(VFXComboTimeline::deriveLoopSeed(1u, 1) != VFXComboTimeline::deriveLoopSeed(2u, 1));
        for (uint32_t i = 0; i < 64; ++i)
            CHECK(VFXComboTimeline::deriveLoopSeed(0u, i) != 0u);
        // A loop-iteration seed must not coincide with a child seed at the same index.
        CHECK(VFXComboTimeline::deriveLoopSeed(1234u, 3) != VFXComboTimeline::deriveSeed(1234u, 3));
    }

    TEST_CASE("VK-1498: rewind loop re-fires the marker exactly once per iteration and repeats the schedule")
    {
        VFXSequenceData data;
        data.steps.push_back(timeStep(0.0f));    // 0 — time-driven
        data.steps.push_back(cueStep("boom"));   // 1 — cue-driven (fired by the marker)
        data.eventMarkers.push_back(VFXSequenceEventMarker{0.3f, "boom"});

        VFXComboTimeline tl;
        tl.reset(data, 7u);

        for (int iter = 0; iter < 3; ++iter)
        {
            std::vector<ComboEvent> ev;
            tl.advance(1.0f, ev); // cross both the time step and the marker
            const std::vector<int> spawns = spawnIndices(ev);
            CHECK(std::find(spawns.begin(), spawns.end(), 0) != spawns.end()); // time step spawned
            CHECK(std::find(spawns.begin(), spawns.end(), 1) != spawns.end()); // cue step via marker
            REQUIRE(tl.firedMarkers().size() == 1);
            CHECK(tl.firedMarkers()[0]); // marker fired exactly once this iteration
            CHECK(tl.allStepsSpawned());

            // Loop restart's stableLoop primitive: rewind() clears fired/spawned so the next
            // iteration re-fires from t=0 (mirrors VFXSequenceRuntimeServiceImpl::restartComboForLoop).
            tl.rewind();
            CHECK(tl.elapsed() == doctest::Approx(0.0f));
            CHECK_FALSE(tl.firedMarkers()[0]);
            CHECK_FALSE(tl.allStepsSpawned());
        }
    }

    TEST_CASE("VK-1498: re-seeding per iteration varies the variant winner; a stable seed keeps it fixed")
    {
        VFXSequenceData data;
        for (int k = 0; k < 3; ++k)
        {
            VFXSequenceStep s = timeStep(0.0f);
            s.variantGroup = 5;
            data.steps.push_back(s);
        }
        const uint32_t base = 20260712u;

        // Variety path (restartComboForLoop's !stableLoop branch): each iteration resolves with
        // deriveLoopSeed(base, iter). Exactly one member wins each time, and the winner varies.
        int firstWinner = -1;
        bool winnerVaried = false;
        for (uint32_t iter = 1; iter <= 200; ++iter)
        {
            const std::vector<bool> plays =
                VFXComboTimeline::resolvePlays(data, VFXComboTimeline::deriveLoopSeed(base, iter));
            int winner = -1;
            int count = 0;
            for (int i = 0; i < 3; ++i)
                if (plays[i])
                {
                    winner = i;
                    ++count;
                }
            CHECK(count == 1);
            if (firstWinner < 0)
                firstWinner = winner;
            else if (winner != firstWinner)
                winnerVaried = true;
        }
        CHECK(winnerVaried);

        // Stable path (rewind keeps the same seed): identical resolution every iteration.
        CHECK(VFXComboTimeline::resolvePlays(data, base) == VFXComboTimeline::resolvePlays(data, base));
    }
}
