#include <doctest.h>

#include <retargeting/HumanoidBoneRole.hpp>
#include <retargeting/HumanoidBoneAutoMap.hpp>
#include <retargeting/RetargetAsset.hpp>
#include <resource/Types.hpp>
#include <animation/AnimationEvaluator.hpp>
#include <animation/RetargetContext.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

using retargeting::HumanoidBoneRole;

namespace
{
    struct BoneDef
    {
        std::string name;
        int parent;
        glm::vec3 t{0.0f};
        glm::quat r{1.0f, 0.0f, 0.0f, 0.0f};
    };

    // Build a skeleton with local bind transforms (offsetMatrix), deriving the
    // model-space bind poses + inverse bind poses by forward kinematics. Bones must
    // be listed parent-before-child.
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

    // A full-body humanoid skeleton where every bone maps to a required role.
    std::vector<BoneDef> humanoidBoneDefs()
    {
        const glm::quat tilt = glm::angleAxis(glm::radians(20.0f), glm::vec3(0, 0, 1));
        return {
            {"Hips", -1, {0.0f, 1.0f, 0.0f}, glm::quat(1, 0, 0, 0)},
            {"Spine", 0, {0.0f, 0.2f, 0.0f}, tilt},
            {"Head", 1, {0.0f, 0.5f, 0.0f}, glm::quat(1, 0, 0, 0)},
            {"LeftUpperArm", 1, {0.2f, 0.3f, 0.0f}, tilt},
            {"LeftLowerArm", 3, {0.3f, 0.0f, 0.0f}, glm::quat(1, 0, 0, 0)},
            {"LeftHand", 4, {0.25f, 0.0f, 0.0f}, glm::quat(1, 0, 0, 0)},
            {"RightUpperArm", 1, {-0.2f, 0.3f, 0.0f}, tilt},
            {"RightLowerArm", 6, {-0.3f, 0.0f, 0.0f}, glm::quat(1, 0, 0, 0)},
            {"RightHand", 7, {-0.25f, 0.0f, 0.0f}, glm::quat(1, 0, 0, 0)},
            {"LeftUpperLeg", 0, {0.1f, -0.1f, 0.0f}, glm::quat(1, 0, 0, 0)},
            {"LeftLowerLeg", 9, {0.0f, -0.4f, 0.0f}, glm::quat(1, 0, 0, 0)},
            {"LeftFoot", 10, {0.0f, -0.4f, 0.0f}, glm::quat(1, 0, 0, 0)},
            {"RightUpperLeg", 0, {-0.1f, -0.1f, 0.0f}, glm::quat(1, 0, 0, 0)},
            {"RightLowerLeg", 12, {0.0f, -0.4f, 0.0f}, glm::quat(1, 0, 0, 0)},
            {"RightFoot", 13, {0.0f, -0.4f, 0.0f}, glm::quat(1, 0, 0, 0)},
        };
    }

    // A clip that translates + rotates the hips and rotates a few other bones,
    // but never translates a non-hips bone (standard humanoid retarget assumption).
    resource::AnimationData makeClip()
    {
        resource::AnimationData clip;
        clip.name = "test";
        clip.duration = 30.0f;
        clip.ticksPerSecond = 30.0f;

        const glm::quat rotA = glm::angleAxis(glm::radians(10.0f), glm::vec3(1, 0, 0));
        const glm::quat rotB = glm::angleAxis(glm::radians(40.0f), glm::vec3(0, 1, 0));

        resource::BoneAnimation hips;
        hips.boneName = "Hips";
        hips.positionKeys = {{0.0f, {0.0f, 1.0f, 0.0f}}, {30.0f, {0.3f, 1.4f, 0.1f}}};
        hips.rotationKeys = {{0.0f, rotA}, {30.0f, rotB}};
        clip.channels.push_back(hips);

        for (const char* bn : {"Spine", "LeftUpperArm", "RightUpperArm", "LeftLowerLeg"})
        {
            resource::BoneAnimation ch;
            ch.boneName = bn;
            ch.rotationKeys = {{0.0f, rotA}, {30.0f, rotB}};
            clip.channels.push_back(ch);
        }
        return clip;
    }

    void requireRole(const std::vector<retargeting::HumanoidBoneBinding>& bindings, HumanoidBoneRole role)
    {
        bool found = false;
        for (const auto& b : bindings)
            if (b.role == role) { found = true; break; }
        INFO("missing role: " << retargeting::humanoidBoneRoleName(role));
        CHECK(found);
    }
}

