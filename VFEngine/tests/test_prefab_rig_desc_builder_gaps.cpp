// VK-1433 Layer C — additional CPU coverage for the prefab-tree -> PrefabRigDescDTO conversion.
//
// Complements test_prefab_rig_desc_builder.cpp by closing the logic gaps the first file left:
// static-only vs mesh-less node handling, deeper / branching trees + forward-reference parent
// resolution, material plumbing onto the right part, full IK field fidelity (chain copy + the
// first-static-part default + a non-root body owner), multiple chains, duplicate-name "last
// wins", and the single-part empty/edge case. All header-only (no Editor link, no Graphics).

#include <doctest.h>
#include "windows/preview/PrefabRigDescBuilder.hpp"

using windows::PrefabEntityNode;
using windows::buildPrefabRigDescDTO;

namespace
{
    PrefabEntityNode skeletalNode(const std::string& name, const std::string& mesh,
                                  const std::string& animator)
    {
        PrefabEntityNode n;
        n.name = name;
        n.meshPath = mesh;
        n.animatorPath = animator;
        return n;
    }

    PrefabEntityNode staticNode(const std::string& name, const std::string& mesh)
    {
        PrefabEntityNode n;
        n.name = name;
        n.meshPath = mesh;
        return n;
    }

    PrefabEntityNode emptyNode(const std::string& name)
    {
        PrefabEntityNode n;
        n.name = name; // no meshPath -> not a part
        return n;
    }
}

