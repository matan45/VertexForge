#include <doctest.h>

#include <animation/AnimationLayerStack.hpp>
#include <animator/AnimatorTypes.hpp>
#include <animator/SocketTypes.hpp>
#include <asset/AssetRef.hpp>
#include <asset/AssetGUID.hpp>
#include <resource/Types.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <string>
#include <vector>

// ============================================================
// VK-1407: edit-mode socket-attachment preview.
//
// Covers the CPU-testable contract of AnimationLayerStack::evaluateRestPose()
// and computeSocketTransforms() that the edit-mode preview relies on:
//   * evaluateRestPose() composes a stable frame-0 rest pose WITHOUT update().
//   * It is idempotent (repeat calls yield identical matrices).
//   * computeSocketTransforms() early-outs (clears output) on a never-evaluated
//     stack, and yields finite, sized transforms after a rest-pose eval.
//   * A non-identity default clip moves a child socket vs. the bind pose, proving
//     the socket reflects the *evaluated* rest pose (not raw bind pose).
//
// NOT covered here (no clean CPU harness — documented at bottom):
//   RuntimeAnimatorSystem::updateEditModePreview() dirty-gating, which depends on
//   the EnTT registry + EventDispatcher singletons and file-path skeleton loading.
// ============================================================

namespace
{
    // Local bone spec (parent-before-child), mirrors the proven helper in
    // test_retargeting.cpp:19 but kept in this TU's anonymous namespace.
    struct BoneDef
    {
        std::string name;
        int parent;
        glm::vec3 t{0.0f};
        glm::quat r{1.0f, 0.0f, 0.0f, 0.0f};
    };

    // Build a skeleton with local bind transforms (offsetMatrix), deriving the
    // model-space bind poses + inverse bind poses by forward kinematics. Same
    // construction as test_retargeting.cpp:30 (makeSkeleton).
    resource::SkeletonData makeSkeleton(const std::vector<BoneDef>& defs)
    {
        resource::SkeletonData s;
        s.bones.resize(defs.size());
        s.bindPoses.resize(defs.size());
        s.inverseBindPoses.resize(defs.size());
        for (size_t i = 0; i < defs.size(); ++i)
        {
            auto& b = s.bones[i];
            b.name = defs[i].name;
            b.parentIndex = defs[i].parent;
            b.offsetMatrix = glm::translate(glm::mat4(1.0f), defs[i].t) * glm::mat4_cast(defs[i].r);
            b.preTransform = glm::mat4(1.0f);

            const glm::mat4 parentModel = defs[i].parent >= 0
                                              ? s.bindPoses[static_cast<size_t>(defs[i].parent)]
                                              : glm::mat4(1.0f);
            s.bindPoses[i] = parentModel * b.offsetMatrix;
            s.inverseBindPoses[i] = glm::inverse(s.bindPoses[i]);
        }
        s.globalInverseTransform = glm::mat4(1.0f);
        return s;
    }

    // Two-bone vertical-ish chain: Root at origin, Child offset +1 on X.
    std::vector<BoneDef> twoBoneDefs()
    {
        return {
            {"Root", -1, {0.0f, 0.0f, 0.0f}, glm::quat(1, 0, 0, 0)},
            {"Child", 0, {1.0f, 0.0f, 0.0f}, glm::quat(1, 0, 0, 0)},
        };
    }

    // A single-frame clip. If rootRot is non-identity it injects one rotation key
    // on "Root" at t=0, so the rest pose (frame 0) is a rotated pose rather than
    // the raw bind pose. Bones with no channel fall back to their bind transform.
    resource::AnimationData makeClip(const glm::quat& rootRot)
    {
        resource::AnimationData clip;
        clip.name = "rest_clip";
        clip.duration = 1.0f;
        clip.ticksPerSecond = 1.0f;

        resource::BoneAnimation root;
        root.boneName = "Root";
        root.positionKeys = {{0.0f, glm::vec3(0.0f)}};
        root.rotationKeys = {{0.0f, rootRot}};
        clip.channels.push_back(root);

        return clip;
    }

