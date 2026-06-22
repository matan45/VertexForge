#include <doctest.h>

#include "animation/IKSolver.hpp"
#include "animation/IKPostProcess.hpp"
#include "animator/IKTypes.hpp"
#include "components/IKComponent.hpp"
#include "resource/Types.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

// ============================================================
// End-to-end IK solver + post-process regression tests.
//
// The pre-existing test_ik.cpp only covers the target-COMPUTING helpers
// (FootIK / HandIK / LookAt). Nothing exercised the FABRIK solver or the
// IKTargetComponent -> IKPostProcessor path, so a regression where IK
// "silently does nothing" (active chain, valid target, but the pose never
// changes) had no guard. These tests close that gap: they assert the solver
// actually bends a chain to its target and that applyIK mutates the bone
// matrices for an active chain (and leaves them untouched when it must not).
//
// NOTE: the original VK bug was that the GPU upload read the base state
// machine's pre-IK bone buffer instead of the AnimationLayerStack's composed
// (post-IK) buffer. That upload path needs a Vulkan device and is not
// CPU-testable here; these tests guard the layer below it — that applyIK
// genuinely produces a changed pose for the renderer to upload.
// ============================================================

using animation::FABRIKSolver;
using animation::IKPostProcessor;

namespace
{
    // A trivial 3-joint arm laid out straight along +Y:
    //   Shoulder (0,0,0) -> Elbow (0,1,0) -> Hand (0,2,0)
    // Identity bind / inverse-bind / globalInverse so that a bone's skinning
    // matrix equals its world transform — i.e. boneMatrices[i] translation IS
    // the joint's world position, which keeps the assertions exact.
    resource::SkeletonData makeArmSkeleton()
    {
        resource::SkeletonData s;
        s.globalInverseTransform = glm::mat4(1.0f);

        resource::SkeletonBone shoulder; shoulder.name = "Shoulder"; shoulder.parentIndex = -1;
        resource::SkeletonBone elbow;    elbow.name = "Elbow";       elbow.parentIndex = 0;
        resource::SkeletonBone hand;     hand.name = "Hand";         hand.parentIndex = 1;
        s.bones = { shoulder, elbow, hand };

        s.bindPoses = { glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f) };
        s.inverseBindPoses = { glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f) };
        return s;
    }

    std::vector<glm::mat4> makeArmBoneMatrices()
    {
        return {
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)),
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 0.0f)),
        };
    }

    glm::vec3 translationOf(const glm::mat4& m) { return glm::vec3(m[3]); }
}

TEST_SUITE("IK Solver")
{

TEST_CASE("FABRIK: reachable target — tip reaches it, root stays, bone lengths preserved")
{
    FABRIKSolver::ChainInput input;
    input.positions  = { {0,0,0}, {0,1,0}, {0,2,0} };          // straight arm, reach = 2
    input.rotations  = { glm::quat(1,0,0,0), glm::quat(1,0,0,0), glm::quat(1,0,0,0) };
    input.boneLengths = { 1.0f, 1.0f };
    input.constraints.resize(3);                                // all None

    animator::ik::IKTarget target;
    target.position = glm::vec3(1.0f, 1.0f, 0.0f);              // dist from root ~1.414 < 2

    auto result = FABRIKSolver::solve(input, target);

    REQUIRE(result.positions.size() == 3);
    CHECK(glm::length(result.positions.back()  - target.position)   < 0.05f); // tip reached
    CHECK(glm::length(result.positions.front() - glm::vec3(0.0f))   < 0.05f); // root pinned
    CHECK(glm::length(result.positions[1] - result.positions[0]) == doctest::Approx(1.0f).epsilon(0.03));
    CHECK(glm::length(result.positions[2] - result.positions[1]) == doctest::Approx(1.0f).epsilon(0.03));
}

TEST_CASE("FABRIK: unreachable target — chain extends straight toward it")
{
    FABRIKSolver::ChainInput input;
    input.positions  = { {0,0,0}, {0,1,0}, {0,2,0} };
    input.rotations  = { glm::quat(1,0,0,0), glm::quat(1,0,0,0), glm::quat(1,0,0,0) };
    input.boneLengths = { 1.0f, 1.0f };
    input.constraints.resize(3);

    animator::ik::IKTarget target;
    target.position = glm::vec3(10.0f, 0.0f, 0.0f);            // far beyond reach (2)

    auto result = FABRIKSolver::solve(input, target);

    float reach = glm::length(result.positions.back() - result.positions.front());
    CHECK(reach == doctest::Approx(2.0f).epsilon(0.05));       // fully extended
    CHECK(result.positions.back().x > 1.5f);                   // aimed at the target
}

} // TEST_SUITE("IK Solver")