TEST_SUITE("PrefabRigDescBuilder")
{
    // -----------------------------------------------------------------------
    // Part inclusion: a mesh node (static or skeletal) is a part; a mesh-LESS
    // node is excluded even when it sits between two mesh nodes.
    // -----------------------------------------------------------------------
    TEST_CASE("a static-only part has an empty animatorPath; mesh-less nodes are excluded")
    {
        // Root(Body, skeletal) -> Empty(no mesh) -> Prop(static, child of Empty).
        // The Empty node must NOT become a part, leaving exactly 2 parts.
        PrefabEntityNode root = skeletalNode("Body", "body.vfMesh", "body.vfAnimator");

        PrefabEntityNode empty = emptyNode("Pivot");
        empty.children.push_back(staticNode("Prop", "prop.vfMesh"));
        root.children.push_back(std::move(empty));

        auto desc = buildPrefabRigDescDTO(root);

        REQUIRE(desc.parts.size() == 2);
        // Depth-first part order: 0=Body (skeletal), 1=Prop (static). Pivot is skipped.
        CHECK(desc.parts[0].meshPath == "body.vfMesh");
        CHECK_FALSE(desc.parts[0].animatorPath.empty()); // skeletal
        CHECK(desc.parts[1].meshPath == "prop.vfMesh");
        CHECK(desc.parts[1].animatorPath.empty());       // static -> no animator
        CHECK(desc.parts[1].retargetPath.empty());
    }

    // -----------------------------------------------------------------------
    // A node with no mesh at all yields ZERO parts (and no crash).
    // -----------------------------------------------------------------------
    TEST_CASE("a tree of mesh-less nodes yields zero parts")
    {
        PrefabEntityNode root = emptyNode("Root");
        root.children.push_back(emptyNode("ChildA"));
        root.children.push_back(emptyNode("ChildB"));

        auto desc = buildPrefabRigDescDTO(root);

        CHECK(desc.parts.empty());
        CHECK(desc.ik.empty());
    }

    // -----------------------------------------------------------------------
    // Empty/edge: a single mesh node with no sockets and no IK builds one part
    // / zero ik without crashing, and stays a root.
    // -----------------------------------------------------------------------
    TEST_CASE("a single skeletal part with no sockets and no IK yields 1 part / 0 ik")
    {
        PrefabEntityNode root = skeletalNode("Solo", "solo.vfMesh", "solo.vfAnimator");

        auto desc = buildPrefabRigDescDTO(root);

        REQUIRE(desc.parts.size() == 1);
        CHECK(desc.ik.empty());
        CHECK(desc.parts[0].parentPartIndex == -1);
        CHECK(desc.parts[0].attachParentSocket.empty());
    }

    // -----------------------------------------------------------------------
    // Deeper / branching tree: child-of-child + a second branch, with correct
    // part indexing and topological parentPartIndex on every attached part.
    // -----------------------------------------------------------------------
    TEST_CASE("a branching 4-part tree resolves each part's parent index topologically")
    {
        // Body(0)
        //  ├─ Backpack(1, on "Spine")           -> child of 0
        //  └─ Weapon(2, on "Hand_R")            -> child of 0
        //        └─ Scope(3, on "Rail")         -> child of 2
        PrefabEntityNode root = skeletalNode("Body", "body.vfMesh", "body.vfAnimator");

        PrefabEntityNode backpack = staticNode("Backpack", "pack.vfMesh");
        backpack.hasSocketAttachment = true;
        backpack.attachParentEntityName = "Body";
        backpack.attachSocketName = "Spine";

        PrefabEntityNode weapon = staticNode("Weapon", "weapon.vfMesh");
        weapon.hasSocketAttachment = true;
        weapon.attachParentEntityName = "Body";
        weapon.attachSocketName = "Hand_R";

        PrefabEntityNode scope = staticNode("Scope", "scope.vfMesh");
        scope.hasSocketAttachment = true;
        scope.attachParentEntityName = "Weapon";
        scope.attachSocketName = "Rail";
        weapon.children.push_back(scope);

        root.children.push_back(backpack);
        root.children.push_back(weapon);

        auto desc = buildPrefabRigDescDTO(root);

        REQUIRE(desc.parts.size() == 4);
        // Depth-first order: 0=Body, 1=Backpack, 2=Weapon, 3=Scope.
        CHECK(desc.parts[0].parentPartIndex == -1);                 // Body root
        CHECK(desc.parts[1].parentPartIndex == 0);                  // Backpack -> Body
        CHECK(desc.parts[1].attachParentSocket == "Spine");
        CHECK(desc.parts[2].parentPartIndex == 0);                  // Weapon -> Body
        CHECK(desc.parts[2].attachParentSocket == "Hand_R");
        CHECK(desc.parts[3].parentPartIndex == 2);                  // Scope -> Weapon (child of child)
        CHECK(desc.parts[3].attachParentSocket == "Rail");
        CHECK(desc.parts[3].parentPartIndex > desc.parts[2].parentPartIndex); // strictly deeper
    }

    // -----------------------------------------------------------------------
    // Forward reference: a child whose attachParentEntityName names a part that
    // appears LATER in the depth-first flatten still resolves (nameToPart is
    // fully built in pass 1 before the attachment-resolution pass).
    // -----------------------------------------------------------------------
    TEST_CASE("socket attachment resolves a parent that is flattened after the child")
    {
        // Root(Empty) with two mesh children: Weapon listed FIRST, Body listed SECOND.
        // Weapon attaches to "Body" — a name that becomes part index 1 (after Weapon=0).
        PrefabEntityNode root = emptyNode("Root");

        PrefabEntityNode weapon = staticNode("Weapon", "weapon.vfMesh");
        weapon.hasSocketAttachment = true;
        weapon.attachParentEntityName = "Body"; // forward reference (Body flattened later)
        weapon.attachSocketName = "Hand_R";

        root.children.push_back(weapon);
        root.children.push_back(skeletalNode("Body", "body.vfMesh", "body.vfAnimator"));

        auto desc = buildPrefabRigDescDTO(root);

        REQUIRE(desc.parts.size() == 2);
        // Depth-first: 0=Weapon, 1=Body. Weapon's parent resolves to Body=1 despite ordering.
        CHECK(desc.parts[0].meshPath == "weapon.vfMesh");
        CHECK(desc.parts[0].parentPartIndex == 1);
        CHECK(desc.parts[0].attachParentSocket == "Hand_R");
        CHECK(desc.parts[1].parentPartIndex == -1); // Body is the root part
    }

    // -----------------------------------------------------------------------
    // Material plumbing: defaultMaterialPath + subMeshMaterials are carried onto
    // the owning part (and only it).
    // -----------------------------------------------------------------------
    TEST_CASE("material references are copied onto the owning part")
    {
        PrefabEntityNode root = skeletalNode("Body", "body.vfMesh", "body.vfAnimator");
        root.defaultMaterialPath = "body.vfMatInstance";

        PrefabEntityNode weapon = staticNode("Weapon", "weapon.vfMesh");
        weapon.hasSocketAttachment = true;
        weapon.attachParentEntityName = "Body";
        weapon.attachSocketName = "Hand_R";
        weapon.defaultMaterialPath = "weapon.vfMat";
        weapon.subMeshMaterials["Barrel"] = "barrel.vfMat";
        weapon.subMeshMaterials["Grip"]   = "grip.vfMat";
        root.children.push_back(weapon);

        auto desc = buildPrefabRigDescDTO(root);

        REQUIRE(desc.parts.size() == 2);
        // Part 0 (Body): its own default, no submesh overrides.
        CHECK(desc.parts[0].defaultMaterialPath == "body.vfMatInstance");
        CHECK(desc.parts[0].subMeshMaterials.empty());
        // Part 1 (Weapon): its own default + both submesh overrides, nothing leaked from Body.
        CHECK(desc.parts[1].defaultMaterialPath == "weapon.vfMat");
        REQUIRE(desc.parts[1].subMeshMaterials.size() == 2);
        CHECK(desc.parts[1].subMeshMaterials.at("Barrel") == "barrel.vfMat");
        CHECK(desc.parts[1].subMeshMaterials.at("Grip") == "grip.vfMat");
    }

    // -----------------------------------------------------------------------
    // IK field fidelity: every IKChainConfig field is copied verbatim, and the
    // default target binding picks the FIRST static part (not the second).
    // -----------------------------------------------------------------------
    TEST_CASE("an IK chain copies all chain fields and defaults to the first static part")
    {
        // Body(0, skeletal, owns the chain), Weapon(1, static), Shield(2, static).
        // The default targetPartIndex must be the FIRST static part encountered (1).
        PrefabEntityNode root = skeletalNode("Body", "body.vfMesh", "body.vfAnimator");
        root.children.push_back(staticNode("Weapon", "weapon.vfMesh"));
        root.children.push_back(staticNode("Shield", "shield.vfMesh"));

        animator::ik::IKChainConfig chain;
        chain.chainName = "RightArmIK";
        chain.tipBoneName = "RightHand";
        chain.chainBoneNames = { "RightShoulder", "RightElbow" };
        chain.weight = 0.6f;
        chain.enabled = false; // ensure the bool is carried verbatim (not forced true)
        chain.constraints.resize(2);
        chain.constraints[0].type = animator::ik::JointConstraintType::Hinge;
        chain.constraints[1].type = animator::ik::JointConstraintType::Cone;
        root.ikChains.push_back(chain);

        auto desc = buildPrefabRigDescDTO(root);

        REQUIRE(desc.ik.size() == 1);
        const auto& ik = desc.ik[0];
        CHECK(ik.chain.chainName == "RightArmIK");
        CHECK(ik.chain.tipBoneName == "RightHand");
        REQUIRE(ik.chain.chainBoneNames.size() == 2);
        CHECK(ik.chain.chainBoneNames[0] == "RightShoulder");
        CHECK(ik.chain.chainBoneNames[1] == "RightElbow");
        CHECK(ik.chain.weight == doctest::Approx(0.6f));
        CHECK(ik.chain.enabled == false);
        REQUIRE(ik.chain.constraints.size() == 2);
        CHECK(ik.chain.constraints[0].type == animator::ik::JointConstraintType::Hinge);
        CHECK(ik.chain.constraints[1].type == animator::ik::JointConstraintType::Cone);

        CHECK(ik.bodyPartIndex == 0);       // owning node's part
        CHECK(ik.targetPartIndex == 1);     // FIRST static part (Weapon), not Shield
        CHECK(ik.targetSocketName.empty()); // left for the user to pick
    }

    // -----------------------------------------------------------------------
    // bodyPartIndex tracks a NON-root owner; default target skips the body and
    // finds a static part even when the body is not part 0.
    // -----------------------------------------------------------------------
    TEST_CASE("an IK chain owned by a non-root part records that part as the body")
    {
        // Root(Empty) -> Arms(0, skeletal, owns chain) + Gun(1, static).
        PrefabEntityNode root = emptyNode("Root");

        PrefabEntityNode arms = skeletalNode("Arms", "arms.vfMesh", "arms.vfAnimator");
        animator::ik::IKChainConfig chain;
        chain.chainName = "HandIK";
        chain.weight = 1.0f;
        arms.ikChains.push_back(chain);

        root.children.push_back(arms);
        root.children.push_back(staticNode("Gun", "gun.vfMesh"));

        auto desc = buildPrefabRigDescDTO(root);

        REQUIRE(desc.parts.size() == 2);
        REQUIRE(desc.ik.size() == 1);
        CHECK(desc.ik[0].bodyPartIndex == 0);   // Arms is part 0 (Root is mesh-less, not a part)
        CHECK(desc.ik[0].targetPartIndex == 1); // Gun (the only static part)
    }

    // -----------------------------------------------------------------------
    // No static part to default to: targetPartIndex stays -1 (the window must
    // let the user bind it). Body is the only part.
    // -----------------------------------------------------------------------
    TEST_CASE("an IK chain with no static part leaves the default target unbound")
    {
        PrefabEntityNode root = skeletalNode("Body", "body.vfMesh", "body.vfAnimator");

        animator::ik::IKChainConfig chain;
        chain.chainName = "LegIK";
        root.ikChains.push_back(chain);

        auto desc = buildPrefabRigDescDTO(root);

        REQUIRE(desc.ik.size() == 1);
        CHECK(desc.ik[0].bodyPartIndex == 0);
        CHECK(desc.ik[0].targetPartIndex == -1); // no static part -> unbound
    }

    // -----------------------------------------------------------------------
    // Multiple chains on one node: each becomes its own ik[] entry, all owned
    // by the same body part, each defaulting to the first static part.
    // -----------------------------------------------------------------------
    TEST_CASE("multiple IK chains on one node each become a separate ik entry")
    {
        PrefabEntityNode root = skeletalNode("Body", "body.vfMesh", "body.vfAnimator");
        root.children.push_back(staticNode("Weapon", "weapon.vfMesh"));

        animator::ik::IKChainConfig left;  left.chainName = "LeftHandIK";  left.weight = 0.5f;
        animator::ik::IKChainConfig right; right.chainName = "RightHandIK"; right.weight = 1.0f;
        root.ikChains.push_back(left);
        root.ikChains.push_back(right);

        auto desc = buildPrefabRigDescDTO(root);

        REQUIRE(desc.ik.size() == 2);
        CHECK(desc.ik[0].chain.chainName == "LeftHandIK");
        CHECK(desc.ik[0].chain.weight == doctest::Approx(0.5f));
        CHECK(desc.ik[1].chain.chainName == "RightHandIK");
        CHECK(desc.ik[1].chain.weight == doctest::Approx(1.0f));
        // Both owned by Body (part 0), both default to the static Weapon (part 1).
        for (const auto& ik : desc.ik)
        {
            CHECK(ik.bodyPartIndex == 0);
            CHECK(ik.targetPartIndex == 1);
        }
    }

    // -----------------------------------------------------------------------
    // Duplicate names: the documented "last node wins" tie-break for nameToPart
    // resolution (PrefabRigDescBuilder.hpp:90).
    // -----------------------------------------------------------------------
    TEST_CASE("on duplicate part names the last node wins for parent resolution")
    {
        // Two parts both named "Hand"; a weapon attaches to "Hand". The builder maps the
        // name to the LAST part with that name (part 1), per the documented tie-break.
        PrefabEntityNode root = emptyNode("Root");
        root.children.push_back(skeletalNode("Hand", "hand0.vfMesh", "hand.vfAnimator")); // part 0
        root.children.push_back(skeletalNode("Hand", "hand1.vfMesh", "hand.vfAnimator")); // part 1

        PrefabEntityNode weapon = staticNode("Weapon", "weapon.vfMesh");
        weapon.hasSocketAttachment = true;
        weapon.attachParentEntityName = "Hand";
        weapon.attachSocketName = "Grip";
        root.children.push_back(weapon);

        auto desc = buildPrefabRigDescDTO(root);

        REQUIRE(desc.parts.size() == 3);
        // Weapon (part 2) resolves "Hand" to the LAST matching part (1), not the first (0).
        CHECK(desc.parts[2].parentPartIndex == 1);
        CHECK(desc.parts[2].attachParentSocket == "Grip");
    }

    // -----------------------------------------------------------------------
    // Retarget path is carried onto the part (skeletal part with a .vfretarget).
    // -----------------------------------------------------------------------
    TEST_CASE("a retarget path is carried onto its skeletal part")
    {
        PrefabEntityNode root = skeletalNode("Body", "body.vfMesh", "body.vfAnimator");
        root.retargetPath = "humanoid.vfretarget";

        auto desc = buildPrefabRigDescDTO(root);

        REQUIRE(desc.parts.size() == 1);
        CHECK(desc.parts[0].retargetPath == "humanoid.vfretarget");
    }
}