    // Build an AnimatorData with a single graph (no explicit layers => the
    // initializeSingleGraphLayer() path), one default state referencing a clip
    // via a valid (but unregistered) GUID. The load callback below ignores the
    // resolved path and always returns the test clip, so no AssetDatabase entry
    // is required.
    animator::AnimatorData makeAnimatorData()
    {
        animator::AnimatorData data;
        data.name = "rest_animator";

        animator::AnimatorState state;
        state.id = 1;
        state.name = "Idle";
        // A valid GUID makes animationRef.isValid() true; resolve() returns "" for
        // an unregistered GUID against an empty AssetDatabase (no crash, no FS walk).
        state.animationRef = asset::AssetRef::fromGUID(asset::AssetGUID::fromValue(0xABCDEF01ull));
        state.loop = true;

        data.graph.states.push_back(state);
        data.graph.defaultStateId = 1;
        data.graph.nextStateId = 2;

        return data;
    }
}

TEST_SUITE("EditModeSocketPreview")
{
    TEST_CASE("evaluateRestPose populates bone matrices without update()")
    {
        auto skeleton = makeSkeleton(twoBoneDefs());
        auto data = makeAnimatorData();
        const auto clip = makeClip(glm::quat(1, 0, 0, 0)); // identity rest

        animation::AnimationLayerStack stack;
        stack.initialize(data, &skeleton,
                         [&clip](const std::string&) -> const resource::AnimationData* { return &clip; });

        REQUIRE(stack.isInitialized());

        // No update() / time advance — only the rest-pose evaluation.
        stack.evaluateRestPose();

        const auto& bones = stack.getBoneMatrices();
        REQUIRE(bones.size() == skeleton.bones.size());

        // Every matrix must be finite.
        for (const auto& m : bones)
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    CHECK(std::isfinite(m[c][r]));
    }

    TEST_CASE("evaluateRestPose is idempotent")
    {
        auto skeleton = makeSkeleton(twoBoneDefs());
        auto data = makeAnimatorData();
        const auto clip = makeClip(glm::quat(1, 0, 0, 0));

        animation::AnimationLayerStack stack;
        stack.initialize(data, &skeleton,
                         [&clip](const std::string&) -> const resource::AnimationData* { return &clip; });

        stack.evaluateRestPose();
        const std::vector<glm::mat4> first = stack.getBoneMatrices(); // copy
        REQUIRE(first.size() == skeleton.bones.size());

        stack.evaluateRestPose();
        const auto& second = stack.getBoneMatrices();
        REQUIRE(second.size() == first.size());

        // Element-wise identical: rest pose does not drift across re-evaluation.
        for (size_t b = 0; b < first.size(); ++b)
        {
            INFO("bone " << skeleton.bones[b].name);
            const float* a = &first[b][0][0];
            const float* c = &second[b][0][0];
            for (int e = 0; e < 16; ++e)
                CHECK(c[e] == doctest::Approx(a[e]));
        }
    }

    TEST_CASE("computeSocketTransforms early-outs on a never-evaluated stack")
    {
        auto skeleton = makeSkeleton(twoBoneDefs());
        auto data = makeAnimatorData();
        const auto clip = makeClip(glm::quat(1, 0, 0, 0));

        animation::AnimationLayerStack stack;
        stack.initialize(data, &skeleton,
                         [&clip](const std::string&) -> const resource::AnimationData* { return &clip; });

        // No evaluateRestPose()/update() => finalBoneMatrices is empty, so the
        // early-out at AnimationLayerStackControls.cpp:200 clears the output.
        std::vector<animator::SocketDefinition> sockets(1);
        sockets[0].name = "Muzzle";
        sockets[0].boneIndex = 1;

        // Pre-fill the output to prove it gets cleared (not left untouched).
        std::vector<glm::mat4> out(3, glm::mat4(1.0f));
        stack.computeSocketTransforms(sockets, out);
        CHECK(out.empty());
    }

    TEST_CASE("computeSocketTransforms yields finite, sized transforms after rest eval")
    {
        auto skeleton = makeSkeleton(twoBoneDefs());
        auto data = makeAnimatorData();
        const auto clip = makeClip(glm::quat(1, 0, 0, 0));

        animation::AnimationLayerStack stack;
        stack.initialize(data, &skeleton,
                         [&clip](const std::string&) -> const resource::AnimationData* { return &clip; });

        stack.evaluateRestPose();

        std::vector<animator::SocketDefinition> sockets(2);
        sockets[0].name = "RootSocket";
        sockets[0].boneIndex = 0;
        sockets[1].name = "ChildSocket";
        sockets[1].boneIndex = 1;
        sockets[1].localPosition = glm::vec3(0.5f, 0.0f, 0.0f);

        std::vector<glm::mat4> out;
        stack.computeSocketTransforms(sockets, out);

        REQUIRE(out.size() == sockets.size());
        for (const auto& m : out)
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    CHECK(std::isfinite(m[c][r]));
    }

    TEST_CASE("socket reflects evaluated rest pose, not raw bind pose")
    {
        // identity-rest stack: Root not rotated => child socket sits at the
        // bind-pose child origin (1,0,0).
        auto skeletonA = makeSkeleton(twoBoneDefs());
        auto dataA = makeAnimatorData();
        const auto clipIdentity = makeClip(glm::quat(1, 0, 0, 0));

        animation::AnimationLayerStack restA;
        restA.initialize(dataA, &skeletonA,
                         [&clipIdentity](const std::string&) -> const resource::AnimationData* { return &clipIdentity; });
        restA.evaluateRestPose();

        // rotated-rest stack: Root rotated 90deg about +Z at frame 0. The child
        // bone origin (bind = +X) swings toward +Y, so its socket position differs.
        auto skeletonB = makeSkeleton(twoBoneDefs());
        auto dataB = makeAnimatorData();
        const glm::quat yaw90 = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        const auto clipRotated = makeClip(yaw90);

        animation::AnimationLayerStack restB;
        restB.initialize(dataB, &skeletonB,
                         [&clipRotated](const std::string&) -> const resource::AnimationData* { return &clipRotated; });
        restB.evaluateRestPose();

        std::vector<animator::SocketDefinition> sockets(1);
        sockets[0].name = "ChildSocket";
        sockets[0].boneIndex = 1; // attached to the Child bone (bind origin at +X)

        std::vector<glm::mat4> outA, outB;
        restA.computeSocketTransforms(sockets, outA);
        restB.computeSocketTransforms(sockets, outB);

        REQUIRE(outA.size() == 1);
        REQUIRE(outB.size() == 1);

        const glm::vec3 posA(outA[0][3]);
        const glm::vec3 posB(outB[0][3]);

        // Identity rest: child socket at the bind-pose child origin (1,0,0).
        CHECK(posA.x == doctest::Approx(1.0f));
        CHECK(std::fabs(posA.y) < 1e-4f);
        CHECK(std::fabs(posA.z) < 1e-4f);

        // Rotated rest: +Z 90deg rotation maps the child origin (+X) toward +Y.
        CHECK(std::fabs(posB.x) < 1e-4f);
        CHECK(posB.y == doctest::Approx(1.0f));
        CHECK(std::fabs(posB.z) < 1e-4f);

        // The two rest poses produce materially different socket positions,
        // proving the socket follows the evaluated rest pose, not the bind pose.
        CHECK(glm::length(posB - posA) > 0.5f);
    }
}

// ------------------------------------------------------------
// Manual / integration check (NOT unit-tested here):
//
// RuntimeAnimatorSystem::updateEditModePreview() dirty-gating
// (graphics/animation/RuntimeAnimatorSystem.{hpp,cpp}) cannot be exercised on the
// CPU test path: RuntimeAnimatorSystem is a process singleton whose private
// editPreviewDirty flag has no getter, and initialize()/syncWithRegistry()/
// updateEditModePreview() depend on the global EnTT registry, the EventDispatcher
// singleton, and file-path skeleton loading. There is no existing harness that
// drives this singleton from a test. Verify manually in the Editor:
//   1. Place a skeletal mesh with sockets in edit mode (no Play) -> mesh renders
//      in rest pose and socket attachments resolve to bone positions.
//   2. Edit socket data / reload the sector / toggle editor mode -> the preview
//      re-arms (editPreviewDirty=true) and updates once, then stays idle.
// ------------------------------------------------------------
