#include <doctest.h>

// Phase 2 (Prefab Rig Preview editing safety) — GAP coverage that complements test_prefab_rig_phase2.cpp.
//
// Focus areas (the still-live header-only seams; the PrefabRefWriter / PrefabTransformWriter JSON
// suites were removed with those headers in VK-1433 Phase 4c — the part Transform gizmo and the
// drag-swap now edit the source ENTITY directly, persisted by SavePrefab):
//   * validatePartRefs: retarget + defaultMaterial missing; mixed present/missing submesh materials;
//     the singular validatePartRef entry point.
//   * childHasDroppedTranslation / sourcePositionForPart: part-index mapping SKIPS non-mesh nodes
//     exactly like buildPrefabRigDescDTO; negative index.
//   * chainsEqual: a constraints-ONLY diff is treated as EQUAL (the documented no-op-gating behavior,
//     chainEquals omits `constraints`); socketEquals across every compared member.
//
// All pure / dependency-injected — no imgui, no Graphics, no real filesystem, no EventDispatcher.

#include "windows/preview/PrefabRigValidation.hpp"
#include "windows/preview/PrefabRigEditUndo.hpp"
#include "windows/preview/PrefabRigDescBuilder.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <map>
#include <string>
#include <unordered_set>

TEST_SUITE("PrefabRigPhase2Gaps.Validation")
{
    TEST_CASE("validatePartRef (singular): retarget + default material missing are flagged independently")
    {
        services::PrefabRigPartDTO part;
        part.meshPath = "m.vfMesh";                  // present
        part.retargetPath = "r.vfretarget";          // missing
        part.defaultMaterialPath = "mat.vfmaterial"; // missing
        std::unordered_set<std::string> present = {"m.vfMesh"};
        auto exists = [&](const std::string& p) { return present.count(p) > 0; };

        auto s = windows::prefabrigval::validatePartRef(part, exists);
        CHECK_FALSE(s.meshMissing);
        CHECK(s.retargetMissing);
        CHECK(s.defaultMaterialMissing);
        CHECK(s.animatorMissing == false); // empty path
        CHECK(s.anyMissing());
    }

    TEST_CASE("validatePartRef: mixed submesh materials => only the missing ones are reported")
    {
        services::PrefabRigPartDTO part;
        part.meshPath = "m.vfMesh";
        part.subMeshMaterials["blade"] = "blade.vfmaterial"; // present
        part.subMeshMaterials["hilt"] = "hilt.vfmaterial";   // missing
        part.subMeshMaterials["empty"] = "";                  // no ref -> ignored
        std::unordered_set<std::string> present = {"m.vfMesh", "blade.vfmaterial"};
        auto exists = [&](const std::string& p) { return present.count(p) > 0; };

        auto s = windows::prefabrigval::validatePartRef(part, exists);
        REQUIRE(s.missingSubMeshMaterials.size() == 1);
        CHECK(s.missingSubMeshMaterials[0] == "hilt");
        CHECK(s.anyMissing());
    }

    TEST_CASE("validatePartRefs: a part with every path empty reports nothing missing")
    {
        services::PrefabRigDescDTO desc;
        desc.parts.push_back(services::PrefabRigPartDTO{}); // all paths empty
        auto exists = [](const std::string&) { return false; };
        auto statuses = windows::prefabrigval::validatePartRefs(desc, exists);
        REQUIRE(statuses.size() == 1);
        CHECK_FALSE(statuses[0].anyMissing());
        CHECK(windows::prefabrigval::countPartsWithMissingRefs(statuses) == 0);
    }
}

TEST_SUITE("PrefabRigPhase2Gaps.TranslateDrop")
{
    TEST_CASE("sourcePositionForPart: skips a non-mesh intermediate node when counting parts")
    {
        // Body(mesh, part0) -> Pivot(NO mesh, pos far away) -> Weapon(mesh, part1, pos 1,2,3).
        windows::PrefabEntityNode weapon;
        weapon.name = "Weapon";
        weapon.meshPath = "weapon.vfMesh";
        weapon.position = glm::vec3(1.0f, 2.0f, 3.0f);

        windows::PrefabEntityNode pivot;
        pivot.name = "Pivot";          // no meshPath -> not mesh-bearing
        pivot.position = glm::vec3(99.0f, 99.0f, 99.0f);
        pivot.children.push_back(weapon);

        windows::PrefabEntityNode body;
        body.name = "Body";
        body.meshPath = "body.vfMesh";
        body.position = glm::vec3(0.0f);
        body.children.push_back(pivot);

        // Part 1 must map to Weapon (1,2,3), NOT the non-mesh Pivot (99,99,99).
        CHECK(windows::prefabrigval::sourcePositionForPart(body, 0) == glm::vec3(0.0f));
        CHECK(windows::prefabrigval::sourcePositionForPart(body, 1) == glm::vec3(1.0f, 2.0f, 3.0f));
    }

    TEST_CASE("sourcePositionForPart: negative part index returns origin")
    {
        windows::PrefabEntityNode body;
        body.name = "Body";
        body.meshPath = "body.vfMesh";
        body.position = glm::vec3(7.0f);
        CHECK(windows::prefabrigval::sourcePositionForPart(body, -1) == glm::vec3(0.0f));
    }

    TEST_CASE("childHasDroppedTranslation: epsilon boundary is exclusive (> not >=)")
    {
        using windows::prefabrigval::childHasDroppedTranslation;
        // Default epsilon 1e-5; a value EQUAL to epsilon must NOT warn (strict >).
        CHECK_FALSE(childHasDroppedTranslation(0, glm::vec3(1e-5f, 0.0f, 0.0f)));
        // Just above epsilon warns.
        CHECK(childHasDroppedTranslation(0, glm::vec3(2e-5f, 0.0f, 0.0f)));
        // A custom larger epsilon suppresses a small offset.
        CHECK_FALSE(childHasDroppedTranslation(0, glm::vec3(0.05f, 0.0f, 0.0f), 0.1f));
    }
}

