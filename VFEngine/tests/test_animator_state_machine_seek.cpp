#include <doctest.h>

// VK-1433 — AnimatorStateMachine frame-step / absolute-seek primitives that back the prefab-rig
// preview scrub. These are the deterministic CPU-testable half of the feature:
//   * getCurrentClipFrameDuration() == 1 / ticksPerSecond of the current clip (one source frame).
//   * setNormalizedStateTime(t) clamps t to [0,1] and lands the playhead at exactly that fraction,
//     WITHOUT firing transitions/events (a clean scrub seek; Play/update() is untouched).
//   * a one-frame update(frameDt) advances normalized time by frameDt/duration (the forward step
//     the prefab-rig "Next Frame" button performs).
//
// Built with hand-built SkeletonData + AnimationData + a load callback (no GPU, no AssetDatabase),
// mirroring the proven harness in test_animator_clip_events.cpp.

#include <animation/AnimatorStateMachine.hpp>
#include <animator/AnimatorTypes.hpp>
#include <animator/AnimationEventTypes.hpp>
#include <asset/AssetRef.hpp>
#include <asset/AssetGUID.hpp>
#include <resource/Types.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector>

namespace
{
    // One-bone skeleton with non-empty bind / inverse-bind so the evaluator does not early-out.
    resource::SkeletonData makeSkeleton()
    {
        resource::SkeletonData s;
        s.bones.resize(1);
        s.bindPoses.resize(1);
        s.inverseBindPoses.resize(1);
        s.bones[0].name = "Root";
        s.bones[0].parentIndex = -1;
        s.bones[0].offsetMatrix = glm::mat4(1.0f);
        s.bones[0].preTransform = glm::mat4(1.0f);
        s.bindPoses[0] = glm::mat4(1.0f);
        s.inverseBindPoses[0] = glm::mat4(1.0f);
        s.globalInverseTransform = glm::mat4(1.0f);
        return s;
    }

    // A clip of `durationTicks / ticksPerSecond` seconds. With 24 ticks @ 24 tps the duration is
    // exactly 1.0s and one source frame is 1/24s.
    resource::AnimationData makeClip(float durationTicks, float ticksPerSecond,
                                     std::vector<animator::AnimationEvent> events = {})
    {
        resource::AnimationData clip;
        clip.name = "clip";
        clip.duration = durationTicks;
        clip.ticksPerSecond = ticksPerSecond;

        resource::BoneAnimation root;
        root.boneName = "Root";
        root.positionKeys = {{0.0f, glm::vec3(0.0f)}, {durationTicks, glm::vec3(0.0f)}};
        clip.channels.push_back(root);

        clip.events = std::move(events);
        return clip;
    }

    animator::AnimatorGraph makeSingleStateGraph(bool loop)
    {
        animator::AnimatorGraph graph;
        animator::AnimatorState state;
        state.id = 1;
        state.name = "S1";
        state.animationRef = asset::AssetRef::fromGUID(asset::AssetGUID::fromValue(0xA1ull));
        state.loop = loop;
        graph.states.push_back(std::move(state));
        graph.defaultStateId = 1;
        graph.nextStateId = 2;
        return graph;
    }
}

