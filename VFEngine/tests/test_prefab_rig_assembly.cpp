#include <doctest.h>

// VK-1433 Layer A — PrefabRigAssembly pure-math coverage.
//
// PrefabRigAssembly::build()/update() load from disk via AnimationDataCache and build
// real AnimationLayerStacks, which is not CPU-testable here (no assets, no Vulkan). The
// divergence risk lives in the per-frame MATH, which is factored into the free functions
// in namespace controllers::prefabrig — those are exercised below against the verified
// Play formulas, plus an end-to-end IK-feed check using IKPostProcessor directly (the
// same solver applyIK that the assembly calls in update() step 4).
//
// Pinned source references (the formulas these tests lock):
//   * socketModelTransform  == AnimationLayerStackControls.cpp:211-227
//   * composeChildWorld     == SocketAttachmentUpdater::applyModelOffset (cpp:308-318)
//   * ikTargetFromSocket    == Play feed: targetPartWorld * grip.getLocalOffsetMatrix()
//   * applyIK behavior       == test_ik_postprocess.cpp (shared solver)

#include "controllers/preview/PrefabRigAssembly.hpp"
#include "animation/IKPostProcess.hpp"
#include "animator/SocketTypes.hpp"
#include "animator/IKTypes.hpp"
#include "components/IKComponent.hpp"
#include "resource/Types.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>

using controllers::prefabrig::socketModelTransform;
using controllers::prefabrig::composeChildWorld;
using controllers::prefabrig::ikTargetFromSocket;

namespace
{
    glm::vec3 translationOf(const glm::mat4& m) { return glm::vec3(m[3]); }

    // A trivial 3-joint arm along +Y with identity bind/inverse-bind, mirroring
    // test_ik_postprocess.cpp:41 so a bone's skinning matrix translation IS its world pos.
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
}

TEST_SUITE("PrefabRigAssembly")
{

// ---------------------------------------------------------------------------
// 1. Bone-socket model transform == the verified AnimationLayerStack formula.
// ---------------------------------------------------------------------------
TEST_CASE("socketModelTransform matches translate(bonePos+localPos)*rot")
{
    // Bone 1 sits at world (0,1,0); identity bind => boneMeshPos = (0,1,0).
    std::vector<glm::mat4> boneMatrices = makeArmBoneMatrices();
    std::vector<glm::mat4> bindPoses(3, glm::mat4(1.0f));

    animator::SocketDefinition socket;
    socket.name = "Grip";
    socket.boneIndex = 1;
    socket.localPosition = glm::vec3(0.5f, 0.0f, 0.25f);
    socket.localRotation = glm::angleAxis(glm::radians(30.0f), glm::vec3(0, 0, 1));

    const glm::mat4 out = socketModelTransform(boneMatrices, bindPoses, socket);

    // Re-derive the reference formula independently.
    const glm::vec3 boneMeshPos(0.0f, 1.0f, 0.0f);
    const glm::mat4 expected =
        glm::translate(glm::mat4(1.0f), boneMeshPos + socket.localPosition)
        * glm::mat4_cast(socket.localRotation);

    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            CHECK(out[c][r] == doctest::Approx(expected[c][r]));

    // Translation column is the bone position offset by localPosition.
    CHECK(translationOf(out).x == doctest::Approx(0.5f));
    CHECK(translationOf(out).y == doctest::Approx(1.0f));
    CHECK(translationOf(out).z == doctest::Approx(0.25f));
}

TEST_CASE("socketModelTransform returns identity for an out-of-range bone index")
{
    std::vector<glm::mat4> boneMatrices = makeArmBoneMatrices();
    std::vector<glm::mat4> bindPoses(3, glm::mat4(1.0f));

    animator::SocketDefinition socket;
    socket.boneIndex = 99; // out of range
    socket.localPosition = glm::vec3(1.0f, 2.0f, 3.0f);

    const glm::mat4 out = socketModelTransform(boneMatrices, bindPoses, socket);
    CHECK(out == glm::mat4(1.0f));
}

// ---------------------------------------------------------------------------
// 2. Attachment composition == applyModelOffset (translation dropped), and a
//    depth>=2 nested chain converges in a single pass given topological order.
// ---------------------------------------------------------------------------
TEST_CASE("composeChildWorld drops the child translation, keeps socket origin")
{
    // Socket world placed at (5,1,2); child rotation/scale must not move the origin.
    const glm::mat4 socketWorld = glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 1.0f, 2.0f));

    const glm::mat4 childWorld =
        composeChildWorld(socketWorld, glm::vec3(0.0f, 45.0f, 0.0f), glm::vec3(2.0f));

    // The child rides the socket origin exactly (rotation/scale are about that origin).
    CHECK(translationOf(childWorld).x == doctest::Approx(5.0f));
    CHECK(translationOf(childWorld).y == doctest::Approx(1.0f));
    CHECK(translationOf(childWorld).z == doctest::Approx(2.0f));

    // Reference: socketWorld * (rot * scale) — verify full matrix equality.
    const glm::mat4 entityLocal =
        glm::mat4_cast(glm::quat(glm::radians(glm::vec3(0.0f, 45.0f, 0.0f))))
        * glm::scale(glm::mat4(1.0f), glm::vec3(2.0f));
    const glm::mat4 expected = socketWorld * entityLocal;
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            CHECK(childWorld[c][r] == doctest::Approx(expected[c][r]));
}

