#include <doctest.h>

#include "animation/AnimationEvaluator.hpp"
#include "animation/AnimationBlender.hpp"
#include "resource/Types.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <vector>

// ============================================================
// VK-1441: Skin/bone distortion during animation cross-fade blending.
//
// Root cause: the blend pipeline interpolated the FINAL skinning-palette
// matrices (globalInverse * world * inverseBindPose) per bone IN ISOLATION.
// Because each such matrix bakes in its whole parent chain, decomposing two of
// them, lerp/slerp-ing, and recomposing per bone independently severs the
// parent->child relationship, so during a large-delta blend a child joint leaves
// the arc its parent defines and the bone visibly stretches/shears. The fix
// blends per-bone LOCAL TRS (parent-relative) and runs ONE hierarchy +
// inverse-bind pass (AnimationBlender::blendLocalPoses / blendLocalNPoses +
// composeSkinningPalette).
//
// CRITICAL test-setup detail: the rotating joint must be OFFSET from the bone
// whose distance we measure AND have its own rotation differ between clips. An
// un-animated child shares its parent's skinning matrix, so a "rotate the root,
// identity offset child" test would falsely pass even on the buggy code. Here
// the root is fixed at the origin and the offset child ("Bone") rotates about
// its own joint — its origin must stay exactly L from the root under any correct
// pose, while the old per-bone palette blend pushes it off that circle.
// ============================================================

using animation::AnimationEvaluator;
using animation::AnimationBlender;
using animation::EvaluatedBone;

namespace
{
    // Root (origin, fixed) -> Bone (local offset (L,0,0), rotates about its own joint).
    // Identity globalInverse; bind/inverse-bind derived by forward kinematics so a bone's
    // skinning matrix times its bind pose recovers the bone's world origin.
    resource::SkeletonData makeTwoBoneSkeleton(float L)
    {
        resource::SkeletonData s;
        s.globalInverseTransform = glm::mat4(1.0f);

        resource::SkeletonBone root;
        root.name = "Root";
        root.parentIndex = -1;
        root.offsetMatrix = glm::mat4(1.0f);   // local bind (== computedLocalBindPose)
        root.preTransform = glm::mat4(1.0f);

        resource::SkeletonBone bone;
        bone.name = "Bone";
        bone.parentIndex = 0;
        bone.offsetMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(L, 0.0f, 0.0f));
        bone.preTransform = glm::mat4(1.0f);

        s.bones = {root, bone};
        s.bindPoses = {glm::mat4(1.0f), glm::translate(glm::mat4(1.0f), glm::vec3(L, 0.0f, 0.0f))};
        s.inverseBindPoses = {glm::mat4(1.0f), glm::translate(glm::mat4(1.0f), glm::vec3(-L, 0.0f, 0.0f))};
        return s;
    }

    // Animate only "Bone" with a single rotation key (no position/scale keys -> the channel-less
    // position falls back to the bone's local offset, exactly as the engine does).
    resource::AnimationData makeRotationClip(const char* name, const glm::quat& boneRot)
    {
        resource::AnimationData a;
        a.name = name;
        a.duration = 1.0f;
        a.ticksPerSecond = 1.0f;

        resource::BoneAnimation ch;
        ch.boneName = "Bone";
        ch.rotationKeys.push_back({0.0f, boneRot});
        a.channels.push_back(ch);
        return a;
    }

    resource::AnimationData makeEmptyClip(const char* name)
    {
        resource::AnimationData a;
        a.name = name;
        a.duration = 1.0f;
        a.ticksPerSecond = 1.0f;
        return a;
    }

    resource::SkeletonData makeScaledUnchanneledSkeleton()
    {
        resource::SkeletonData s;
        s.globalInverseTransform = glm::mat4(1.0f);

        resource::SkeletonBone root;
        root.name = "Root";
        root.parentIndex = -1;
        root.offsetMatrix = glm::mat4(1.0f);
        root.preTransform = glm::mat4(1.0f);

        resource::SkeletonBone child;
        child.name = "ScaledChild";
        child.parentIndex = 0;
        child.offsetMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f)) *
                             glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 0.5f, 1.5f));
        child.preTransform = glm::mat4(1.0f);

        s.bones = {root, child};
        s.bindPoses = {glm::mat4(1.0f), child.offsetMatrix};
        s.inverseBindPoses = {glm::mat4(1.0f), glm::inverse(child.offsetMatrix)};
        return s;
    }

    // Recover a bone's world origin from a skinning palette the way IK does
    // (IKPostProcess.cpp): pos = palette[i] * bindPose[i] * (0,0,0,1).
    glm::vec3 head(const std::vector<glm::mat4>& palette, const resource::SkeletonData& s, int i)
    {
        return glm::vec3(palette[i] * s.bindPoses[i] * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    }

    float boneLength(const std::vector<glm::mat4>& palette, const resource::SkeletonData& s)
    {
        return glm::length(head(palette, s, 1) - head(palette, s, 0));
    }

    glm::quat rotZ(float degrees)
    {
        return glm::angleAxis(glm::radians(degrees), glm::vec3(0.0f, 0.0f, 1.0f));
    }
}

