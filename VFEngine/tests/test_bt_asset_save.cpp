#include <doctest.h>
#include <behaviortree/BehaviorTreeAsset.hpp>
#include <behaviortree/BehaviorTreeTypes.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

// ============================================================
// VK-1660: BehaviorTreeAsset::serialize / parse and the atomic save
// ============================================================

namespace
{
    namespace fs = std::filesystem;

    fs::path btSaveTestRoot()
    {
        return fs::temp_directory_path() / "vf_bt_asset_save_tests";
    }

    void resetBtSaveTestRoot()
    {
        std::error_code ec;
        fs::remove_all(btSaveTestRoot(), ec);
        fs::create_directories(btSaveTestRoot(), ec);
    }

    std::string readAll(const fs::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        std::ostringstream out;
        out << file.rdbuf();
        return out.str();
    }

    behaviortree::BehaviorTreeData makeTree(float waitSeconds)
    {
        behaviortree::BehaviorTreeData data = behaviortree::BehaviorTreeAsset::createDefault("SaveTest");
        behaviortree::BTNode wait;
        wait.id = data.graph.nextNodeId++;
        wait.type = behaviortree::BTNodeType::Wait;
        wait.name = "Wait";
        wait.properties["duration"] = waitSeconds;
        data.graph.nodes.push_back(wait);

        behaviortree::BTLink link;
        link.id = data.graph.nextLinkId++;
        link.sourceNodeId = 2; // createDefault: Root(1) -> Sequence(2)
        link.targetNodeId = wait.id;
        data.graph.links.push_back(link);

        behaviortree::BlackboardKeyDef key;
        key.name = "chaseRange";
        key.type = behaviortree::BlackboardValueType::Float;
        key.defaultValue = 10.0f;
        data.graph.blackboardKeys.push_back(key);
        return data;
    }
}

TEST_SUITE("BTAssetSave")
{
    TEST_CASE("save writes exactly serialize() and leaves no temp file")
    {
        resetBtSaveTestRoot();
        const auto data = makeTree(2.5f);
        const fs::path path = btSaveTestRoot() / "nested" / "dir" / "Enemy.vfBehaviorTree";

        REQUIRE(behaviortree::BehaviorTreeAsset::save(path.string(), data)); // creates the folders
        CHECK(fs::exists(path));
        CHECK_FALSE(fs::exists(fs::path(path.string() + ".tmp")));
        CHECK(readAll(path) == behaviortree::BehaviorTreeAsset::serialize(data));
    }

    TEST_CASE("save replaces an existing file")
    {
        resetBtSaveTestRoot();
        const fs::path path = btSaveTestRoot() / "Replace.vfBehaviorTree";
        REQUIRE(behaviortree::BehaviorTreeAsset::save(path.string(), makeTree(1.0f)));
        REQUIRE(behaviortree::BehaviorTreeAsset::save(path.string(), makeTree(4.0f)));

        auto loaded = behaviortree::BehaviorTreeAsset::load(path.string());
        REQUIRE(loaded.has_value());
        const behaviortree::BTNode* wait = nullptr;
        for (const auto& node : loaded->graph.nodes)
        {
            if (node.type == behaviortree::BTNodeType::Wait) wait = &node;
        }
        REQUIRE(wait != nullptr);
        CHECK(behaviortree::getNodeProperty<float>(*wait, "duration", 0.0f) == doctest::Approx(4.0f));
        CHECK_FALSE(fs::exists(fs::path(path.string() + ".tmp")));
    }

    TEST_CASE("parse(serialize(tree)) reproduces the tree")
    {
        const auto data = makeTree(3.0f);
        const std::string text = behaviortree::BehaviorTreeAsset::serialize(data);
        auto parsed = behaviortree::BehaviorTreeAsset::parse(text);
        REQUIRE(parsed.has_value());
        CHECK(parsed->name == data.name);
        CHECK(parsed->graph.rootNodeId == data.graph.rootNodeId);
        CHECK(parsed->graph.nodes.size() == data.graph.nodes.size());
        CHECK(parsed->graph.links.size() == data.graph.links.size());
        REQUIRE(parsed->graph.blackboardKeys.size() == 1);
        CHECK(parsed->graph.blackboardKeys[0].name == "chaseRange");
        // Re-serializing is byte-identical, so a content hash of the text is stable across a round trip.
        CHECK(behaviortree::BehaviorTreeAsset::serialize(*parsed) == text);
    }

    TEST_CASE("parse refuses invalid JSON and non-object roots")
    {
        CHECK_FALSE(behaviortree::BehaviorTreeAsset::parse("{ not json").has_value());
        CHECK_FALSE(behaviortree::BehaviorTreeAsset::parse("[1, 2, 3]").has_value());
        CHECK_FALSE(behaviortree::BehaviorTreeAsset::parse("").has_value());
    }
}
