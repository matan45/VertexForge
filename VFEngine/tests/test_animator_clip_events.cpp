#include <doctest.h>

#include <animation/AnimatorStateMachine.hpp>
#include <animator/AnimatorTypes.hpp>
#include <animator/AnimationEventTypes.hpp>
#include <animator/BlendTreeTypes.hpp>
#include <asset/AssetRef.hpp>
#include <asset/AssetGUID.hpp>
#include <resource/Types.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <string>
#include <vector>

// ============================================================
// VK-1425 (Part A): AnimatorStateMachine::fireTriggeredEvents() now fires BOTH
// the state-level events authored on the AnimatorState AND the timeline notify
// events baked into the loaded .vfAnim clip (resource::AnimationData::events).
// startTransition() also resets eventLastLoopCount so the first frame of a new
// state does not spuriously fire every clip event with normalizedTime > 0.
//
// These are CPU-only: the state machine is driven with hand-built SkeletonData +
// AnimationData and a load callback that returns the in-memory clip (no GPU, no
// AssetDatabase entry needed). Construction mirrors the proven harness in
// test_edit_mode_socket_preview.cpp (makeSkeleton) and the load-callback trick.
// ============================================================

namespace
{
    // Minimal one-bone skeleton with model-space bind + inverse bind poses, so
    // AnimationEvaluator::loadAnimation() (called by evaluateStatePose) does not
    // early-out — it requires non-empty bones AND inverseBindPoses. Empty channels
    // on the clip are tolerated (bones simply fall back to their bind transform).
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

    // A 1.0-second clip (duration is stored in ticks; getAnimationDuration() divides
    // by ticksPerSecond => 24/24 = 1.0s). One position channel on "Root" keeps the
    // evaluator happy; the clip's own timeline events live in `events`.
    resource::AnimationData makeClip(std::vector<animator::AnimationEvent> events)
    {
        resource::AnimationData clip;
        clip.name = "clip";
        clip.duration = 24.0f;
        clip.ticksPerSecond = 24.0f;

        resource::BoneAnimation root;
        root.boneName = "Root";
        root.positionKeys = {{0.0f, glm::vec3(0.0f)}, {24.0f, glm::vec3(0.0f)}};
        clip.channels.push_back(root);

        clip.events = std::move(events);
        return clip;
    }

    // Build a single-state graph. `stateEvents` are authored on the state itself;
    // the clip (returned by the load callback) carries the timeline events.
    animator::AnimatorGraph makeGraph(uint32_t stateId, bool loop,
                                      std::vector<animator::AnimationEvent> stateEvents)
    {
        animator::AnimatorGraph graph;
        animator::AnimatorState state;
        state.id = stateId;
        state.name = "S" + std::to_string(stateId);
        // Valid (but unregistered) GUID => animationRef.isValid() true. resolve()
        // returns "" against an empty AssetDatabase; the load callback ignores the
        // path and always returns our in-memory clip, so no DB entry is required.
        state.animationRef = asset::AssetRef::fromGUID(asset::AssetGUID::fromValue(0xA1ull + stateId));
        state.loop = loop;
        state.events = std::move(stateEvents);
        graph.states.push_back(std::move(state));
        graph.defaultStateId = stateId;
        graph.nextStateId = stateId + 1;
        return graph;
    }

    std::vector<std::string> firedNames(const animation::AnimatorStateMachine& sm)
    {
        std::vector<std::string> out;
        for (const auto* e : sm.getFiredEvents())
            out.push_back(e->name);
        return out;
    }

    int countName(const std::vector<std::string>& v, const std::string& n)
    {
        return static_cast<int>(std::count(v.begin(), v.end(), n));
    }
}