TEST_SUITE("IK PostProcess")
{

TEST_CASE("applyIK: active chain bends the arm toward the target")
{
    resource::SkeletonData skeleton = makeArmSkeleton();
    std::vector<glm::mat4> boneMatrices = makeArmBoneMatrices();
    const glm::vec3 originalTip = translationOf(boneMatrices[2]); // (0,2,0)

    animator::ik::IKChainConfig chain;
    chain.chainName = "LeftArm";
    chain.tipBoneName = "Hand";
    chain.chainBoneNames = { "Shoulder", "Elbow" };
    chain.weight = 1.0f;
    chain.enabled = true;
    chain.constraints.resize(2);                                 // None for shoulder + elbow

    components::IKChainRuntimeState state;
    state.targetPosition = glm::vec3(1.0f, 1.0f, 0.0f);
    state.isActive = true;
    state.currentWeight = 1.0f;
    state.resolvedTipIndex = -1;                                 // force lazy resolve

    std::vector<animator::ik::IKChainConfig> chains = { chain };
    std::vector<components::IKChainRuntimeState> states = { state };

    IKPostProcessor::applyIK(boneMatrices, skeleton, chains, states);

    const glm::vec3 newTip = translationOf(boneMatrices[2]);
    const float origDist = glm::length(originalTip - glm::vec3(1.0f, 1.0f, 0.0f)); // ~1.414
    const float newDist  = glm::length(newTip      - glm::vec3(1.0f, 1.0f, 0.0f));

    CHECK(glm::length(newTip - originalTip) > 0.1f);             // the pose actually changed
    CHECK(newDist < origDist);                                   // moved toward the target
    CHECK(newDist < 0.2f);                                       // essentially reached it
    // Bone names resolved to indices on first call.
    REQUIRE(states[0].resolvedBoneIndices.size() >= 2);
    CHECK(states[0].resolvedTipIndex == 2);
}

TEST_CASE("applyIK: inactive chain leaves the pose untouched")
{
    resource::SkeletonData skeleton = makeArmSkeleton();
    std::vector<glm::mat4> boneMatrices = makeArmBoneMatrices();
    const glm::vec3 originalTip = translationOf(boneMatrices[2]);

    animator::ik::IKChainConfig chain;
    chain.chainName = "LeftArm";
    chain.tipBoneName = "Hand";
    chain.chainBoneNames = { "Shoulder", "Elbow" };
    chain.weight = 1.0f;
    chain.enabled = true;

    components::IKChainRuntimeState state;
    state.targetPosition = glm::vec3(1.0f, 1.0f, 0.0f);
    state.isActive = false;                                      // <-- not driven
    state.currentWeight = 0.0f;
    state.resolvedTipIndex = -1;

    std::vector<animator::ik::IKChainConfig> chains = { chain };
    std::vector<components::IKChainRuntimeState> states = { state };

    IKPostProcessor::applyIK(boneMatrices, skeleton, chains, states);

    CHECK(glm::length(translationOf(boneMatrices[2]) - originalTip) < 1e-5f);
}

TEST_CASE("applyIK: unresolved bone name is a safe no-op (no crash, no change)")
{
    resource::SkeletonData skeleton = makeArmSkeleton();
    std::vector<glm::mat4> boneMatrices = makeArmBoneMatrices();
    const glm::vec3 originalTip = translationOf(boneMatrices[2]);

    animator::ik::IKChainConfig chain;
    chain.chainName = "LeftArm";
    chain.tipBoneName = "Hand";
    chain.chainBoneNames = { "NoSuchBone", "Elbow" };            // first bone missing
    chain.weight = 1.0f;
    chain.enabled = true;

    components::IKChainRuntimeState state;
    state.targetPosition = glm::vec3(1.0f, 1.0f, 0.0f);
    state.isActive = true;
    state.currentWeight = 1.0f;
    state.resolvedTipIndex = -1;

    std::vector<animator::ik::IKChainConfig> chains = { chain };
    std::vector<components::IKChainRuntimeState> states = { state };

    IKPostProcessor::applyIK(boneMatrices, skeleton, chains, states);

    CHECK(glm::length(translationOf(boneMatrices[2]) - originalTip) < 1e-5f);
}

} // TEST_SUITE("IK PostProcess")