TEST_CASE("autoMap resolves all required humanoid roles for Mixamo + UE naming")
{
    const std::vector<HumanoidBoneRole> required = {
        HumanoidBoneRole::Hips, HumanoidBoneRole::Spine, HumanoidBoneRole::Head,
        HumanoidBoneRole::LeftUpperArm, HumanoidBoneRole::LeftLowerArm, HumanoidBoneRole::LeftHand,
        HumanoidBoneRole::RightUpperArm, HumanoidBoneRole::RightLowerArm, HumanoidBoneRole::RightHand,
        HumanoidBoneRole::LeftUpperLeg, HumanoidBoneRole::LeftLowerLeg, HumanoidBoneRole::LeftFoot,
        HumanoidBoneRole::RightUpperLeg, HumanoidBoneRole::RightLowerLeg, HumanoidBoneRole::RightFoot,
    };

    SUBCASE("Mixamo")
    {
        auto skel = makeSkeleton({
            {"mixamorig:Hips", -1}, {"mixamorig:Spine", 0}, {"mixamorig:Spine1", 1},
            {"mixamorig:Spine2", 2}, {"mixamorig:Neck", 3}, {"mixamorig:Head", 4},
            {"mixamorig:LeftShoulder", 3}, {"mixamorig:LeftArm", 6}, {"mixamorig:LeftForeArm", 7}, {"mixamorig:LeftHand", 8},
            {"mixamorig:RightShoulder", 3}, {"mixamorig:RightArm", 10}, {"mixamorig:RightForeArm", 11}, {"mixamorig:RightHand", 12},
            {"mixamorig:LeftUpLeg", 0}, {"mixamorig:LeftLeg", 14}, {"mixamorig:LeftFoot", 15}, {"mixamorig:LeftToeBase", 16},
            {"mixamorig:RightUpLeg", 0}, {"mixamorig:RightLeg", 18}, {"mixamorig:RightFoot", 19}, {"mixamorig:RightToeBase", 20},
        });
        auto bindings = retargeting::autoMapHumanoidBones(skel);
        for (auto role : required) requireRole(bindings, role);
    }

    SUBCASE("Unreal mannequin")
    {
        auto skel = makeSkeleton({
            {"pelvis", -1}, {"spine_01", 0}, {"spine_02", 1}, {"spine_03", 2},
            {"neck_01", 3}, {"head", 4},
            {"clavicle_l", 3}, {"upperarm_l", 6}, {"lowerarm_l", 7}, {"hand_l", 8},
            {"clavicle_r", 3}, {"upperarm_r", 10}, {"lowerarm_r", 11}, {"hand_r", 12},
            {"thigh_l", 0}, {"calf_l", 14}, {"foot_l", 15}, {"ball_l", 16},
            {"thigh_r", 0}, {"calf_r", 18}, {"foot_r", 19}, {"ball_r", 20},
        });
        auto bindings = retargeting::autoMapHumanoidBones(skel);
        for (auto role : required) requireRole(bindings, role);
    }
}

TEST_CASE("identity retarget (source == target skeleton) matches native evaluation")
{
    auto skel = makeSkeleton(humanoidBoneDefs());
    auto rig = retargeting::HumanoidRigAsset::createFromSkeleton(skel, "src-guid");

    retargeting::RetargetMapData map;
    map.sourceRigAssetGuid = "src-guid";
    map.targetRigAssetGuid = "src-guid";

    auto ctx = animation::RetargetContext::build(skel, &skel, rig, rig, map);
    REQUIRE_FALSE(ctx.empty());
    CHECK(ctx.legLengthRatio == doctest::Approx(1.0f));

    auto clip = makeClip();

    animation::AnimationEvaluator nativeEval;
    nativeEval.loadAnimation(clip, skel);
    auto nativePose = nativeEval.evaluatePose(15.0f);

    animation::AnimationEvaluator retargetEval;
    retargetEval.loadAnimation(clip, skel, &ctx);
    auto retargetPose = retargetEval.evaluatePose(15.0f);

    REQUIRE(nativePose.size() == retargetPose.size());
    REQUIRE(nativePose.size() == skel.bones.size());

    for (size_t b = 0; b < nativePose.size(); ++b)
    {
        INFO("bone " << skel.bones[b].name);
        const float* n = &nativePose[b][0][0];
        const float* r = &retargetPose[b][0][0];
        for (int e = 0; e < 16; ++e)
            CHECK(r[e] == doctest::Approx(n[e]).epsilon(0.001));
    }
}

TEST_CASE("leg-length ratio scales with target proportions")
{
    // Minimal vertical leg chains; target segments are twice the source length.
    auto makeLeg = [](float seg) {
        return makeSkeleton({
            {"Hips", -1, {0.0f, 4.0f * seg, 0.0f}},
            {"LeftUpperLeg", 0, {0.0f, -seg, 0.0f}},
            {"LeftLowerLeg", 1, {0.0f, -seg, 0.0f}},
            {"LeftFoot", 2, {0.0f, -seg, 0.0f}},
        });
    };
    auto src = makeLeg(1.0f);
    auto dst = makeLeg(2.0f);

    auto srcRig = retargeting::HumanoidRigAsset::createFromSkeleton(src, "s");
    auto dstRig = retargeting::HumanoidRigAsset::createFromSkeleton(dst, "d");

    retargeting::RetargetMapData map;
    auto ctx = animation::RetargetContext::build(dst, &src, srcRig, dstRig, map);

    // target leg length / source leg length = 2.
    CHECK(ctx.legLengthRatio == doctest::Approx(2.0f));
}