TEST_SUITE("AnimatorClipEvents")
{
    // -------- Merge + order: state event + two clip events, one play --------
    TEST_CASE("state and clip events both fire, once each, in time order")
    {
        auto skeleton = makeSkeleton();
        // State event at 0.12; clip events at 0.25 and 0.80.
        auto graph = makeGraph(1, /*loop*/ false, {{"stateEvt", 0.12f, ""}});
        auto clip = makeClip({{"clip25", 0.25f, ""}, {"clip80", 0.80f, ""}});

        animation::AnimatorStateMachine sm;
        sm.initializeFromGraph(graph, &skeleton,
            [&clip](const std::string&) -> const resource::AnimationData* { return &clip; });
        REQUIRE(sm.isInitialized());

        std::vector<std::string> order;
        // Step to t=0.9s in 0.1s frames (9 frames) — covers (0, 0.9], stays under
        // the 1.0s duration so no loop-wrap muddies this single-play assertion.
        for (int i = 0; i < 9; ++i)
        {
            sm.update(0.1f);
            for (const auto& n : firedNames(sm))
                order.push_back(n);
        }

        CHECK(countName(order, "stateEvt") == 1);
        CHECK(countName(order, "clip25") == 1);
        CHECK(countName(order, "clip80") == 1);
        REQUIRE(order.size() == 3);
        CHECK(order[0] == "stateEvt");
        CHECK(order[1] == "clip25");
        CHECK(order[2] == "clip80");
    }

    // -------- Clip events fire even with NO state events (regression) --------
    // This is exactly what removing the old `events.empty()` early-return enables.
    TEST_CASE("clip events fire when the state has no state-level events")
    {
        auto skeleton = makeSkeleton();
        auto graph = makeGraph(1, /*loop*/ false, /*no state events*/ {});
        auto clip = makeClip({{"clipA", 0.30f, ""}, {"clipB", 0.70f, ""}});

        animation::AnimatorStateMachine sm;
        sm.initializeFromGraph(graph, &skeleton,
            [&clip](const std::string&) -> const resource::AnimationData* { return &clip; });

        std::vector<std::string> order;
        for (int i = 0; i < 9; ++i)
        {
            sm.update(0.1f);
            for (const auto& n : firedNames(sm))
                order.push_back(n);
        }

        CHECK(countName(order, "clipA") == 1);
        CHECK(countName(order, "clipB") == 1);
        REQUIRE(order.size() == 2);
        CHECK(order[0] == "clipA");
        CHECK(order[1] == "clipB");
    }

    // -------- Loop wrap: each event fires again exactly once per loop --------
    TEST_CASE("looping state re-fires each event once per wrap with no boundary double-fire")
    {
        auto skeleton = makeSkeleton();
        auto graph = makeGraph(1, /*loop*/ true, {{"mid", 0.50f, ""}});
        auto clip = makeClip({{"early", 0.10f, ""}, {"late", 0.90f, ""}});

        animation::AnimatorStateMachine sm;
        sm.initializeFromGraph(graph, &skeleton,
            [&clip](const std::string&) -> const resource::AnimationData* { return &clip; });

        // Drive 2.0s in 0.1s frames => two full loops of the 1.0s clip. The frame
        // that straddles the 1.0->0.0 wrap must fire events on both sides correctly
        // and must NOT double-fire any event at the boundary.
        std::vector<std::string> all;
        for (int i = 0; i < 20; ++i)
        {
            sm.update(0.1f);
            for (const auto& n : firedNames(sm))
                all.push_back(n);
        }

        // Each event fires once per loop => twice across two loops.
        CHECK(countName(all, "early") == 2);
        CHECK(countName(all, "mid") == 2);
        CHECK(countName(all, "late") == 2);
    }

    // -------- straddling frame fires both sides of the wrap --------
    TEST_CASE("a single frame straddling the wrap fires the pre- and post-wrap events")
    {
        auto skeleton = makeSkeleton();
        auto graph = makeGraph(1, /*loop*/ true, {});
        // "late" just before the wrap, "early" just after.
        auto clip = makeClip({{"late", 0.95f, ""}, {"early", 0.05f, ""}});

        animation::AnimatorStateMachine sm;
        sm.initializeFromGraph(graph, &skeleton,
            [&clip](const std::string&) -> const resource::AnimationData* { return &clip; });

        // March to normalized 0.90 (prev) without firing either event.
        for (int i = 0; i < 9; ++i)
            sm.update(0.1f); // reaches stateTime 0.9

        // One big step 0.9 -> 1.1 wraps to 0.1: crosses 0.95 (pre-wrap) and 0.05
        // (post-wrap) in the same frame. Both must fire exactly once.
        sm.update(0.2f);
        auto names = firedNames(sm);
        CHECK(countName(names, "late") == 1);
        CHECK(countName(names, "early") == 1);
    }

    // -------- Boundary events at 0.0 and 1.0 fire once --------
    TEST_CASE("events at normalizedTime 0.0 and 1.0 each fire once over a play")
    {
        auto skeleton = makeSkeleton();
        auto graph = makeGraph(1, /*loop*/ false, {});
        auto clip = makeClip({{"zero", 0.0f, ""}, {"one", 1.0f, ""}});

        animation::AnimatorStateMachine sm;
        sm.initializeFromGraph(graph, &skeleton,
            [&clip](const std::string&) -> const resource::AnimationData* { return &clip; });

        // The crossing test is (prev, curr]. The 0.0 event only fires when a loop
        // wrap makes the looped-branch (t <= curr) include it, OR is excluded for a
        // non-looping play (0.0 is never > prev for the very first frame where
        // prev==0). The 1.0 event fires when curr reaches exactly 1.0 before wrap.
        // Drive a SINGLE clean play to t=1.0 in one 1.0s step so curr==fmod(1.0)=0
        // with prev==0 — verify behaviour is deterministic and finite-firing.
        std::vector<std::string> all;
        for (int i = 0; i < 10; ++i) // 0.1s * 10 = 1.0s
        {
            sm.update(0.1f);
            for (const auto& n : firedNames(sm))
                all.push_back(n);
        }

        // "one" (normalizedTime 1.0) is crossed when curr reaches 1.0. On a
        // non-looping clip the playhead lands on stateTime 1.0 at frame 10 where
        // curr=fmod(1.0,1.0)=0 and prev=0.9 => looped branch (t>0.9 || t<=0.0)
        // fires "one" (1.0 > 0.9). It must fire exactly once.
        CHECK(countName(all, "one") == 1);
        // "zero" fires once via that same looped branch (0.0 <= 0.0).
        CHECK(countName(all, "zero") == 1);
    }

    // -------- Transition no-spam: the eventLastLoopCount reset fix --------
    TEST_CASE("transition out of a looped state does not spam clip events on the new state's first frame")
    {
        auto skeleton = makeSkeleton();

        // Two states. State 1 loops (so currentLoopCount/eventLastLoopCount grow).
        // State 2's clip has several events at normalizedTime > 0. Before the fix,
        // the first frame of state 2 saw currentLoopCount(0) != eventLastLoopCount(>0)
        // and treated it as a wrap, firing EVERY event with t > 0.
        animator::AnimatorGraph graph;
        {
            animator::AnimatorState s1;
            s1.id = 1;
            s1.name = "Loop";
            s1.animationRef = asset::AssetRef::fromGUID(asset::AssetGUID::fromValue(0x111ull));
            s1.loop = true;
            graph.states.push_back(s1);

            animator::AnimatorState s2;
            s2.id = 2;
            s2.name = "Target";
            s2.animationRef = asset::AssetRef::fromGUID(asset::AssetGUID::fromValue(0x222ull));
            s2.loop = false;
            graph.states.push_back(s2);

            graph.defaultStateId = 1;
            graph.nextStateId = 3;
        }

        // Both states resolve to the same clip object with events spread across the
        // timeline (the load callback ignores the path).
        auto clip = makeClip({{"e25", 0.25f, ""}, {"e50", 0.50f, ""}, {"e75", 0.75f, ""}});

        animation::AnimatorStateMachine sm;
        sm.initializeFromGraph(graph, &skeleton,
            [&clip](const std::string&) -> const resource::AnimationData* { return &clip; });
        REQUIRE(sm.getCurrentAnimatorState() != nullptr);
        REQUIRE(sm.getCurrentAnimatorState()->id == 1);

        // Loop state 1 for >1 full cycle so currentLoopCount > 0.
        for (int i = 0; i < 13; ++i) // 1.3s => one wrap, loop count 1
            sm.update(0.1f);
        REQUIRE(sm.getMachineState().currentLoopCount >= 1);

        // Force an immediate (non-blended) transition to state 2. stateTime resets
        // to 0 and eventLastLoopCount must reset to 0 too.
        sm.forceTransitionTo(2u, /*blendDuration*/ 0.0f);
        REQUIRE(sm.getCurrentAnimatorState()->id == 2);
        CHECK(sm.getMachineState().currentLoopCount == 0);

        // First frame of state 2: a tiny step to normalized ~0.05. Only events in
        // (0, 0.05] may fire — i.e. NONE of e25/e50/e75. Pre-fix this fired all three.
        sm.update(0.05f);
        auto names = firedNames(sm);
        CHECK(countName(names, "e25") == 0);
        CHECK(countName(names, "e50") == 0);
        CHECK(countName(names, "e75") == 0);
        CHECK(names.empty());

        // Continue the play: each event still fires exactly once as the playhead
        // legitimately crosses it.
        std::vector<std::string> rest;
        for (int i = 0; i < 9; ++i) // ~0.05 -> ~0.95
        {
            sm.update(0.1f);
            for (const auto& n : firedNames(sm))
                rest.push_back(n);
        }
        CHECK(countName(rest, "e25") == 1);
        CHECK(countName(rest, "e50") == 1);
        CHECK(countName(rest, "e75") == 1);
    }

    // -------- Blend-tree state fires STATE events but not clip events --------
    TEST_CASE("blend-tree state fires its state-level events (clip events scoped out)")
    {
        auto skeleton = makeSkeleton();

        animator::AnimatorGraph graph;
        animator::AnimatorState state;
        state.id = 1;
        state.name = "BlendState";
        state.loop = false;
        state.events = {{"btState", 0.40f, ""}};

        // A blend tree with one entry. getAnimationDuration() resolves the duration
        // from the first valid entry's clip via the load callback. fireTriggeredEvents
        // takes the blendTree branch and does NOT pull clip-timeline events.
        animator::BlendTreeData tree;
        tree.type = animator::BlendTreeType::BlendTree1D;
        animator::BlendTreeEntry entry;
        entry.animationRef = asset::AssetRef::fromGUID(asset::AssetGUID::fromValue(0x333ull));
        entry.threshold = 0.0f;
        tree.entries.push_back(entry);
        state.blendTree = tree;

        graph.states.push_back(state);
        graph.defaultStateId = 1;
        graph.nextStateId = 2;

        // The clip carries timeline events that must NOT fire on a blend-tree state.
        auto clip = makeClip({{"btClip", 0.40f, ""}});

        animation::AnimatorStateMachine sm;
        sm.initializeFromGraph(graph, &skeleton,
            [&clip](const std::string&) -> const resource::AnimationData* { return &clip; });
        REQUIRE(sm.isInitialized());

        std::vector<std::string> all;
        for (int i = 0; i < 9; ++i)
        {
            sm.update(0.1f);
            for (const auto& n : firedNames(sm))
                all.push_back(n);
        }

        // State-level event still fires on a blend-tree state.
        CHECK(countName(all, "btState") == 1);
        // Clip-timeline event must be scoped out for blend-tree states.
        CHECK(countName(all, "btClip") == 0);
    }
}
