#include <doctest.h>
#include <scene/SceneGraphSystem.hpp>
#include <scene/Entity.hpp>
#include <serialization/PrefabSerialization.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace
{
    scene::Entity makeChild(scene::SceneGraphSystem& sceneGraph, scene::Entity& parent, const std::string& name)
    {
        scene::Entity entity(name);
        sceneGraph.addChild(parent, entity);
        return entity;
    }

    std::vector<std::string> childNames(scene::Entity& parent)
    {
        std::vector<std::string> names;
        for (auto& child : parent.getChildren())
        {
            names.push_back(child.getName());
        }
        return names;
    }
}

TEST_SUITE("SceneHierarchyReorder")
{
    TEST_CASE("moveEntity inserts at index within same parent")
    {
        scene::SceneGraphSystem sceneGraph;
        auto& root = sceneGraph.GetRoot();
        auto a = makeChild(sceneGraph, root, "A");
        auto b = makeChild(sceneGraph, root, "B");
        auto c = makeChild(sceneGraph, root, "C");

        SUBCASE("move last to front")
        {
            CHECK(sceneGraph.moveEntity(c, root, 0));
            CHECK(childNames(root) == std::vector<std::string>{"C", "A", "B"});
        }

        SUBCASE("move first past its own slot compensates for removal shift")
        {
            // Dropping A in the gap between B and C (insertIndex 2 in the pre-move list)
            CHECK(sceneGraph.moveEntity(a, root, 2));
            CHECK(childNames(root) == std::vector<std::string>{"B", "A", "C"});
        }

        SUBCASE("same slot is a no-op order-wise")
        {
            CHECK(sceneGraph.moveEntity(b, root, 1));
            CHECK(childNames(root) == std::vector<std::string>{"A", "B", "C"});
        }

        sceneGraph.clearScene();
    }

    TEST_CASE("moveEntity appends with -1 and clamps out-of-range index")
    {
        scene::SceneGraphSystem sceneGraph;
        auto& root = sceneGraph.GetRoot();
        auto a = makeChild(sceneGraph, root, "A");
        auto b = makeChild(sceneGraph, root, "B");
        auto c = makeChild(sceneGraph, root, "C");

        CHECK(sceneGraph.moveEntity(a, root, -1));
        CHECK(childNames(root) == std::vector<std::string>{"B", "C", "A"});

        CHECK(sceneGraph.moveEntity(b, root, 99));
        CHECK(childNames(root) == std::vector<std::string>{"C", "A", "B"});

        sceneGraph.clearScene();
    }

    TEST_CASE("moveEntity reparents into target at index")
    {
        scene::SceneGraphSystem sceneGraph;
        auto& root = sceneGraph.GetRoot();
        auto parentA = makeChild(sceneGraph, root, "ParentA");
        auto parentB = makeChild(sceneGraph, root, "ParentB");
        auto x = makeChild(sceneGraph, parentA, "X");
        auto b1 = makeChild(sceneGraph, parentB, "B1");
        auto b2 = makeChild(sceneGraph, parentB, "B2");

        CHECK(sceneGraph.moveEntity(x, parentB, 1));
        CHECK(childNames(parentA).empty());
        CHECK(childNames(parentB) == std::vector<std::string>{"B1", "X", "B2"});
        CHECK(x.getParent() == parentB);

        sceneGraph.clearScene();
    }

    TEST_CASE("moveEntity rejects cycles and self-parenting")
    {
        scene::SceneGraphSystem sceneGraph;
        auto& root = sceneGraph.GetRoot();
        auto parent = makeChild(sceneGraph, root, "Parent");
        auto child = makeChild(sceneGraph, parent, "Child");
        auto grandchild = makeChild(sceneGraph, child, "Grandchild");

        CHECK_FALSE(sceneGraph.moveEntity(parent, grandchild, 0));
        CHECK_FALSE(sceneGraph.moveEntity(parent, parent, 0));

        // Graph unchanged
        CHECK(childNames(root) == std::vector<std::string>{"Parent"});
        CHECK(childNames(parent) == std::vector<std::string>{"Child"});
        CHECK(childNames(child) == std::vector<std::string>{"Grandchild"});

        sceneGraph.clearScene();
    }

    TEST_CASE("entity tree JSON round-trip preserves child order and regenerates UUIDs")
    {
        scene::SceneGraphSystem sceneGraph;
        auto& root = sceneGraph.GetRoot();
        auto source = makeChild(sceneGraph, root, "Source");
        auto c = makeChild(sceneGraph, source, "C");
        auto a = makeChild(sceneGraph, source, "A");
        auto b = makeChild(sceneGraph, source, "B");

        // Reorder so persisted order differs from creation order
        CHECK(sceneGraph.moveEntity(b, source, 0));
        CHECK(childNames(source) == std::vector<std::string>{"B", "C", "A"});

        auto treeJson = serialization::PrefabSerialization::serializeEntityTree(source);
        REQUIRE(treeJson.contains("children"));
        REQUIRE(treeJson["children"].size() == 3);
        CHECK(treeJson["children"][0]["name"].get<std::string>() == "B");
        CHECK(treeJson["children"][1]["name"].get<std::string>() == "C");
        CHECK(treeJson["children"][2]["name"].get<std::string>() == "A");

        auto target = makeChild(sceneGraph, root, "Target");
        auto instantiated = serialization::PrefabSerialization::deserializeEntityTree(treeJson, target, sceneGraph);
        REQUIRE(instantiated.isValid());
        CHECK(instantiated.getName() == "Source");
        CHECK(childNames(instantiated) == std::vector<std::string>{"B", "C", "A"});

        // Fresh entity, fresh identity — not the original
        CHECK_FALSE(instantiated == source);
        CHECK(instantiated.getUUID() != source.getUUID());

        sceneGraph.clearScene();
    }
}
