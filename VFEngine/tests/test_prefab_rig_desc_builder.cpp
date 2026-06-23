// VK-1433 Layer C — CPU tests for the prefab-tree -> PrefabRigDescDTO conversion.
//
// buildPrefabRigDescDTO() is header-only (windows/preview/PrefabRigDescBuilder.hpp) precisely so
// it can be exercised here without linking the Editor: no imgui, no JSON, no Graphics. We build
// PrefabEntityNode trees by hand and assert the flattening / parent-resolution / IK-defaulting.

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
}

TEST_SUITE("PrefabRigDescBuilder")
{
    TEST_CASE("a mesh-less root with one mesh child yields exactly one part")
    {
        PrefabEntityNode root;
        root.name = "Root"; // no mesh -> not a part
        root.children.push_back(skeletalNode("Body", "body.vfMesh", "body.vfAnimator"));

        auto desc = buildPrefabRigDescDTO(root);

        REQUIRE(desc.parts.size() == 1);
        CHECK(desc.parts[0].meshPath == "body.vfMesh");
        CHECK(desc.parts[0].animatorPath == "body.vfAnimator");
        CHECK(desc.parts[0].parentPartIndex == -1); // no socket attachment -> root part
    }

    TEST_CASE("a socket-attached weapon resolves its parent part + child transform")
    {
        PrefabEntityNode root = skeletalNode("Body", "body.vfMesh", "body.vfAnimator");

        PrefabEntityNode weapon = staticNode("Weapon", "weapon.vfMesh");
        weapon.hasSocketAttachment = true;
        weapon.attachParentEntityName = "Body";
        weapon.attachSocketName = "Weapon_R";
        weapon.rotation = glm::vec3(10.0f, 20.0f, 30.0f);
        weapon.scale = glm::vec3(2.0f);
        root.children.push_back(weapon);

        auto desc = buildPrefabRigDescDTO(root);

        REQUIRE(desc.parts.size() == 2);
        // part 0 == Body (root), part 1 == Weapon (child of part 0 via Weapon_R).
        CHECK(desc.parts[0].animatorPath == "body.vfAnimator");
        CHECK(desc.parts[1].animatorPath.empty()); // static
        CHECK(desc.parts[1].parentPartIndex == 0);
        CHECK(desc.parts[1].attachParentSocket == "Weapon_R");
        CHECK(desc.parts[1].attachChildRotation.y == doctest::Approx(20.0f));
        CHECK(desc.parts[1].attachChildScale.x == doctest::Approx(2.0f));
    }

    TEST_CASE("an attachment to a parent with no mesh part leaves the child as a root")
    {
        PrefabEntityNode root;
        root.name = "Empty"; // no mesh

        PrefabEntityNode weapon = staticNode("Weapon", "weapon.vfMesh");
        weapon.hasSocketAttachment = true;
        weapon.attachParentEntityName = "Empty"; // parent is not a part
        weapon.attachSocketName = "Grip";
        root.children.push_back(weapon);

        auto desc = buildPrefabRigDescDTO(root);

        REQUIRE(desc.parts.size() == 1);
        CHECK(desc.parts[0].parentPartIndex == -1); // unresolved -> root
        CHECK(desc.parts[0].attachParentSocket.empty());
    }

    TEST_CASE("an IK chain is owned by its node's part and defaults to the first static part")
    {
        PrefabEntityNode root = skeletalNode("Body", "body.vfMesh", "body.vfAnimator");
        root.children.push_back(staticNode("Weapon", "weapon.vfMesh"));

        animator::ik::IKChainConfig chain;
        chain.chainName = "LeftHandIK";
        chain.tipBoneName = "LeftHand";
        chain.weight = 0.75f;
        root.ikChains.push_back(chain); // body owns the chain

        auto desc = buildPrefabRigDescDTO(root);

        REQUIRE(desc.ik.size() == 1);
        CHECK(desc.ik[0].chain.chainName == "LeftHandIK");
        CHECK(desc.ik[0].chain.weight == doctest::Approx(0.75f));
        CHECK(desc.ik[0].bodyPartIndex == 0);       // the Body part
        CHECK(desc.ik[0].targetPartIndex == 1);     // defaulted to the static Weapon part
        CHECK(desc.ik[0].targetSocketName.empty()); // socket left for the user to pick
    }

    TEST_CASE("topological flattening preserves parents-before-children part order")
    {
        // Root(Body) -> Weapon(static) -> Scope(static, attached to Weapon).
        PrefabEntityNode root = skeletalNode("Body", "body.vfMesh", "body.vfAnimator");

        PrefabEntityNode weapon = staticNode("Weapon", "weapon.vfMesh");
        weapon.hasSocketAttachment = true;
        weapon.attachParentEntityName = "Body";
        weapon.attachSocketName = "Weapon_R";

        PrefabEntityNode scope = staticNode("Scope", "scope.vfMesh");
        scope.hasSocketAttachment = true;
        scope.attachParentEntityName = "Weapon";
        scope.attachSocketName = "ScopeMount";
        weapon.children.push_back(scope);

        root.children.push_back(weapon);

        auto desc = buildPrefabRigDescDTO(root);

        REQUIRE(desc.parts.size() == 3);
        // Depth-first: 0=Body, 1=Weapon, 2=Scope. Each parent index is strictly less than its child.
        CHECK(desc.parts[1].parentPartIndex == 0);
        CHECK(desc.parts[2].parentPartIndex == 1);
        CHECK(desc.parts[2].parentPartIndex < 2);
    }
}
