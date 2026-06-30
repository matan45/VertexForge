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
}