TEST_SUITE("AnimationBlendLocal")
{

TEST_CASE("cross-fade preserves bone length at mid-weight (VK-1441)")
{
    const float L = 2.0f;
    auto skel = makeTwoBoneSkeleton(L);
    auto clipA = makeRotationClip("A", glm::quat(1.0f, 0.0f, 0.0f, 0.0f)); // identity
    auto clipB = makeRotationClip("B", rotZ(90.0f));

    AnimationEvaluator evA, evB;
    evA.loadAnimation(clipA, skel);
    evB.loadAnimation(clipB, skel);
    auto palA = evA.evaluatePose(0.0f);
    auto palB = evB.evaluatePose(0.0f);

    REQUIRE(palA.size() == 2);
    REQUIRE(palB.size() == 2);

    // Endpoints are correct in both representations: the bone is exactly L long.
    CHECK(boneLength(palA, skel) == doctest::Approx(L));
    CHECK(boneLength(palB, skel) == doctest::Approx(L));

    // Control (documents the bug): blending the final skinning palettes per bone stretches the
    // bone to L*sqrt(1.5) ~= 2.449 (a ~22.5% elongation) at weight 0.5.
    auto palMatrix = AnimationBlender::blendPoses(palA, palB, 0.5f);
    const float distMatrix = boneLength(palMatrix, skel);
    CHECK(distMatrix == doctest::Approx(L * std::sqrt(1.5f)));
    CHECK(distMatrix > 2.4f);

    // Fix: local-space blend keeps the bone rigid.
    auto palLocal = AnimationBlender::blendLocalPoses(
        evA.getEvaluatedBones(), evB.getEvaluatedBones(), 0.5f, skel);
    CHECK(boneLength(palLocal, skel) == doctest::Approx(L));
}

TEST_CASE("cross-fade bone length preserved across the whole sweep")
{
    const float L = 2.0f;
    auto skel = makeTwoBoneSkeleton(L);
    auto clipA = makeRotationClip("A", glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    auto clipB = makeRotationClip("B", rotZ(120.0f));

    AnimationEvaluator evA, evB;
    evA.loadAnimation(clipA, skel);
    evB.loadAnimation(clipB, skel);
    evA.evaluatePose(0.0f);
    evB.evaluatePose(0.0f);

    for (float w = 0.0f; w <= 1.0001f; w += 0.1f)
    {
        auto pal = AnimationBlender::blendLocalPoses(
            evA.getEvaluatedBones(), evB.getEvaluatedBones(), w, skel);
        CHECK(boneLength(pal, skel) == doctest::Approx(L));
    }
}

TEST_CASE("blend-tree N-way preserves bone length (VK-1441)")
{
    const float L = 2.0f;
    auto skel = makeTwoBoneSkeleton(L);
    auto clipA = makeRotationClip("A", glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    auto clipB = makeRotationClip("B", rotZ(90.0f));

    AnimationEvaluator evA, evB;
    evA.loadAnimation(clipA, skel);
    evB.loadAnimation(clipB, skel);
    auto palA = evA.evaluatePose(0.0f);
    auto palB = evB.evaluatePose(0.0f);

    std::vector<std::vector<EvaluatedBone>> sources = {evA.getEvaluatedBones(), evB.getEvaluatedBones()};
    std::vector<float> weights = {0.5f, 0.5f};

    // Control (documents the bug): matrix N-way blend distorts.
    auto palMatrix = AnimationBlender::blendNPoses({palA, palB}, weights);
    CHECK(boneLength(palMatrix, skel) > 2.4f);

    // Fix: local-space N-way blend preserves the bone length.
    auto palLocal = AnimationBlender::blendLocalNPoses(sources, weights, skel);
    CHECK(boneLength(palLocal, skel) == doctest::Approx(L));
}

TEST_CASE("composeSkinningPalette matches evaluatePose (equivalence guard)")
{
    const float L = 2.0f;
    auto skel = makeTwoBoneSkeleton(L);
    auto clip = makeRotationClip("C", rotZ(37.0f));

    AnimationEvaluator ev;
    ev.loadAnimation(clip, skel);
    auto pal = ev.evaluatePose(0.0f);

    std::vector<glm::mat4> locals;
    for (const auto& eb : ev.getEvaluatedBones())
        locals.push_back(eb.localTransform);
    auto composed = animation::composeSkinningPalette(locals, skel);

    REQUIRE(composed.size() == pal.size());
    for (size_t i = 0; i < pal.size(); ++i)
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                CHECK(composed[i][c][r] == doctest::Approx(pal[i][c][r]));
}

TEST_CASE("composeSkinningPalette clamps to available inverse bind poses")
{
    auto skel = makeTwoBoneSkeleton(2.0f);
    std::vector<glm::mat4> locals = {glm::mat4(1.0f), skel.bones[1].offsetMatrix};

    auto noInverse = skel;
    noInverse.inverseBindPoses.clear();
    CHECK(animation::composeSkinningPalette(locals, noInverse).empty());

    auto shortInverse = skel;
    shortInverse.inverseBindPoses.resize(1);
    auto bounded = animation::composeSkinningPalette(locals, shortInverse);
    REQUIRE(bounded.size() == 1);
}

TEST_CASE("local blend preserves scaled unchanneled bind bones")
{
    auto skel = makeScaledUnchanneledSkeleton();
    auto clip = makeEmptyClip("BindOnly");

    AnimationEvaluator evA, evB;
    evA.loadAnimation(clip, skel);
    evB.loadAnimation(clip, skel);
    auto expected = evA.evaluatePose(0.0f);
    evB.evaluatePose(0.0f);

    for (float w : {0.0f, 0.5f, 1.0f})
    {
        auto blended = AnimationBlender::blendLocalPoses(
            evA.getEvaluatedBones(), evB.getEvaluatedBones(), w, skel);
        REQUIRE(blended.size() == expected.size());
        for (size_t i = 0; i < expected.size(); ++i)
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    CHECK(blended[i][c][r] == doctest::Approx(expected[i][c][r]));
    }

    std::vector<std::vector<EvaluatedBone>> sources = {evA.getEvaluatedBones(), evB.getEvaluatedBones()};
    std::vector<float> weights = {0.5f, 0.5f};
    auto nWay = AnimationBlender::blendLocalNPoses(sources, weights, skel);
    REQUIRE(nWay.size() == expected.size());
    for (size_t i = 0; i < expected.size(); ++i)
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                CHECK(nWay[i][c][r] == doctest::Approx(expected[i][c][r]));
}

TEST_CASE("evaluateLocalPose composed palette matches evaluatePose")
{
    const float L = 2.0f;
    auto skel = makeTwoBoneSkeleton(L);
    auto clip = makeRotationClip("Local", rotZ(23.0f));

    AnimationEvaluator full;
    full.loadAnimation(clip, skel);
    auto expected = full.evaluatePose(0.0f);

    AnimationEvaluator local;
    local.loadAnimation(clip, skel);
    local.evaluateLocalPose(0.0f);

    std::vector<glm::mat4> locals;
    for (const auto& eb : local.getEvaluatedBones())
        locals.push_back(eb.localTransform);
    auto composed = animation::composeSkinningPalette(locals, skel);

    REQUIRE(composed.size() == expected.size());
    for (size_t i = 0; i < expected.size(); ++i)
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                CHECK(composed[i][c][r] == doctest::Approx(expected[i][c][r]));
}

TEST_CASE("self-blend equals the single pose (no drift on the non-blend path)")
{
    const float L = 2.0f;
    auto skel = makeTwoBoneSkeleton(L);
    auto clip = makeRotationClip("C", rotZ(55.0f));

    AnimationEvaluator ev;
    ev.loadAnimation(clip, skel);
    auto pal = ev.evaluatePose(0.0f);

    for (float w : {0.0f, 0.5f, 1.0f})
    {
        auto blended = AnimationBlender::blendLocalPoses(
            ev.getEvaluatedBones(), ev.getEvaluatedBones(), w, skel);
        REQUIRE(blended.size() == pal.size());
        for (size_t i = 0; i < pal.size(); ++i)
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    CHECK(blended[i][c][r] == doctest::Approx(pal[i][c][r]));
    }
}

} // TEST_SUITE