TEST_SUITE("PrefabRigPhase2Gaps.UndoData")
{
    TEST_CASE("chainsEqual: a constraints-ONLY difference is treated as EQUAL (documented no-op gating)")
    {
        using namespace windows::prefabrigedit;

        animator::ik::IKChainConfig a;
        a.chainName = "L_Arm";
        a.tipBoneName = "Hand_L";
        a.chainBoneNames = {"Shoulder_L", "Elbow_L", "Hand_L"};
        a.weight = 1.0f;
        a.enabled = true;

        animator::ik::IKChainConfig b = a;
        // Mutate ONLY constraints — chainEquals deliberately ignores the constraints vector.
        animator::ik::JointConstraint hinge;
        hinge.type = animator::ik::JointConstraintType::Hinge;
        b.constraints.push_back(hinge);

        CHECK(chainsEqual({a}, {b})); // constraints diff -> still "equal" (no undo pushed)

        // Sanity: a tracked-field diff IS detected.
        b = a;
        b.chainBoneNames = {"Shoulder_L", "Hand_L"};
        CHECK_FALSE(chainsEqual({a}, {b}));

        b = a;
        b.tipBoneName = "Hand_R";
        CHECK_FALSE(chainsEqual({a}, {b}));
    }

    TEST_CASE("socketEquals: every compared member flips the result")
    {
        using namespace windows::prefabrigedit;

        animator::SocketDefinition base;
        base.name = "Hand";
        base.targetBoneName = "Hand_L";
        base.boneIndex = 7;
        base.localPosition = glm::vec3(1.0f, 0.0f, 0.0f);
        base.localRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

        CHECK(socketsEqual({base}, {base}));

        auto diffName = base;        diffName.name = "Foot";
        auto diffBone = base;        diffBone.targetBoneName = "Hand_R";
        auto diffIndex = base;       diffIndex.boneIndex = 8;
        auto diffPos = base;         diffPos.localPosition = glm::vec3(0.0f);
        auto diffRot = base;         diffRot.localRotation = glm::quat(0.0f, 1.0f, 0.0f, 0.0f);

        CHECK_FALSE(socketsEqual({base}, {diffName}));
        CHECK_FALSE(socketsEqual({base}, {diffBone}));
        CHECK_FALSE(socketsEqual({base}, {diffIndex}));
        CHECK_FALSE(socketsEqual({base}, {diffPos}));
        CHECK_FALSE(socketsEqual({base}, {diffRot}));
    }

    TEST_CASE("chainsEqual / socketsEqual: empty vectors compare equal")
    {
        using namespace windows::prefabrigedit;
        CHECK(socketsEqual({}, {}));
        CHECK(chainsEqual({}, {}));
    }

    // VK-1433 Phase 4d: the previewTransforms snapshot field + its transform-only-engagement test were
    // removed — the part Transform gizmo now edits the source ENTITY transform (replayed by
    // PrefabRigEntityTransformUndoCommand, which captures TransformData by value), not a transient
    // preview-offset map. An IK-binding-only snapshot still engages just ikBindings.
    TEST_CASE("snapshot: an ik-binding-only snapshot engages only ikBindings")
    {
        using namespace windows::prefabrigedit;
        PrefabRigEditSnapshot snap;
        snap.ikBindings = std::vector<IKBindingSnapshot>{IKBindingSnapshot{2, "muzzle"}};

        CHECK(snap.ikBindings.has_value());
        CHECK_FALSE(snap.socketPart.has_value());
        CHECK_FALSE(snap.chains.has_value());
        CHECK((*snap.ikBindings).size() == 1);
        CHECK((*snap.ikBindings)[0].targetPartIndex == 2);
        CHECK((*snap.ikBindings)[0].targetSocketName == "muzzle");
    }
}