TEST_CASE("depth-2 nested attachment chain converges in one topological pass")
{
    // Manually drive the same composition order the assembly's update() uses:
    // root -> A (on a root socket) -> B (on an A socket). Parent-before-child means B
    // resolves correctly in the SAME pass.
    const glm::mat4 rootWorld = glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, 0.0f));

    // Socket on root at local +X 1.
    animator::SocketDefinition rootSocket;
    rootSocket.localPosition = glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::mat4 rootSocketWorld = rootWorld * rootSocket.getLocalOffsetMatrix();
    const glm::mat4 aWorld = composeChildWorld(rootSocketWorld, glm::vec3(0.0f), glm::vec3(1.0f));

    // A is at root(10,0,0) + socket(1,0,0) = (11,0,0).
    CHECK(translationOf(aWorld).x == doctest::Approx(11.0f));

    // Socket on A at local +Y 2.
    animator::SocketDefinition aSocket;
    aSocket.localPosition = glm::vec3(0.0f, 2.0f, 0.0f);
    const glm::mat4 aSocketWorld = aWorld * aSocket.getLocalOffsetMatrix();
    const glm::mat4 bWorld = composeChildWorld(aSocketWorld, glm::vec3(0.0f), glm::vec3(1.0f));

    // B is at A(11,0,0) + socket(0,2,0) = (11,2,0): the depth-2 chain accumulated correctly.
    CHECK(translationOf(bWorld).x == doctest::Approx(11.0f));
    CHECK(translationOf(bWorld).y == doctest::Approx(2.0f));
    CHECK(translationOf(bWorld).z == doctest::Approx(0.0f));
}

// ---------------------------------------------------------------------------
// 3. IK target feed from a grip socket + applyIK moves the tip toward it.
// ---------------------------------------------------------------------------
TEST_CASE("ikTargetFromSocket is the grip socket world origin")
{
    const glm::mat4 targetPartWorld = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 1.0f, 0.0f));

    animator::SocketDefinition grip;
    grip.name = "LeftHandGrip";
    grip.localPosition = glm::vec3(0.0f, 0.0f, 0.0f); // grip at the part origin

    const glm::vec3 target = ikTargetFromSocket(targetPartWorld, grip);
    CHECK(target.x == doctest::Approx(1.0f));
    CHECK(target.y == doctest::Approx(1.0f));
    CHECK(target.z == doctest::Approx(0.0f));

    // With a non-zero grip offset the target shifts by the offset (transformed by the part).
    grip.localPosition = glm::vec3(0.0f, 0.5f, 0.0f);
    const glm::vec3 target2 = ikTargetFromSocket(targetPartWorld, grip);
    CHECK(target2.y == doctest::Approx(1.5f));
}

TEST_CASE("IK feed drives applyIK: tip moves toward the socket-derived target")
{
    // The assembly's update() steps 3+4: compute a world target from a grip socket, populate
    // a runtime state (isActive, currentWeight=chain.weight), then applyIK on the body.
    resource::SkeletonData skeleton = makeArmSkeleton();
    std::vector<glm::mat4> boneMatrices = makeArmBoneMatrices();
    const glm::vec3 originalTip = translationOf(boneMatrices[2]); // (0,2,0)

    // Target part placed so its grip socket origin is a reachable target (1,1,0).
    const glm::mat4 targetPartWorld = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 1.0f, 0.0f));
    animator::SocketDefinition grip; grip.name = "Grip"; grip.localPosition = glm::vec3(0.0f);

    animator::ik::IKChainConfig chain;
    chain.chainName = "LeftArm";
    chain.tipBoneName = "Hand";
    chain.chainBoneNames = { "Shoulder", "Elbow" };
    chain.weight = 1.0f;
    chain.enabled = true;
    chain.constraints.resize(2);

    // --- Step 3: feed the runtime state from the grip socket (as update() does). ---
    components::IKChainRuntimeState state;
    state.targetPosition = ikTargetFromSocket(targetPartWorld, grip);
    state.targetRotation = std::nullopt;
    state.isActive = true;
    state.currentWeight = chain.weight; // == chain.weight, matching update() step 3
    state.resolvedTipIndex = -1;

    CHECK(state.targetPosition.x == doctest::Approx(1.0f));
    CHECK(state.currentWeight == doctest::Approx(1.0f));

    // --- Step 4: apply IK on the body (body part world = identity here). ---
    std::vector<animator::ik::IKChainConfig> chains = { chain };
    std::vector<components::IKChainRuntimeState> states = { state };
    animation::IKPostProcessor::applyIK(boneMatrices, skeleton, chains, states);

    const glm::vec3 newTip = translationOf(boneMatrices[2]);
    const float origDist = glm::length(originalTip - state.targetPosition);
    const float newDist  = glm::length(newTip      - state.targetPosition);

    CHECK(glm::length(newTip - originalTip) > 0.1f); // pose changed
    CHECK(newDist < origDist);                        // moved toward target
    CHECK(newDist < 0.2f);                            // essentially reached
}

