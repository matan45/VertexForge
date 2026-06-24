#include <doctest.h>

// Phase 2 (Prefab Rig Preview editing safety) — GAP coverage that complements test_prefab_rig_phase2.cpp.
//
// Focus areas (the highest-risk / under-covered seams per the Phase-2 contract):
//   * PrefabRefWriter cold-DB fallback proofs: resolve-EMPTY overwrites a pre-existing stale GUID
//     with the NEW path (in BOTH keys) on animator + material refs (not just mesh) — never the OLD
//     GUID, never an erase/zero-GUID (those dead-ref on load: readAssetRef only consults <key>Path
//     for a VALID-but-unresolvable <key>); a swap touches ONLY its ref family; un-swapped parts are
//     BYTE-IDENTICAL after a round-trip; multiple swaps; swapping a part that had no prior ref;
//     malformed-root no-ops; writeRefPair in isolation.
//   * validatePartRefs: retarget + defaultMaterial missing; mixed present/missing submesh materials;
//     the singular validatePartRef entry point.
//   * childHasDroppedTranslation / sourcePositionForPart: part-index mapping SKIPS non-mesh nodes
//     exactly like buildPrefabRigDescDTO; negative index.
//   * zeroPartTranslation: root part; already-zero round-trip stability; absent-transform materializes
//     identity rot/scale.
//   * chainsEqual: a constraints-ONLY diff is treated as EQUAL (the documented no-op-gating behavior,
//     chainEquals omits `constraints`); socketEquals across every compared member.
//
// All pure / dependency-injected — no imgui, no Graphics, no real filesystem, no EventDispatcher.

#include "windows/preview/PrefabRigValidation.hpp"
#include "windows/preview/PrefabRigEditUndo.hpp"
#include "windows/preview/PrefabTransformWriter.hpp"
#include "windows/preview/PrefabRefWriter.hpp"
#include "windows/preview/PrefabRigDescBuilder.hpp"

#include <nlohmann/json.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <map>
#include <string>
#include <unordered_set>

using nlohmann::json;

namespace
{
    // A prefab with two mesh-bearing parts, each carrying a full mesh ref pair, plus a material
    // component on the BODY (so a material-family swap on the body has a pre-existing GUID to replace).
    json makeRefPrefabRich()
    {
        json weapon = {
            {"name", "Weapon"},
            {"transform", {{"position", {0.0f, 0.0f, 0.0f}}, {"rotation", {0.0f, 0.0f, 0.0f}}, {"scale", {1.0f, 1.0f, 1.0f}}}},
            {"components", {
                {"mesh", {
                    {"meshRef", "OLDGUID-weapon"}, {"meshRefPath", "old_weapon.vfMesh"},
                    {"animatorRef", "OLDGUID-weaponAnim"}, {"animatorRefPath", "old_weapon.vfAnim"}
                }},
                {"socketAttachment", {{"parentEntityName", "Body"}, {"socketName", "Hand"}}}
            }},
            {"children", json::array()}
        };
        json body = {
            {"name", "Body"},
            {"transform", {{"position", {0.0f, 0.0f, 0.0f}}, {"rotation", {0.0f, 0.0f, 0.0f}}, {"scale", {1.0f, 1.0f, 1.0f}}}},
            {"components", {
                {"mesh", {{"meshRef", "OLDGUID-body"}, {"meshRefPath", "old_body.vfMesh"}}},
                {"material", {{"defaultMaterialRef", "OLDGUID-bodyMat"}, {"defaultMaterialRefPath", "old_body.vfmaterial"}}}
            }},
            {"children", json::array({weapon})}
        };
        return json{{"version", "1.0"}, {"prefab", {{"name", "Rig"}, {"entity", body}}}};
    }
}

