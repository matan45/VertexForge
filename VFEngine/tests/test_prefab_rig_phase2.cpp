#include <doctest.h>

// Phase 2 (Prefab Rig Preview editing safety) — CPU coverage for the header-only seams:
//   * windows::prefabrigval::validatePartRefs  (injected fake fs::exists predicate)
//   * windows::prefabrigval::childHasDroppedTranslation + sourcePositionForPart
//   * windows::prefabrigedit equality + snapshot DATA logic (the separable part of the undo command)
//
// All three are pure / dependency-injected so they run with no imgui, no Graphics, no real filesystem,
// no EventDispatcher.
//
// VK-1433 Phase 4c retired the JSON writers (PrefabTransformWriter / PrefabRefWriter): the part
// Transform gizmo and the drag-swap now edit the source ENTITY directly (persisted by SavePrefab),
// so the prefabtransform::zeroPartTranslation / prefabref::applyRefEdits suites that lived here are
// gone with the headers. The entity-driven save spine is covered by test_prefab_rig_live_builder.cpp.

#include "windows/preview/PrefabRigValidation.hpp"
#include "windows/preview/PrefabRigEditUndo.hpp"
#include "windows/preview/PrefabRigDescBuilder.hpp"

#include <nlohmann/json.hpp>
#include <glm/glm.hpp>
#include <map>
#include <set>
#include <string>
#include <unordered_set>

using nlohmann::json;

namespace
{
    services::PrefabRigDescDTO makeDesc()
    {
        services::PrefabRigDescDTO desc;

        services::PrefabRigPartDTO body;
        body.meshPath = "body.vfMesh";
        body.animatorPath = "body.vfAnim";
        body.parentPartIndex = -1;
        desc.parts.push_back(body);

        services::PrefabRigPartDTO weapon;
        weapon.meshPath = "weapon.vfMesh";
        weapon.animatorPath = ""; // static
        weapon.parentPartIndex = 0;
        weapon.defaultMaterialPath = "weapon_mat.vfmaterial";
        weapon.subMeshMaterials["blade"] = "blade.vfmaterial";
        desc.parts.push_back(weapon);

        return desc;
    }
}

TEST_SUITE("PrefabRigPhase2.Validation")
{
    TEST_CASE("validatePartRefs: all present => no missing")
    {
        auto desc = makeDesc();
        std::unordered_set<std::string> present = {
            "body.vfMesh", "body.vfAnim", "weapon.vfMesh",
            "weapon_mat.vfmaterial", "blade.vfmaterial"
        };
        auto exists = [&](const std::string& p) { return present.count(p) > 0; };

        auto statuses = windows::prefabrigval::validatePartRefs(desc, exists);
        REQUIRE(statuses.size() == 2);
        CHECK_FALSE(statuses[0].anyMissing());
        CHECK_FALSE(statuses[1].anyMissing());
        CHECK(windows::prefabrigval::countPartsWithMissingRefs(statuses) == 0);
    }

    TEST_CASE("validatePartRefs: a missing mesh + submesh material are flagged; empty paths ignored")
    {
        auto desc = makeDesc();
        // Body mesh present, but body.vfAnim missing. Weapon mesh present, blade material missing.
        std::unordered_set<std::string> present = {
            "body.vfMesh", "weapon.vfMesh", "weapon_mat.vfmaterial"
        };
        auto exists = [&](const std::string& p) { return present.count(p) > 0; };

        auto statuses = windows::prefabrigval::validatePartRefs(desc, exists);
        REQUIRE(statuses.size() == 2);

        // Body: animator missing only (mesh present, no retarget/material => empty paths skipped).
        CHECK(statuses[0].meshMissing == false);
        CHECK(statuses[0].animatorMissing == true);
        CHECK(statuses[0].retargetMissing == false);   // empty path is "no ref", not broken
        CHECK(statuses[0].defaultMaterialMissing == false);
        CHECK(statuses[0].anyMissing());

        // Weapon: mesh + default material present, blade submesh material missing.
        CHECK(statuses[1].meshMissing == false);
        CHECK(statuses[1].defaultMaterialMissing == false);
        REQUIRE(statuses[1].missingSubMeshMaterials.size() == 1);
        CHECK(statuses[1].missingSubMeshMaterials[0] == "blade");
        CHECK(statuses[1].anyMissing());

        CHECK(windows::prefabrigval::countPartsWithMissingRefs(statuses) == 2);
    }

    TEST_CASE("validatePartRefs: empty predicate-false leaves empty paths un-flagged")
    {
        services::PrefabRigDescDTO desc;
        services::PrefabRigPartDTO p; // every path empty
        p.meshPath = "m.vfMesh";      // only mesh set
        desc.parts.push_back(p);

        auto exists = [](const std::string&) { return false; }; // nothing on disk
        auto statuses = windows::prefabrigval::validatePartRefs(desc, exists);
        REQUIRE(statuses.size() == 1);
        CHECK(statuses[0].meshMissing == true);
        CHECK(statuses[0].animatorMissing == false); // empty animator path is not "missing"
        CHECK(statuses[0].retargetMissing == false);
        CHECK(statuses[0].defaultMaterialMissing == false);
    }
}