// ---------------------------------------------------------------------------
// 4. Live edits (no disk): mutating an editable socket moves the IK target /
//    child world by the same delta — the property the authoring panels rely on.
// ---------------------------------------------------------------------------
TEST_CASE("editing a grip socket shifts the IK target by the same delta")
{
    const glm::mat4 targetPartWorld = glm::mat4(1.0f); // identity part world: model == world

    animator::SocketDefinition grip; grip.localPosition = glm::vec3(0.0f, 0.0f, 0.0f);
    const glm::vec3 before = ikTargetFromSocket(targetPartWorld, grip);

    // Simulate a live authoring edit on the editable socket copy.
    const glm::vec3 delta(0.3f, -0.2f, 0.7f);
    grip.localPosition += delta;
    const glm::vec3 after = ikTargetFromSocket(targetPartWorld, grip);

    CHECK((after - before).x == doctest::Approx(delta.x));
    CHECK((after - before).y == doctest::Approx(delta.y));
    CHECK((after - before).z == doctest::Approx(delta.z));
}

TEST_CASE("editing a bone socket moves the attached child world by the same delta")
{
    // A weapon attached on a body bone socket: moving the bone socket's localPosition
    // shifts the weapon's partWorld by exactly that delta (identity rotation/scale path).
    std::vector<glm::mat4> bodyBones = makeArmBoneMatrices();
    std::vector<glm::mat4> bindPoses(3, glm::mat4(1.0f));
    const glm::mat4 bodyWorld = glm::mat4(1.0f);

    animator::SocketDefinition weaponSocket;
    weaponSocket.boneIndex = 1; // body bone at (0,1,0)
    weaponSocket.localPosition = glm::vec3(0.0f);

    auto weaponWorld = [&](const animator::SocketDefinition& s)
    {
        const glm::mat4 socketModel = socketModelTransform(bodyBones, bindPoses, s);
        const glm::mat4 socketWorld = bodyWorld * socketModel;
        return composeChildWorld(socketWorld, glm::vec3(0.0f), glm::vec3(1.0f));
    };

    const glm::vec3 before = translationOf(weaponWorld(weaponSocket));

    const glm::vec3 delta(0.4f, 0.0f, -0.6f);
    weaponSocket.localPosition += delta; // live edit of editableSockets(body)[weaponR]
    const glm::vec3 after = translationOf(weaponWorld(weaponSocket));

    CHECK((after - before).x == doctest::Approx(delta.x));
    CHECK((after - before).y == doctest::Approx(delta.y));
    CHECK((after - before).z == doctest::Approx(delta.z));
}

// ---------------------------------------------------------------------------
// VK-1433 Phase 1 — read-only overlay accessors never dangle on an unbuilt rig.
// (A built rig needs disk assets + Vulkan, out of scope here; the empties are the
//  contract the overlay relies on for static / out-of-range parts and chains.)
// ---------------------------------------------------------------------------
TEST_CASE("skeleton()/ikOverlayInfo() are safe on an unbuilt / out-of-range assembly")
{
    controllers::PrefabRigAssembly assembly;
    CHECK(assembly.isBuilt() == false);
    CHECK(assembly.partCount() == 0);

    // Out-of-range part -> empty static skeleton (no bones / bind poses), no dangle.
    const resource::SkeletonData& skel = assembly.skeleton(0);
    CHECK(skel.bones.empty());
    CHECK(skel.bindPoses.empty());

    // Out-of-range chain -> default (inactive, unresolved) overlay info.
    const auto info = assembly.ikOverlayInfo(0);
    CHECK(info.active == false);
    CHECK(info.bodyPartIndex == -1);
    CHECK(info.resolvedTipIndex == -1);
    // The overlay reads targetPosition only when active; the default must be a safe zero so a
    // stale/garbage marker is never drawn for an unresolved chain.
    CHECK(info.targetPosition.x == doctest::Approx(0.0f));
    CHECK(info.targetPosition.y == doctest::Approx(0.0f));
    CHECK(info.targetPosition.z == doctest::Approx(0.0f));
}

TEST_CASE("skeleton(): the empty static skeleton reference is reused and never dangles")
{
    controllers::PrefabRigAssembly assembly;
    // Same stable reference for every out-of-range index (the emptySkeleton member).
    const resource::SkeletonData& a = assembly.skeleton(0);
    const resource::SkeletonData& b = assembly.skeleton(123);
    CHECK(&a == &b);
    CHECK(a.bones.empty());
    CHECK(a.bindPoses.empty());
    CHECK(a.inverseBindPoses.empty());
}

} // TEST_SUITE("PrefabRigAssembly")