TEST_SUITE("PrefabRigPhase2Gaps.RefWriter")
{
    // ---- writeRefPair in isolation ----------------------------------------------------------------

    TEST_CASE("writeRefPair: resolver yields a GUID => writes BOTH keys")
    {
        json obj = json::object();
        auto resolver = [](const std::string&) -> std::string { return "GUID-123"; };
        const bool wrote = windows::prefabref::writeRefPair(obj, "meshRef", "new.vfMesh", resolver);
        CHECK(wrote);
        CHECK(obj["meshRefPath"].get<std::string>() == "new.vfMesh");
        CHECK(obj["meshRef"].get<std::string>() == "GUID-123");
    }

    TEST_CASE("writeRefPair: empty resolver overwrites a stale GUID with the NEW path (both keys)")
    {
        json obj = {{"meshRef", "STALE"}, {"meshRefPath", "old.vfMesh"}};
        const bool wrote = windows::prefabref::writeRefPair(obj, "meshRef", "new.vfMesh", {});
        CHECK(wrote);
        CHECK(obj["meshRefPath"].get<std::string>() == "new.vfMesh");
        // No resolver -> <key> holds the NEW path (path-shaped, recoverable via the reader's fromPath
        // branch), NOT the stale GUID. A bare erase/zero-GUID would dead-ref on load.
        REQUIRE(obj.contains("meshRef"));
        CHECK(obj["meshRef"].get<std::string>() == "new.vfMesh");
        CHECK(obj["meshRef"].get<std::string>() != "STALE");
    }

    TEST_CASE("writeRefPair: empty resolver on an absent GUID key writes the NEW path into both keys")
    {
        json obj = json::object(); // no meshRef key at all
        const bool wrote = windows::prefabref::writeRefPair(obj, "meshRef", "fresh.vfMesh", {});
        CHECK(wrote);
        CHECK(obj["meshRefPath"].get<std::string>() == "fresh.vfMesh");
        REQUIRE(obj.contains("meshRef"));
        CHECK(obj["meshRef"].get<std::string>() == "fresh.vfMesh");
    }

    TEST_CASE("writeRefPair: a resolver that returns empty STRING is treated as unresolvable")
    {
        json obj = {{"meshRef", "STALE"}};
        auto emptyResolver = [](const std::string&) -> std::string { return ""; };
        windows::prefabref::writeRefPair(obj, "meshRef", "new.vfMesh", emptyResolver);
        // empty == unresolvable: <key> becomes the NEW path (not the stale GUID, not erased).
        REQUIRE(obj.contains("meshRef"));
        CHECK(obj["meshRef"].get<std::string>() == "new.vfMesh");
        CHECK(obj["meshRefPath"].get<std::string>() == "new.vfMesh");
    }

    // ---- applyRefEdits: family isolation + untouched byte-identity --------------------------------

    TEST_CASE("applyRefEdits: an animator swap with no resolver writes the NEW animator path into both keys, mesh family untouched")
    {
        json prefab = makeRefPrefabRich();

        windows::prefabref::PartRefEdit e;
        e.part = 1; // weapon
        e.animatorPath = "new_weapon.vfAnim";
        const int applied = windows::prefabref::applyRefEdits(prefab, {e}, {});
        CHECK(applied == 1);

        const json& weaponMesh = prefab["prefab"]["entity"]["children"][0]["components"]["mesh"];
        // Animator family rewritten: empty resolver -> NEW path in both keys (recoverable on load).
        CHECK(weaponMesh["animatorRefPath"].get<std::string>() == "new_weapon.vfAnim");
        REQUIRE(weaponMesh.contains("animatorRef"));
        CHECK(weaponMesh["animatorRef"].get<std::string>() == "new_weapon.vfAnim");
        // Mesh family BYTE-untouched (a non-empty meshPath was not part of this edit).
        CHECK(weaponMesh["meshRef"].get<std::string>() == "OLDGUID-weapon");
        CHECK(weaponMesh["meshRefPath"].get<std::string>() == "old_weapon.vfMesh");
    }

    TEST_CASE("applyRefEdits: a default-material swap (empty resolver) overwrites the body's material with the NEW path")
    {
        json prefab = makeRefPrefabRich();

        windows::prefabref::PartRefEdit e;
        e.part = 0; // body (has a material component with a stale GUID)
        e.defaultMaterialPath = "new_body.vfmaterial";
        const int applied = windows::prefabref::applyRefEdits(prefab, {e}, {});
        CHECK(applied == 1);

        const json& bodyMat = prefab["prefab"]["entity"]["components"]["material"];
        CHECK(bodyMat["defaultMaterialRefPath"].get<std::string>() == "new_body.vfmaterial");
        REQUIRE(bodyMat.contains("defaultMaterialRef"));
        // Empty resolver -> NEW path (not the stale GUID, not erased).
        CHECK(bodyMat["defaultMaterialRef"].get<std::string>() == "new_body.vfmaterial");
        CHECK(bodyMat["defaultMaterialRef"].get<std::string>() != "OLDGUID-bodyMat");
        // Body mesh family is byte-identical (material swap must not touch the mesh).
        const json& bodyMesh = prefab["prefab"]["entity"]["components"]["mesh"];
        CHECK(bodyMesh["meshRef"].get<std::string>() == "OLDGUID-body");
        CHECK(bodyMesh["meshRefPath"].get<std::string>() == "old_body.vfMesh");
    }

    TEST_CASE("applyRefEdits: a single-part swap leaves every UN-swapped part byte-identical after round-trip")
    {
        json prefab = makeRefPrefabRich();
        const json before = prefab; // deep copy

        windows::prefabref::PartRefEdit e;
        e.part = 1; // weapon mesh only
        e.meshPath = "new_weapon.vfMesh";
        auto resolver = [](const std::string& p) -> std::string {
            return p == "new_weapon.vfMesh" ? "NEWGUID-weapon" : std::string();
        };
        CHECK(windows::prefabref::applyRefEdits(prefab, {e}, resolver) == 1);

        // The BODY node (part 0) — every component and field — must be byte-identical to before.
        CHECK(prefab["prefab"]["entity"]["components"] ==
              before["prefab"]["entity"]["components"]);
        // The weapon's NON-ref siblings (socketAttachment, transform, name) survive untouched.
        const json& weaponNodeNow = prefab["prefab"]["entity"]["children"][0];
        const json& weaponNodeBefore = before["prefab"]["entity"]["children"][0];
        CHECK(weaponNodeNow["components"]["socketAttachment"] ==
              weaponNodeBefore["components"]["socketAttachment"]);
        CHECK(weaponNodeNow["components"]["mesh"]["animatorRef"] ==
              weaponNodeBefore["components"]["mesh"]["animatorRef"]);
        CHECK(weaponNodeNow["transform"] == weaponNodeBefore["transform"]);
        CHECK(weaponNodeNow["name"] == weaponNodeBefore["name"]);
        CHECK(prefab["version"] == before["version"]);
    }

    TEST_CASE("applyRefEdits: two swaps in one prefab apply to both targets, each touching only its family")
    {
        json prefab = makeRefPrefabRich();

        windows::prefabref::PartRefEdit body;
        body.part = 0;
        body.meshPath = "new_body.vfMesh";

        windows::prefabref::PartRefEdit weapon;
        weapon.part = 1;
        weapon.meshPath = "new_weapon.vfMesh";

        auto resolver = [](const std::string& p) -> std::string {
            if (p == "new_body.vfMesh") return "G-body";
            if (p == "new_weapon.vfMesh") return "G-weapon";
            return {};
        };

        const int applied = windows::prefabref::applyRefEdits(prefab, {body, weapon}, resolver);
        CHECK(applied == 2);

        const json& bodyMesh = prefab["prefab"]["entity"]["components"]["mesh"];
        CHECK(bodyMesh["meshRefPath"].get<std::string>() == "new_body.vfMesh");
        CHECK(bodyMesh["meshRef"].get<std::string>() == "G-body");

        const json& weaponMesh = prefab["prefab"]["entity"]["children"][0]["components"]["mesh"];
        CHECK(weaponMesh["meshRefPath"].get<std::string>() == "new_weapon.vfMesh");
        CHECK(weaponMesh["meshRef"].get<std::string>() == "G-weapon");
        // Weapon animator (different family) untouched by the mesh swap.
        CHECK(weaponMesh["animatorRef"].get<std::string>() == "OLDGUID-weaponAnim");
    }

    TEST_CASE("applyRefEdits: swapping the animator on a part that had NO animator ref adds it")
    {
        // Body has a mesh ref but no animatorRef. An animator swap should create the animatorRef pair.
        json prefab = makeRefPrefabRich();

        windows::prefabref::PartRefEdit e;
        e.part = 0; // body
        e.animatorPath = "body_added.vfAnim";
        auto resolver = [](const std::string&) -> std::string { return "G-anim"; };

        CHECK(windows::prefabref::applyRefEdits(prefab, {e}, resolver) == 1);

        const json& bodyMesh = prefab["prefab"]["entity"]["components"]["mesh"];
        CHECK(bodyMesh["animatorRefPath"].get<std::string>() == "body_added.vfAnim");
        CHECK(bodyMesh["animatorRef"].get<std::string>() == "G-anim");
        // Existing mesh ref pair untouched.
        CHECK(bodyMesh["meshRef"].get<std::string>() == "OLDGUID-body");
    }

    TEST_CASE("applyRefEdits: a negative part index edit is skipped (applied count 0)")
    {
        json prefab = makeRefPrefabRich();
        const json before = prefab;

        windows::prefabref::PartRefEdit e;
        e.part = -1;
        e.meshPath = "ignored.vfMesh";
        CHECK(windows::prefabref::applyRefEdits(prefab, {e}, {}) == 0);
        CHECK(prefab == before); // nothing changed
    }

    TEST_CASE("applyRefEdits: an out-of-range part index finds no target (applied count 0)")
    {
        json prefab = makeRefPrefabRich();
        const json before = prefab;

        windows::prefabref::PartRefEdit e;
        e.part = 9; // only 2 mesh-bearing parts exist
        e.meshPath = "ignored.vfMesh";
        CHECK(windows::prefabref::applyRefEdits(prefab, {e}, {}) == 0);
        CHECK(prefab == before);
    }

    TEST_CASE("applyRefEdits: malformed root (no prefab/entity) returns 0 and changes nothing")
    {
        json noPrefab = {{"version", "1.0"}};
        windows::prefabref::PartRefEdit e;
        e.part = 0;
        e.meshPath = "x.vfMesh";
        CHECK(windows::prefabref::applyRefEdits(noPrefab, {e}, {}) == 0);

        json noEntity = {{"prefab", {{"name", "Rig"}}}};
        CHECK(windows::prefabref::applyRefEdits(noEntity, {e}, {}) == 0);
    }

    TEST_CASE("applyRefEdits: part index counts only mesh-bearing nodes, skipping a non-mesh intermediate")
    {
        // Body(mesh, part0) -> Pivot(NO mesh) -> Weapon(mesh, part1). A part==1 edit must land on
        // Weapon, proving non-mesh nodes do not consume a part index (matches buildPrefabRigDescDTO).
        json weapon = {
            {"name", "Weapon"},
            {"components", {{"mesh", {{"meshRef", "OLDGUID-weapon"}, {"meshRefPath", "old_weapon.vfMesh"}}}}},
            {"children", json::array()}
        };
        json pivot = {
            {"name", "Pivot"},
            {"components", {{"transform2", json::object()}}}, // no mesh component
            {"children", json::array({weapon})}
        };
        json body = {
            {"name", "Body"},
            {"components", {{"mesh", {{"meshRef", "OLDGUID-body"}, {"meshRefPath", "old_body.vfMesh"}}}}},
            {"children", json::array({pivot})}
        };
        json prefab = json{{"version", "1.0"}, {"prefab", {{"name", "Rig"}, {"entity", body}}}};

        windows::prefabref::PartRefEdit e;
        e.part = 1;
        e.meshPath = "new_weapon.vfMesh";
        CHECK(windows::prefabref::applyRefEdits(prefab, {e}, {}) == 1);

        const json& weaponMesh =
            prefab["prefab"]["entity"]["children"][0]["children"][0]["components"]["mesh"];
        CHECK(weaponMesh["meshRefPath"].get<std::string>() == "new_weapon.vfMesh");
        // Body (part 0) untouched.
        CHECK(prefab["prefab"]["entity"]["components"]["mesh"]["meshRefPath"].get<std::string>() ==
              "old_body.vfMesh");
    }
}

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