TEST_SUITE("AnimatorStateMachineSeek")
{
    // -----------------------------------------------------------------------
    // getCurrentClipFrameDuration() == 1 / ticksPerSecond of the current clip.
    // -----------------------------------------------------------------------
    TEST_CASE("getCurrentClipFrameDuration returns one source frame in seconds")
    {
        auto skeleton = makeSkeleton();
        auto graph = makeSingleStateGraph(/*loop*/ true);
        auto clip = makeClip(/*durationTicks*/ 24.0f, /*tps*/ 24.0f);

        animation::AnimatorStateMachine sm;
        sm.initializeFromGraph(graph, &skeleton,
            [&clip](const std::string&) -> const resource::AnimationData* { return &clip; });
        REQUIRE(sm.isInitialized());

        // 24 fps => 1/24 s per frame.
        CHECK(sm.getCurrentClipFrameDuration() == doctest::Approx(1.0f / 24.0f));
        // And the whole-clip duration is 1.0s.
        CHECK(sm.getCurrentStateDuration() == doctest::Approx(1.0f));
    }

    // -----------------------------------------------------------------------
    // setNormalizedStateTime lands at exactly the requested fraction.
    // -----------------------------------------------------------------------
    TEST_CASE("setNormalizedStateTime seeks to the requested normalized fraction")
    {
        auto skeleton = makeSkeleton();
        auto graph = makeSingleStateGraph(/*loop*/ true);
        auto clip = makeClip(24.0f, 24.0f);

        animation::AnimatorStateMachine sm;
        sm.initializeFromGraph(graph, &skeleton,
            [&clip](const std::string&) -> const resource::AnimationData* { return &clip; });

        sm.setNormalizedStateTime(0.5f);
        CHECK(sm.getNormalizedStateTime() == doctest::Approx(0.5f));

        sm.setNormalizedStateTime(0.25f);
        CHECK(sm.getNormalizedStateTime() == doctest::Approx(0.25f));
    }

    // -----------------------------------------------------------------------
    // setNormalizedStateTime CLAMPS to [0,1] (the ticket's clamp requirement).
    // -----------------------------------------------------------------------
    TEST_CASE("setNormalizedStateTime clamps out-of-range t to [0,1]")
    {
        auto skeleton = makeSkeleton();
        auto graph = makeSingleStateGraph(/*loop*/ true);
        auto clip = makeClip(24.0f, 24.0f);

        animation::AnimatorStateMachine sm;
        sm.initializeFromGraph(graph, &skeleton,
            [&clip](const std::string&) -> const resource::AnimationData* { return &clip; });

        // t > 1 clamps to the very end. getNormalizedStateTime() = fmod(stateTime/dur, 1):
        // at stateTime == duration this wraps to 0, so assert stateTime via the raw normalized
        // read at a hair under 1 instead — but the clamp itself is what we lock: feeding 5.0
        // must behave identically to feeding 1.0 (no overshoot, no NaN).
        sm.setNormalizedStateTime(5.0f);
        const float atHigh = sm.getNormalizedStateTime();

        sm.setNormalizedStateTime(1.0f);
        const float atOne = sm.getNormalizedStateTime();
        CHECK(atHigh == doctest::Approx(atOne)); // 5.0 was clamped to 1.0

        // t < 0 clamps to the start (normalized 0).
        sm.setNormalizedStateTime(-3.0f);
        CHECK(sm.getNormalizedStateTime() == doctest::Approx(0.0f));
    }

    // -----------------------------------------------------------------------
    // A seek must NOT fire transitions/events (it is a pose-only re-evaluate).
    // -----------------------------------------------------------------------
    TEST_CASE("setNormalizedStateTime does not fire clip events")
    {
        auto skeleton = makeSkeleton();
        auto graph = makeSingleStateGraph(/*loop*/ false);
        // An event at 0.5 that a normal play would fire when crossing it.
        auto clip = makeClip(24.0f, 24.0f, {{"mid", 0.5f, ""}});

        animation::AnimatorStateMachine sm;
        sm.initializeFromGraph(graph, &skeleton,
            [&clip](const std::string&) -> const resource::AnimationData* { return &clip; });

        // Jump straight past 0.5 — a seek fires nothing (no crossing test runs).
        sm.setNormalizedStateTime(0.9f);
        CHECK(sm.getFiredEvents().empty());
    }

    // -----------------------------------------------------------------------
    // A single forward frame (update(frameDt)) advances normalized time by
    // frameDt / duration — the deterministic forward step the "Next Frame" uses.
    // -----------------------------------------------------------------------
    TEST_CASE("one forward frame advances normalized time by frameDt/duration")
    {
        auto skeleton = makeSkeleton();
        auto graph = makeSingleStateGraph(/*loop*/ true);
        auto clip = makeClip(24.0f, 24.0f); // 1.0s, 1/24s per frame

        animation::AnimatorStateMachine sm;
        sm.initializeFromGraph(graph, &skeleton,
            [&clip](const std::string&) -> const resource::AnimationData* { return &clip; });

        const float frameDt = sm.getCurrentClipFrameDuration();
        const float dur = sm.getCurrentStateDuration();
        REQUIRE(dur > 0.0f);

        // Seek to a known start, then advance exactly one frame.
        sm.setNormalizedStateTime(0.25f);
        const float before = sm.getNormalizedStateTime();
        sm.update(frameDt);
        const float after = sm.getNormalizedStateTime();

        CHECK(after - before == doctest::Approx(frameDt / dur));
        // One frame of a 24-fps, 1.0s clip == 1/24 of normalized time.
        CHECK(after - before == doctest::Approx(1.0f / 24.0f));
    }

    // -----------------------------------------------------------------------
    // VK-1433 gap: the boundary seeks t==0 and t==1 land the RAW playhead exactly.
    // getNormalizedStateTime() = fmod(stateTime/dur, 1) reads back 0 at BOTH ends
    // (t=1 -> stateTime==dur -> fmod wraps to 0), which can mask a seek that never
    // reached the end. Read state.stateTime (and previousNormalizedTime) directly to
    // prove the playhead genuinely lands at 0 for t=0 and at `duration` for t=1 (and
    // for an overshoot t=5 clamped to 1). Bookkeeping previousNormalizedTime tracks
    // the clamped fraction so a later update() sees no spurious wrap.
    // -----------------------------------------------------------------------
    TEST_CASE("setNormalizedStateTime boundary seeks (0 and 1) land the raw playhead exactly")
    {
        auto skeleton = makeSkeleton();
        auto graph = makeSingleStateGraph(/*loop*/ true);
        auto clip = makeClip(24.0f, 24.0f); // duration = 1.0s

        animation::AnimatorStateMachine sm;
        sm.initializeFromGraph(graph, &skeleton,
            [&clip](const std::string&) -> const resource::AnimationData* { return &clip; });
        REQUIRE(sm.isInitialized());

        const float dur = sm.getCurrentStateDuration();
        REQUIRE(dur == doctest::Approx(1.0f));

        // t == 0: playhead at the very start.
        sm.setNormalizedStateTime(0.0f);
        CHECK(sm.getMachineState().stateTime == doctest::Approx(0.0f));
        CHECK(sm.getMachineState().previousNormalizedTime == doctest::Approx(0.0f));

        // t == 1: playhead lands at exactly `duration` (NOT wrapped back to 0).
        sm.setNormalizedStateTime(1.0f);
        CHECK(sm.getMachineState().stateTime == doctest::Approx(dur));
        CHECK(sm.getMachineState().previousNormalizedTime == doctest::Approx(1.0f));

        // Overshoot t == 5 clamps to 1 -> identical raw playhead as t == 1.
        sm.setNormalizedStateTime(5.0f);
        CHECK(sm.getMachineState().stateTime == doctest::Approx(dur));
        CHECK(sm.getMachineState().previousNormalizedTime == doctest::Approx(1.0f));

        // Undershoot t == -3 clamps to 0 -> raw playhead at the start.
        sm.setNormalizedStateTime(-3.0f);
        CHECK(sm.getMachineState().stateTime == doctest::Approx(0.0f));
        CHECK(sm.getMachineState().previousNormalizedTime == doctest::Approx(0.0f));
    }
}
