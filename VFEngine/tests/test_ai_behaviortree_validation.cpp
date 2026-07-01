#include <doctest.h>
#include <behaviortree/BehaviorTreeAsset.hpp>
#include <behaviortree/BehaviorTreeValidation.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>

namespace
{
    namespace bt = behaviortree;
    namespace validation = behaviortree::validation;
    namespace fs = std::filesystem;

    bool hasDiagnostic(const validation::ValidationReport& report,
                       validation::Severity severity,
                       const std::string& text)
    {
        for (const auto& diagnostic : report.diagnostics)
        {
            if (diagnostic.severity == severity &&
                diagnostic.message.find(text) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    bt::BTNode makeNode(uint32_t id, bt::BTNodeType type)
    {
        bt::BTNode node;
        node.id = id;
        node.type = type;
        node.name = bt::nodeTypeToString(type);
        return node;
    }

    void linkNodes(bt::BTGraph& graph, uint32_t source, uint32_t target)
    {
        bt::BTLink link;
        link.id = graph.nextLinkId++;
        link.sourceNodeId = source;
        link.targetNodeId = target;
        link.sortOrder = 0;
        graph.links.push_back(link);
    }

    void finalizeIds(bt::BTGraph& graph)
    {
        for (const auto& node : graph.nodes)
        {
            graph.nextNodeId = std::max(graph.nextNodeId, node.id + 1);
        }
    }

    bt::BehaviorTreeData makeValidTree()
    {
        bt::BehaviorTreeData data;
        data.name = "Valid";
        data.graph.rootNodeId = 1;
        data.graph.nodes.push_back(makeNode(1, bt::BTNodeType::Root));
        data.graph.nodes.push_back(makeNode(2, bt::BTNodeType::Sequence));
        data.graph.nodes.push_back(makeNode(3, bt::BTNodeType::Wait));
        data.graph.nodes.back().properties["duration"] = 0.1f;
        linkNodes(data.graph, 1, 2);
        linkNodes(data.graph, 2, 3);
        data.graph.blackboardKeys.push_back({"ok", bt::BlackboardValueType::Bool, true});
        finalizeIds(data.graph);
        return data;
    }

    fs::path uniqueTempPath(const std::string& stem)
    {
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        fs::path dir = fs::temp_directory_path() / ("vf_bt_validation_" + std::to_string(tick));
        fs::create_directories(dir);
        return dir / (stem + ".vfBehaviorTree");
    }
}

TEST_SUITE("AIBehaviorTreeValidation")
{
    TEST_CASE("valid tree has no validation diagnostics")
    {
        const auto report = validation::validateBehaviorTree(makeValidTree());
        CHECK(report.ok());
        CHECK(report.diagnostics.empty());
    }

    TEST_CASE("structural validation catches ids roots links cycles and parents")
    {
        SUBCASE("duplicate node id")
        {
            auto data = makeValidTree();
            data.graph.nodes.push_back(makeNode(2, bt::BTNodeType::Wait));

            const auto report = validation::validateBehaviorTree(data);
            CHECK(hasDiagnostic(report, validation::Severity::Error, "Duplicate node id"));
        }

        SUBCASE("missing root id")
        {
            auto data = makeValidTree();
            data.graph.rootNodeId = 0;

            const auto report = validation::validateBehaviorTree(data);
            CHECK(hasDiagnostic(report, validation::Severity::Error, "no rootNodeId"));
        }

        SUBCASE("dangling root id")
        {
            auto data = makeValidTree();
            data.graph.rootNodeId = 99;

            const auto report = validation::validateBehaviorTree(data);
            CHECK(hasDiagnostic(report, validation::Severity::Error, "does not exist"));
        }

        SUBCASE("invalid link endpoint and self edge")
        {
            auto data = makeValidTree();
            bt::BTLink invalid;
            invalid.id = data.graph.nextLinkId++;
            invalid.sourceNodeId = 2;
            invalid.targetNodeId = 99;
            data.graph.links.push_back(invalid);
            bt::BTLink self;
            self.id = data.graph.nextLinkId++;
            self.sourceNodeId = 3;
            self.targetNodeId = 3;
            data.graph.links.push_back(self);

            const auto report = validation::validateBehaviorTree(data);
            CHECK(hasDiagnostic(report, validation::Severity::Error, "missing target node"));
            CHECK(hasDiagnostic(report, validation::Severity::Error, "links to itself"));
        }

        SUBCASE("cycle")
        {
            auto data = makeValidTree();
            linkNodes(data.graph, 3, 2);

            const auto report = validation::validateBehaviorTree(data);
            CHECK(hasDiagnostic(report, validation::Severity::Error, "cycle"));
        }

        SUBCASE("multi parent")
        {
            auto data = makeValidTree();
            data.graph.nodes.push_back(makeNode(4, bt::BTNodeType::Sequence));
            linkNodes(data.graph, 1, 4);
            linkNodes(data.graph, 4, 3);

            const auto report = validation::validateBehaviorTree(data);
            CHECK(hasDiagnostic(report, validation::Severity::Error, "multiple incoming links"));
        }
    }

    TEST_CASE("child count validation separates errors from warnings")
    {
        SUBCASE("empty root")
        {
            bt::BehaviorTreeData data;
            data.graph.rootNodeId = 1;
            data.graph.nodes.push_back(makeNode(1, bt::BTNodeType::Root));

            const auto report = validation::validateBehaviorTree(data);
            CHECK(hasDiagnostic(report, validation::Severity::Error, "Root node has no child"));
        }

        SUBCASE("empty composite")
        {
            auto data = makeValidTree();
            data.graph.links.erase(data.graph.links.begin() + 1);

            const auto report = validation::validateBehaviorTree(data);
            CHECK(hasDiagnostic(report, validation::Severity::Error, "Composite node has no children"));
        }

        SUBCASE("decorator cardinality")
        {
            bt::BehaviorTreeData data;
            data.graph.rootNodeId = 1;
            data.graph.nodes.push_back(makeNode(1, bt::BTNodeType::Root));
            data.graph.nodes.push_back(makeNode(2, bt::BTNodeType::Inverter));
            data.graph.nodes.push_back(makeNode(3, bt::BTNodeType::Wait));
            data.graph.nodes.push_back(makeNode(4, bt::BTNodeType::Wait));
            linkNodes(data.graph, 1, 2);
            linkNodes(data.graph, 2, 3);
            linkNodes(data.graph, 2, 4);

            const auto report = validation::validateBehaviorTree(data);
            CHECK(hasDiagnostic(report, validation::Severity::Warning, "Decorator node has more than one child"));
        }

        SUBCASE("task with child")
        {
            auto data = makeValidTree();
            linkNodes(data.graph, 3, 2);

            const auto report = validation::validateBehaviorTree(data);
            CHECK(hasDiagnostic(report, validation::Severity::Warning, "Task node has children"));
        }
    }

    TEST_CASE("blackboard validation catches references and type problems")
    {
        SUBCASE("missing key reference")
        {
            auto data = makeValidTree();
            auto node = makeNode(4, bt::BTNodeType::CheckBlackboardValue);
            node.properties["key"] = std::string("missing");
            node.properties["compareOp"] = std::string("==");
            node.properties["compareValue"] = true;
            data.graph.nodes.push_back(node);

            const auto report = validation::validateBehaviorTree(data);
            CHECK(hasDiagnostic(report, validation::Severity::Warning, "not declared"));
        }

        SUBCASE("default and compare value type mismatch")
        {
            auto data = makeValidTree();
            data.graph.blackboardKeys.push_back({"badDefault", bt::BlackboardValueType::Int, true});
            auto node = makeNode(4, bt::BTNodeType::BlackboardCondition);
            node.properties["key"] = std::string("ok");
            node.properties["compareOp"] = std::string("==");
            node.properties["compareValue"] = 1.0f;
            data.graph.nodes.push_back(node);

            const auto report = validation::validateBehaviorTree(data);
            CHECK(hasDiagnostic(report, validation::Severity::Warning, "Default value"));
            CHECK(hasDiagnostic(report, validation::Severity::Warning, "does not match declared type"));
        }

        SUBCASE("invalid compare operators")
        {
            auto data = makeValidTree();
            auto node = makeNode(4, bt::BTNodeType::CheckBlackboardValue);
            node.properties["key"] = std::string("ok");
            node.properties["compareOp"] = std::string(">");
            node.properties["compareValue"] = true;
            data.graph.nodes.push_back(node);

            auto report = validation::validateBehaviorTree(data);
            CHECK(hasDiagnostic(report, validation::Severity::Warning, "numeric but key"));

            data.graph.nodes.back().properties["compareOp"] = std::string("Approximately");
            report = validation::validateBehaviorTree(data);
            CHECK(hasDiagnostic(report, validation::Severity::Warning, "unsupported"));
        }
    }

    TEST_CASE("string property enum validation catches unsupported values")
    {
        auto data = makeValidTree();
        auto parallel = makeNode(4, bt::BTNodeType::Parallel);
        parallel.properties["policy"] = std::string("RequireMany");
        data.graph.nodes.push_back(parallel);
        auto log = makeNode(5, bt::BTNodeType::Log);
        log.properties["level"] = std::string("Verbose");
        data.graph.nodes.push_back(log);
        auto condition = makeNode(6, bt::BTNodeType::BlackboardCondition);
        condition.properties["key"] = std::string("ok");
        condition.properties["compareOp"] = std::string("==");
        condition.properties["compareValue"] = true;
        condition.properties["abortMode"] = std::string("Parents");
        data.graph.nodes.push_back(condition);

        const auto report = validation::validateBehaviorTree(data);
        CHECK(hasDiagnostic(report, validation::Severity::Warning, "RequireMany"));
        CHECK(hasDiagnostic(report, validation::Severity::Warning, "Verbose"));
        CHECK(hasDiagnostic(report, validation::Severity::Warning, "Parents"));
    }

    TEST_CASE("subtree validation uses optional existence predicate")
    {
        bt::BehaviorTreeData data;
        data.graph.rootNodeId = 1;
        data.graph.nodes.push_back(makeNode(1, bt::BTNodeType::Root));
        auto subTree = makeNode(2, bt::BTNodeType::SubTree);
        subTree.properties["treePath"] = std::string("assets/behaviortrees/Engage.bt");
        data.graph.nodes.push_back(subTree);
        linkNodes(data.graph, 1, 2);

        validation::ValidationContext context;
        context.subtreeExists = [](std::string_view path)
        {
            return path == "assets/behaviortrees/Engage.vfBehaviorTree";
        };

        auto report = validation::validateBehaviorTree(data, context);
        CHECK(hasDiagnostic(report, validation::Severity::Error, "does not exist"));

        data.graph.nodes[1].properties["treePath"] = std::string("assets/behaviortrees/Engage.vfBehaviorTree");
        report = validation::validateBehaviorTree(data, context);
        CHECK_FALSE(hasDiagnostic(report, validation::Severity::Error, "does not exist"));

        context.allowSubTrees = false;
        report = validation::validateBehaviorTree(data, context);
        CHECK(hasDiagnostic(report, validation::Severity::Error, "still contains a SubTree"));
    }

    TEST_CASE("dynamic subtree validation (VK-1457)")
    {
        auto makeDynTree = [](const bt::BTNode& dyn)
        {
            bt::BehaviorTreeData data;
            data.graph.rootNodeId = 1;
            data.graph.nodes.push_back(makeNode(1, bt::BTNodeType::Root));
            data.graph.nodes.push_back(dyn);
            linkNodes(data.graph, 1, 2);
            finalizeIds(data.graph);
            return data;
        };

        SUBCASE("no selection source is an error")
        {
            auto dyn = makeNode(2, bt::BTNodeType::DynamicSubTree);
            auto report = validation::validateBehaviorTree(makeDynTree(dyn));
            CHECK(hasDiagnostic(report, validation::Severity::Error, "at least one of"));
        }

        SUBCASE("a selection key alone is valid")
        {
            auto dyn = makeNode(2, bt::BTNodeType::DynamicSubTree);
            dyn.properties["selectionKey"] = std::string("brain");
            auto report = validation::validateBehaviorTree(makeDynTree(dyn));
            CHECK_FALSE(report.hasErrors());
        }

        SUBCASE("a missing default tree path is an error when a predicate is provided")
        {
            auto dyn = makeNode(2, bt::BTNodeType::DynamicSubTree);
            dyn.properties["defaultTreePath"] = std::string("assets/ai/Missing.vfBehaviorTree");
            validation::ValidationContext context;
            context.subtreeExists = [](std::string_view) { return false; };
            auto report = validation::validateBehaviorTree(makeDynTree(dyn), context);
            CHECK(hasDiagnostic(report, validation::Severity::Error, "does not exist"));
        }

        SUBCASE("an empty mapping key is a warning")
        {
            auto dyn = makeNode(2, bt::BTNodeType::DynamicSubTree);
            dyn.properties["selectionKey"] = std::string("brain");
            dyn.blackboardMappings.push_back({"", "child", bt::MappingDirection::In});
            auto report = validation::validateBehaviorTree(makeDynTree(dyn));
            CHECK_FALSE(report.hasErrors());
            CHECK(hasDiagnostic(report, validation::Severity::Warning, "empty parent or child key"));
        }

        SUBCASE("DynamicSubTree survives the expanded-tree check (unlike SubTree)")
        {
            auto dyn = makeNode(2, bt::BTNodeType::DynamicSubTree);
            dyn.properties["selectionKey"] = std::string("brain");
            validation::ValidationContext context;
            context.allowSubTrees = false; // runtime-expanded tree
            auto report = validation::validateBehaviorTree(makeDynTree(dyn), context);
            CHECK_FALSE(report.hasErrors());
        }
    }

    TEST_CASE("script task missing script metadata is visible but non-blocking")
    {
        auto data = makeValidTree();
        data.graph.nodes.push_back(makeNode(4, bt::BTNodeType::ScriptTask));

        const auto report = validation::validateBehaviorTree(data);
        CHECK_FALSE(report.hasErrors());
        CHECK(hasDiagnostic(report, validation::Severity::Warning, "scriptPath"));
        CHECK(hasDiagnostic(report, validation::Severity::Warning, "scriptClassName"));
    }

    TEST_CASE("typed blackboard serialization round-trips supported boundaries")
    {
        bt::BehaviorTreeData data = makeValidTree();
        data.graph.blackboardKeys.clear();
        data.graph.blackboardKeys.push_back({"floatKey", bt::BlackboardValueType::Float, 1.5f});
        data.graph.blackboardKeys.push_back({"intKey", bt::BlackboardValueType::Int, int32_t{7}});
        data.graph.blackboardKeys.push_back({"boolKey", bt::BlackboardValueType::Bool, true});
        data.graph.blackboardKeys.push_back({"stringKey", bt::BlackboardValueType::String, std::string("hello")});
        data.graph.blackboardKeys.push_back({"vec3Key", bt::BlackboardValueType::Vec3, glm::vec3{1.0f, 2.0f, 3.0f}});
        data.graph.blackboardKeys.push_back({"entityKey", bt::BlackboardValueType::Entity, services::EntityHandle{42}});

        auto& node = data.graph.nodes[2];
        node.properties["floatValue"] = 2.5f;
        node.properties["intValue"] = int32_t{9};
        node.properties["boolValue"] = false;
        node.properties["stringValue"] = std::string("world");
        node.properties["vec3Value"] = glm::vec3{4.0f, 5.0f, 6.0f};
        node.properties["entityValue"] = services::EntityHandle{99};

        const fs::path path = uniqueTempPath("roundtrip");
        REQUIRE(bt::BehaviorTreeAsset::save(path.string(), data));
        auto loaded = bt::BehaviorTreeAsset::load(path.string());
        REQUIRE(loaded.has_value());

        const auto& keys = loaded->graph.blackboardKeys;
        REQUIRE(keys.size() == 6);
        CHECK(std::holds_alternative<float>(keys[0].defaultValue));
        CHECK(std::holds_alternative<int32_t>(keys[1].defaultValue));
        CHECK(std::holds_alternative<bool>(keys[2].defaultValue));
        CHECK(std::holds_alternative<std::string>(keys[3].defaultValue));
        CHECK(std::holds_alternative<glm::vec3>(keys[4].defaultValue));
        CHECK(std::holds_alternative<services::EntityHandle>(keys[5].defaultValue));
        CHECK(std::get<services::EntityHandle>(keys[5].defaultValue).id == 42);

        const auto* loadedNode = loaded->graph.findNodeById(3);
        REQUIRE(loadedNode != nullptr);
        CHECK(std::holds_alternative<float>(loadedNode->properties.at("floatValue")));
        CHECK(std::holds_alternative<int32_t>(loadedNode->properties.at("intValue")));
        CHECK(std::holds_alternative<bool>(loadedNode->properties.at("boolValue")));
        CHECK(std::holds_alternative<std::string>(loadedNode->properties.at("stringValue")));
        CHECK(std::holds_alternative<glm::vec3>(loadedNode->properties.at("vec3Value")));
        CHECK_FALSE(std::holds_alternative<services::EntityHandle>(loadedNode->properties.at("entityValue")));
        CHECK(std::holds_alternative<int32_t>(loadedNode->properties.at("entityValue")));

        fs::remove_all(path.parent_path());
    }

    TEST_CASE("DynamicSubTree node with blackboard mappings round-trips through save/load")
    {
        bt::BehaviorTreeData data = makeValidTree();
        // Repurpose the leaf (id 3) as a DynamicSubTree with config + explicit mappings.
        auto& node = data.graph.nodes[2];
        node.type = bt::BTNodeType::DynamicSubTree;
        node.name = "Run Dynamic Subtree";
        node.properties.clear();
        node.properties["selectionKey"] = std::string("brainPath");
        node.properties["injectionTag"] = std::string("combat");
        node.properties["defaultTreePath"] = std::string("assets/ai/Idle.vfBehaviorTree");
        node.blackboardMappings.push_back({"target", "target", bt::MappingDirection::In});
        node.blackboardMappings.push_back({"result", "outcome", bt::MappingDirection::Out});
        node.blackboardMappings.push_back({"shared", "shared", bt::MappingDirection::InOut});

        const fs::path path = uniqueTempPath("dynsub");
        REQUIRE(bt::BehaviorTreeAsset::save(path.string(), data));
        auto loaded = bt::BehaviorTreeAsset::load(path.string());
        REQUIRE(loaded.has_value());
        CHECK(loaded->version == bt::BT_FORMAT_VERSION);

        const auto* loadedNode = loaded->graph.findNodeById(3);
        REQUIRE(loadedNode != nullptr);
        CHECK(loadedNode->type == bt::BTNodeType::DynamicSubTree);
        CHECK(std::get<std::string>(loadedNode->properties.at("selectionKey")) == "brainPath");
        CHECK(std::get<std::string>(loadedNode->properties.at("injectionTag")) == "combat");

        REQUIRE(loadedNode->blackboardMappings.size() == 3);
        CHECK(loadedNode->blackboardMappings[0].parentKey == "target");
        CHECK(loadedNode->blackboardMappings[0].childKey == "target");
        CHECK(loadedNode->blackboardMappings[0].direction == bt::MappingDirection::In);
        CHECK(loadedNode->blackboardMappings[1].childKey == "outcome");
        CHECK(loadedNode->blackboardMappings[1].direction == bt::MappingDirection::Out);
        CHECK(loadedNode->blackboardMappings[2].direction == bt::MappingDirection::InOut);

        fs::remove_all(path.parent_path());
    }

    TEST_CASE("pre-1.2 asset without blackboardMappings loads with empty mappings")
    {
        // A tree with no DynamicSubTree nodes and no mappings key (the old shape) must still load,
        // and every node's blackboardMappings must be empty.
        bt::BehaviorTreeData data = makeValidTree();
        const fs::path path = uniqueTempPath("legacy");
        REQUIRE(bt::BehaviorTreeAsset::save(path.string(), data));
        auto loaded = bt::BehaviorTreeAsset::load(path.string());
        REQUIRE(loaded.has_value());
        for (const auto& node : loaded->graph.nodes)
        {
            CHECK(node.blackboardMappings.empty());
        }
        fs::remove_all(path.parent_path());
    }
}