TEST_SUITE("PrefabRigPhase2Gaps.ZeroTranslation")
{
    json makeOnePart(const json& transform)
    {
        json body = {
            {"name", "Body"},
            {"components", {{"mesh", {{"meshRefPath", "body.vfMesh"}}}}},
            {"children", json::array()}
        };
        if (!transform.is_null())
            body["transform"] = transform;
        return json{{"version", "1.0"}, {"prefab", {{"name", "Rig"}, {"entity", body}}}};
    }

    TEST_CASE("zeroPartTranslation: the ROOT part (part 0) is zeroable")
    {
        json prefab = makeOnePart(json{
            {"position", {5.0f, 6.0f, 7.0f}}, {"rotation", {1.0f, 2.0f, 3.0f}}, {"scale", {2.0f, 2.0f, 2.0f}}});
        CHECK(windows::prefabtransform::zeroPartTranslation(prefab, 0));

        const json& t = prefab["prefab"]["entity"]["transform"];
        CHECK(t["position"][0].get<float>() == doctest::Approx(0.0f));
        CHECK(t["position"][1].get<float>() == doctest::Approx(0.0f));
        CHECK(t["position"][2].get<float>() == doctest::Approx(0.0f));
        // Rotation + scale preserved.
        CHECK(t["rotation"][1].get<float>() == doctest::Approx(2.0f));
        CHECK(t["scale"][0].get<float>() == doctest::Approx(2.0f));
    }

    TEST_CASE("zeroPartTranslation: already-zero position is idempotent (returns true, stays zero)")
    {
        json prefab = makeOnePart(json{
            {"position", {0.0f, 0.0f, 0.0f}}, {"rotation", {0.0f, 0.0f, 0.0f}}, {"scale", {1.0f, 1.0f, 1.0f}}});
        CHECK(windows::prefabtransform::zeroPartTranslation(prefab, 0));
        // Second application is a stable no-op on the data.
        json once = prefab;
        CHECK(windows::prefabtransform::zeroPartTranslation(prefab, 0));
        CHECK(prefab["prefab"]["entity"]["transform"]["position"] ==
              once["prefab"]["entity"]["transform"]["position"]);
        CHECK(prefab["prefab"]["entity"]["transform"]["position"][0].get<float>() == doctest::Approx(0.0f));
    }

    TEST_CASE("zeroPartTranslation: absent transform block materializes identity rotation/scale + zero position")
    {
        json prefab = makeOnePart(json()); // null -> no transform block written
        REQUIRE_FALSE(prefab["prefab"]["entity"].contains("transform"));

        CHECK(windows::prefabtransform::zeroPartTranslation(prefab, 0));
        const json& t = prefab["prefab"]["entity"]["transform"];
        // readLocalTransform defaulted to identity, then position zeroed -> pos 0, rot 0, scale 1.
        CHECK(t["position"][0].get<float>() == doctest::Approx(0.0f));
        CHECK(t["rotation"][0].get<float>() == doctest::Approx(0.0f));
        CHECK(t["scale"][0].get<float>() == doctest::Approx(1.0f));
        CHECK(t["scale"][1].get<float>() == doctest::Approx(1.0f));
        CHECK(t["scale"][2].get<float>() == doctest::Approx(1.0f));
    }

    TEST_CASE("zeroPartTranslation: malformed root returns false")
    {
        json noEntity = {{"prefab", {{"name", "Rig"}}}};
        CHECK_FALSE(windows::prefabtransform::zeroPartTranslation(noEntity, 0));
        json noPrefab = {{"version", "1.0"}};
        CHECK_FALSE(windows::prefabtransform::zeroPartTranslation(noPrefab, 0));
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

    TEST_CASE("snapshot: a transform-only snapshot engages only previewTransforms")
    {
        using namespace windows::prefabrigedit;
        PrefabRigEditSnapshot snap;
        std::map<int, glm::mat4> m;
        m[0] = glm::mat4(1.0f);            // identity entry (reset covers it on replay)
        m[2] = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 0.0f, 0.0f));
        snap.previewTransforms = m;

        CHECK(snap.previewTransforms.has_value());
        CHECK_FALSE(snap.socketPart.has_value());
        CHECK_FALSE(snap.chains.has_value());
        CHECK((*snap.previewTransforms).size() == 2);
        CHECK((*snap.previewTransforms)[0] == glm::mat4(1.0f));
    }
}