TEST_SUITE("PrefabRigPhase2.TranslateDrop")
{
    TEST_CASE("childHasDroppedTranslation: only fires for socketed child with non-zero position")
    {
        using windows::prefabrigval::childHasDroppedTranslation;
        // Root part keeps its translation -> never warns regardless of position.
        CHECK_FALSE(childHasDroppedTranslation(-1, glm::vec3(5.0f, 0.0f, 0.0f)));
        // Socketed child with zero position -> nothing dropped.
        CHECK_FALSE(childHasDroppedTranslation(0, glm::vec3(0.0f)));
        // Socketed child with a non-zero component -> warns.
        CHECK(childHasDroppedTranslation(0, glm::vec3(0.0f, 0.0f, 0.01f)));
        // Sub-epsilon noise does not warn.
        CHECK_FALSE(childHasDroppedTranslation(0, glm::vec3(1e-7f, 0.0f, 0.0f)));
    }

    TEST_CASE("sourcePositionForPart: returns the k-th mesh-bearing node's own position")
    {
        // Build a tree: root mesh "Body" (part 0) with child mesh "Weapon" (part 1) at (1,2,3).
        windows::PrefabEntityNode body;
        body.name = "Body";
        body.meshPath = "body.vfMesh";
        body.animatorPath = "body.vfAnim";
        body.position = glm::vec3(0.0f);

        windows::PrefabEntityNode weapon;
        weapon.name = "Weapon";
        weapon.meshPath = "weapon.vfMesh";
        weapon.position = glm::vec3(1.0f, 2.0f, 3.0f);
        weapon.hasSocketAttachment = true;
        weapon.attachParentEntityName = "Body";
        body.children.push_back(weapon);

        CHECK(windows::prefabrigval::sourcePositionForPart(body, 0) == glm::vec3(0.0f));
        CHECK(windows::prefabrigval::sourcePositionForPart(body, 1) == glm::vec3(1.0f, 2.0f, 3.0f));
        CHECK(windows::prefabrigval::sourcePositionForPart(body, 5) == glm::vec3(0.0f)); // OOB
    }
}

TEST_SUITE("PrefabRigPhase2.UndoData")
{
    // The undo command's CQRS replay needs a live EventDispatcher, but its DATA logic — the equality
    // helpers that gate "push or skip", and the snapshot field engagement — is separable + tested here.
    TEST_CASE("socketsEqual / chainsEqual detect a real change")
    {
        using namespace windows::prefabrigedit;

        animator::SocketDefinition a;
        a.name = "Hand";
        a.localPosition = glm::vec3(0.0f);
        animator::SocketDefinition b = a;
        CHECK(socketsEqual({a}, {b}));

        b.localPosition = glm::vec3(0.0f, 1.0f, 0.0f);
        CHECK_FALSE(socketsEqual({a}, {b}));         // position changed
        CHECK_FALSE(socketsEqual({a}, {a, a}));      // size changed

        animator::ik::IKChainConfig c;
        c.chainName = "L_Arm";
        c.weight = 1.0f;
        c.enabled = true;
        animator::ik::IKChainConfig d = c;
        CHECK(chainsEqual({c}, {d}));
        d.weight = 0.5f;
        CHECK_FALSE(chainsEqual({c}, {d}));
        d = c;
        d.enabled = false;
        CHECK_FALSE(chainsEqual({c}, {d}));
    }

    TEST_CASE("snapshot field engagement: each kind engages only its own optional")
    {
        using namespace windows::prefabrigedit;

        // A sockets snapshot built by hand (mirrors snapshotSockets): socketPart engaged, others not.
        PrefabRigEditSnapshot socketsSnap;
        socketsSnap.socketPart = 2;
        socketsSnap.sockets = {animator::SocketDefinition{}};
        CHECK(socketsSnap.socketPart.has_value());
        CHECK_FALSE(socketsSnap.chains.has_value());
        CHECK_FALSE(socketsSnap.previewTransforms.has_value());

        PrefabRigEditSnapshot chainsSnap;
        chainsSnap.chains = std::vector<animator::ik::IKChainConfig>{};
        CHECK_FALSE(chainsSnap.socketPart.has_value());
        CHECK(chainsSnap.chains.has_value());

        PrefabRigEditSnapshot xformSnap;
        std::map<int, glm::mat4> m;
        m[1] = glm::mat4(2.0f);
        xformSnap.previewTransforms = m;
        CHECK(xformSnap.previewTransforms.has_value());
        CHECK((*xformSnap.previewTransforms)[1] == glm::mat4(2.0f));
    }
}
