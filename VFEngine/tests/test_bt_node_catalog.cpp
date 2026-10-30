#include <doctest.h>
#include <behaviortree/BehaviorTreeNodeCatalog.hpp>
#include <behaviortree/BehaviorTreeTypes.hpp>

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// VK-1660: the behavior tree node catalogue (BehaviorTreeNodeCatalog) - one spec per node type, the
// editor's createNode seeds, service variants and the strict validator used before MCP saves.

namespace
{
    namespace bt = behaviortree;
    namespace catalog = behaviortree::catalog;
    namespace validation = behaviortree::validation;
    using Props = std::unordered_map<std::string, bt::BlackboardValue>;
    using Type = bt::BlackboardValueType;

    constexpr int kNodeTypeCount = static_cast<int>(bt::BTNodeType::FollowRoute) + 1;

    bool hasError(const std::vector<validation::Diagnostic>& diagnostics, const std::string& text)
    {
        return std::any_of(diagnostics.begin(), diagnostics.end(), [&text](const validation::Diagnostic& d)
        {
            return d.severity == validation::Severity::Error && d.message.find(text) != std::string::npos;
        });
    }

    std::string allMessages(const std::vector<validation::Diagnostic>& diagnostics)
    {
        std::string text;
        for (const auto& d : diagnostics)
        {
            text += d.message + "\n";
        }
        return text;
    }

    bt::BTNode makeNode(uint32_t id, bt::BTNodeType type, const std::string& name = {})
    {
        bt::BTNode node;
        node.id = id;
        node.type = type;
        node.name = name.empty() ? bt::nodeTypeToString(type) : name;
        node.properties = catalog::defaultProperties(type);
        return node;
    }

    void link(bt::BTGraph& graph, uint32_t parent, uint32_t child)
    {
        uint32_t order = 0;
        for (const auto& existing : graph.links)
        {
            if (existing.sourceNodeId == parent) ++order;
        }
        bt::BTLink l;
        l.id = graph.nextLinkId++;
        l.sourceNodeId = parent;
        l.targetNodeId = child;
        l.sortOrder = order;
        graph.links.push_back(l);
    }

    bt::BlackboardKeyDef key(const std::string& name, Type type, bt::BlackboardValue defaultValue)
    {
        bt::BlackboardKeyDef def;
        def.name = name;
        def.type = type;
        def.defaultValue = std::move(defaultValue);
        return def;
    }

    void finalizeIds(bt::BTGraph& graph)
    {
        for (const auto& node : graph.nodes)
        {
            graph.nextNodeId = std::max(graph.nextNodeId, node.id + 1);
        }
    }

    // Root(1) -> Sequence(2) -> Wait(3), with keys for the failure cases.
    bt::BehaviorTreeData makeBaseTree()
    {
        bt::BehaviorTreeData data;
        data.name = "Base";
        data.graph.rootNodeId = 1;
        data.graph.nodes.push_back(makeNode(1, bt::BTNodeType::Root));
        data.graph.nodes.push_back(makeNode(2, bt::BTNodeType::Sequence));
        data.graph.nodes.push_back(makeNode(3, bt::BTNodeType::Wait));
        link(data.graph, 1, 2);
        link(data.graph, 2, 3);
        data.graph.blackboardKeys.push_back(key("target", Type::Entity, services::EntityHandle::invalid()));
        data.graph.blackboardKeys.push_back(key("route", Type::Entity, services::EntityHandle::invalid()));
        data.graph.blackboardKeys.push_back(key("distance", Type::Float, 0.0f));
        data.graph.blackboardKeys.push_back(key("flag", Type::Bool, false));
        data.graph.blackboardKeys.push_back(key("spot", Type::Vec3, glm::vec3(0.0f)));
        data.graph.blackboardKeys.push_back(key("eqsResult", Type::Vec3, glm::vec3(0.0f)));
        finalizeIds(data.graph);
        return data;
    }

    // Replaces node 3 of the base tree (keeping the Sequence -> 3 link).
    bt::BTNode& replaceNode3(bt::BehaviorTreeData& data, bt::BTNodeType type)
    {
        bt::BTNode* node = data.graph.findNodeById(3);
        REQUIRE(node != nullptr);
        *node = makeNode(3, type);
        return *node;
    }

    // Replaces node 3 with a single-child node whose child is a Wait (id 4).
    bt::BTNode& replaceNode3WithParent(bt::BehaviorTreeData& data, bt::BTNodeType type)
    {
        replaceNode3(data, type);
        data.graph.nodes.push_back(makeNode(4, bt::BTNodeType::Wait));
        link(data.graph, 3, 4);
        finalizeIds(data.graph);
        return *data.graph.findNodeById(3);
    }

    catalog::StrictContext fakeContext()
    {
        catalog::StrictContext context;
        context.scriptClass = [](std::string_view path) -> std::optional<std::string>
        {
            if (path == "game/ai/Attack.mt") return std::string("Attack");
            return std::nullopt;
        };
        context.treeExists = [](std::string_view path) { return path == "assets/ai/Sub.vfBehaviorTree"; };
        context.eqsQueryExists = [](std::string_view name) { return name == "NearestEnemy"; };
        return context;
    }

    std::vector<std::string> paramNames(const std::vector<const catalog::ParamSpec*>& params)
    {
        std::vector<std::string> names;
        for (const auto* param : params)
        {
            names.push_back(param->name);
        }
        return names;
    }

    std::string lower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });
        return text;
    }
}

TEST_SUITE("AIBehaviorTreeCatalog")
{
    TEST_CASE("every node type has exactly one spec, in enum order")
    {
        const auto& specs = catalog::nodes();
        REQUIRE(specs.size() == static_cast<size_t>(kNodeTypeCount));

        std::unordered_set<int> seen;
        for (int i = 0; i < kNodeTypeCount; ++i)
        {
            const auto type = static_cast<bt::BTNodeType>(i);
            INFO("type ", bt::nodeTypeToString(type));
            CHECK(specs[static_cast<size_t>(i)].type == type);
            CHECK(seen.insert(static_cast<int>(specs[static_cast<size_t>(i)].type)).second);

            const catalog::NodeSpec* spec = catalog::find(type);
            REQUIRE(spec != nullptr);
            CHECK(spec->type == type);
            CHECK(spec->name == bt::nodeTypeToString(type));
            CHECK(!spec->description.empty());
            CHECK(catalog::findByName(spec->name) == spec);
            CHECK(catalog::findByName(lower(spec->name)) == spec);
            for (const auto& param : spec->params)
            {
                CHECK(!param.name.empty());
                CHECK(!param.description.empty());
            }
        }

        CHECK(catalog::findByName("Bogus") == nullptr);
        CHECK(catalog::findByName("") == nullptr);
    }

    TEST_CASE("categories and child rules follow the node classification")
    {
        for (const auto& spec : catalog::nodes())
        {
            INFO("type ", spec.name);
            switch (spec.category)
            {
            case catalog::NodeCategory::Root:
                CHECK(bt::isRootNode(spec.type));
                CHECK(spec.minChildren == 1);
                CHECK(spec.maxChildren == 1);
                break;
            case catalog::NodeCategory::Composite:
                CHECK(bt::isCompositeNode(spec.type));
                CHECK(spec.minChildren == 1);
                CHECK(spec.maxChildren == catalog::kUnlimitedChildren);
                break;
            case catalog::NodeCategory::Decorator:
                CHECK(bt::isDecoratorNode(spec.type));
                CHECK(spec.minChildren == 1);
                CHECK(spec.maxChildren == 1);
                break;
            case catalog::NodeCategory::Service:
                CHECK(bt::isServiceNode(spec.type));
                CHECK(spec.minChildren == 1);
                CHECK(spec.maxChildren == 1);
                break;
            case catalog::NodeCategory::Task:
                CHECK(bt::isTaskNode(spec.type));
                CHECK(spec.minChildren == 0);
                CHECK(spec.maxChildren == 0);
                break;
            }
        }

        CHECK(catalog::find(bt::BTNodeType::FollowRoute)->category == catalog::NodeCategory::Task);
        CHECK(catalog::find(bt::BTNodeType::DynamicSubTree)->category == catalog::NodeCategory::Task);
    }

    TEST_CASE("defaultProperties equal the editor createNode seeds")
    {
        // editor/graph/BTGraphEditorEvents.cpp createNode, plus the VK-1660 params.
        const std::unordered_map<bt::BTNodeType, Props> expected = {
            {bt::BTNodeType::Root, {}},
            {bt::BTNodeType::Sequence, {}},
            {bt::BTNodeType::Selector, {}},
            {bt::BTNodeType::Parallel, {{"policy", std::string("RequireAll")}}},
            {bt::BTNodeType::Inverter, {}},
            {bt::BTNodeType::Repeater, {{"repeatCount", int32_t{3}}}},
            {bt::BTNodeType::Succeeder, {}},
            {bt::BTNodeType::RepeatUntilFail, {}},
            {bt::BTNodeType::Cooldown, {{"cooldownTime", 1.0f}}},
            {bt::BTNodeType::TimeLimit, {{"timeLimit", 5.0f}}},
            {bt::BTNodeType::BlackboardCondition, {{"key", std::string("")}, {"compareOp", std::string("==")},
                                                   {"compareValue", 0.0f}, {"abortMode", std::string("None")}}},
            {bt::BTNodeType::Wait, {{"duration", 1.0f}}},
            {bt::BTNodeType::Log, {{"message", std::string("Hello")}, {"level", std::string("Info")}}},
            {bt::BTNodeType::MoveTo, {{"targetKey", std::string("target")}, {"arrivalDistance", 0.5f},
                                      {"repathDistance", 1.0f}, {"stopOnArrival", true}}},
            {bt::BTNodeType::PlayAnimation, {{"stateName", std::string("")}, {"waitForCompletion", false}}},
            {bt::BTNodeType::SetBlackboardValue, {{"key", std::string("")}, {"value", 0.0f}}},
            {bt::BTNodeType::CheckBlackboardValue, {{"key", std::string("")}, {"compareOp", std::string("==")},
                                                    {"compareValue", 0.0f}}},
            {bt::BTNodeType::ScriptTask, {}},
            {bt::BTNodeType::EnvironmentQuery, {{"queryName", std::string("")}, {"resultKey", std::string("eqsResult")},
                                                {"targetKey", std::string("")}, {"resultEntityKey", std::string("")}}},
            {bt::BTNodeType::LineOfSight, {{"targetKey", std::string("target")}, {"maxDistance", 50.0f},
                                           {"eyeOffset", 1.6f}}},
            {bt::BTNodeType::SubTree, {{"treePath", std::string("")}}},
            {bt::BTNodeType::Service, {{"interval", 0.5f}, {"randomDeviation", 0.0f}, {"runOnActivation", false},
                                       {"serviceType", std::string("EQSRefresh")}, {"queryName", std::string("")},
                                       {"resultKey", std::string("eqsResult")}, {"targetKey", std::string("")},
                                       {"resultEntityKey", std::string("")}}},
            {bt::BTNodeType::DynamicSubTree, {{"selectionKey", std::string("")}, {"injectionTag", std::string("")},
                                              {"defaultTreePath", std::string("")}}},
            {bt::BTNodeType::FollowRoute, {{"routeKey", std::string("route")}, {"mode", std::string("Loop")},
                                           {"arrivalDistance", 1.0f}, {"dwell", 0.0f}, {"startAtNearest", true},
                                           {"legTimeout", 30.0f}}},
        };
        REQUIRE(expected.size() == static_cast<size_t>(kNodeTypeCount));

        for (const auto& entry : expected)
        {
            const bt::BTNodeType type = entry.first;
            INFO("type ", bt::nodeTypeToString(type));
            CHECK(catalog::defaultProperties(type) == entry.second);
        }
    }

    TEST_CASE("Service defaultProperties follow the requested variant")
    {
        const Props targetDistance = catalog::defaultProperties(bt::BTNodeType::Service, "TargetDistance");
        const Props expected = {{"interval", 0.5f}, {"randomDeviation", 0.0f}, {"runOnActivation", false},
                                {"serviceType", std::string("TargetDistance")}, {"targetKey", std::string("target")},
                                {"distanceKey", std::string("targetDistance")}, {"horizontal", true}};
        CHECK(targetDistance == expected);

        const Props lineOfSight = catalog::defaultProperties(bt::BTNodeType::Service, "LineOfSightRefresh");
        CHECK(lineOfSight.count("visibilityKey") == 1);
        CHECK(lineOfSight.count("queryName") == 0);
    }

    TEST_CASE("paramsFor picks the service variant")
    {
        const catalog::NodeSpec* service = catalog::find(bt::BTNodeType::Service);
        REQUIRE(service != nullptr);

        const std::vector<std::string> common{"serviceType", "interval", "randomDeviation", "runOnActivation"};
        CHECK(paramNames(catalog::paramsFor(*service)) == common);
        CHECK(paramNames(catalog::paramsFor(*service, "Bogus")) == common);

        auto withCommon = [&common](std::vector<std::string> extra)
        {
            std::vector<std::string> names = common;
            names.insert(names.end(), extra.begin(), extra.end());
            return names;
        };
        CHECK(paramNames(catalog::paramsFor(*service, "EQSRefresh")) ==
              withCommon({"queryName", "resultKey", "targetKey", "resultEntityKey"}));
        CHECK(paramNames(catalog::paramsFor(*service, "LineOfSightRefresh")) ==
              withCommon({"targetKey", "visibilityKey", "maxDistance", "eyeOffset"}));
        CHECK(paramNames(catalog::paramsFor(*service, "FocusUpdate")) == withCommon({"targetKey", "focusKey"}));
        CHECK(paramNames(catalog::paramsFor(*service, "TargetDistance")) ==
              withCommon({"targetKey", "distanceKey", "horizontal"}));

        const std::vector<std::string> serviceTypes{"EQSRefresh", "LineOfSightRefresh", "FocusUpdate", "TargetDistance"};
        CHECK(catalog::serviceTypes() == serviceTypes);

        // A non-service node gets every param.
        const catalog::NodeSpec* moveTo = catalog::find(bt::BTNodeType::MoveTo);
        REQUIRE(moveTo != nullptr);
        const std::vector<std::string> moveToParams{"targetKey", "arrivalDistance", "repathDistance", "stopOnArrival"};
        CHECK(paramNames(catalog::paramsFor(*moveTo)) == moveToParams);
    }

    TEST_CASE("validateStrict accepts a well-formed patrol / chase / attack tree")
    {
        bt::BehaviorTreeData data;
        data.name = "EnemyAI";
        data.graph.rootNodeId = 1;

        data.graph.nodes.push_back(makeNode(1, bt::BTNodeType::Root));

        bt::BTNode sense = makeNode(2, bt::BTNodeType::Service, "Sense Target");
        sense.properties = catalog::defaultProperties(bt::BTNodeType::Service, "TargetDistance");
        sense.properties["interval"] = 0.2f;
        sense.properties["runOnActivation"] = true;
        data.graph.nodes.push_back(sense);

        data.graph.nodes.push_back(makeNode(3, bt::BTNodeType::Selector, "Brain"));

        bt::BTNode attackRange = makeNode(4, bt::BTNodeType::BlackboardCondition, "In Attack Range");
        attackRange.properties["key"] = std::string("targetDistance");
        attackRange.properties["compareOp"] = std::string("<=");
        attackRange.properties["compareValue"] = 2.0f;
        attackRange.properties["abortMode"] = std::string("Both");
        data.graph.nodes.push_back(attackRange);

        bt::BTNode attack = makeNode(5, bt::BTNodeType::ScriptTask, "Attack");
        attack.scriptPath = "game/ai/Attack.mt";
        attack.scriptClassName = "Attack";
        data.graph.nodes.push_back(attack);

        bt::BTNode chaseRange = attackRange;
        chaseRange.id = 6;
        chaseRange.name = "In Chase Range";
        chaseRange.properties["compareValue"] = 10.0f;
        data.graph.nodes.push_back(chaseRange);

        bt::BTNode chase = makeNode(7, bt::BTNodeType::MoveTo, "Chase");
        chase.properties["arrivalDistance"] = 1.5f;
        data.graph.nodes.push_back(chase);

        bt::BTNode patrol = makeNode(8, bt::BTNodeType::FollowRoute, "Patrol");
        patrol.properties["dwell"] = 1.0f;
        data.graph.nodes.push_back(patrol);

        link(data.graph, 1, 2);
        link(data.graph, 2, 3);
        link(data.graph, 3, 4);
        link(data.graph, 4, 5);
        link(data.graph, 3, 6);
        link(data.graph, 6, 7);
        link(data.graph, 3, 8);

        data.graph.blackboardKeys.push_back(key("target", Type::Entity, services::EntityHandle::invalid()));
        data.graph.blackboardKeys.push_back(key("route", Type::Entity, services::EntityHandle::invalid()));
        data.graph.blackboardKeys.push_back(key("targetDistance", Type::Float, 1000000.0f));
        data.graph.blackboardKeys.push_back(key("hits", Type::Int, int32_t{0}));
        finalizeIds(data.graph);

        const auto diagnostics = catalog::validateStrict(data, fakeContext());
        INFO(allMessages(diagnostics));
        CHECK(diagnostics.empty());
    }

    TEST_CASE("validateStrict accepts every node type once its required params are filled")
    {
        bt::BehaviorTreeData data;
        data.name = "Everything";
        data.graph.rootNodeId = 1;
        data.graph.nodes.push_back(makeNode(1, bt::BTNodeType::Root));
        data.graph.nodes.push_back(makeNode(2, bt::BTNodeType::Sequence));
        link(data.graph, 1, 2);

        uint32_t nextId = 3;
        auto addLeaf = [&](bt::BTNode node) -> uint32_t
        {
            node.id = nextId++;
            const uint32_t id = node.id;
            data.graph.nodes.push_back(std::move(node));
            link(data.graph, 2, id);
            return id;
        };
        // A single-child node under the Sequence, with a Wait under it.
        auto addParent = [&](bt::BTNode node)
        {
            const uint32_t parentId = addLeaf(std::move(node));
            bt::BTNode wait = makeNode(nextId++, bt::BTNodeType::Wait);
            const uint32_t waitId = wait.id;
            data.graph.nodes.push_back(std::move(wait));
            link(data.graph, parentId, waitId);
        };

        addLeaf(makeNode(0, bt::BTNodeType::Wait));
        addLeaf(makeNode(0, bt::BTNodeType::Log));

        bt::BTNode moveTo = makeNode(0, bt::BTNodeType::MoveTo);
        moveTo.properties["targetKey"] = std::string("spot"); // Vec3 key
        addLeaf(moveTo);

        bt::BTNode play = makeNode(0, bt::BTNodeType::PlayAnimation);
        play.properties["stateName"] = std::string("Idle");
        addLeaf(play);

        bt::BTNode setValue = makeNode(0, bt::BTNodeType::SetBlackboardValue);
        setValue.properties["key"] = std::string("hits");
        setValue.properties["value"] = int32_t{1};
        addLeaf(setValue);

        bt::BTNode check = makeNode(0, bt::BTNodeType::CheckBlackboardValue);
        check.properties["key"] = std::string("hits");
        check.properties["compareOp"] = std::string(">=");
        check.properties["compareValue"] = int32_t{1};
        addLeaf(check);

        bt::BTNode script = makeNode(0, bt::BTNodeType::ScriptTask);
        script.scriptPath = "game/ai/Attack.mt";
        script.scriptClassName = "Attack";
        addLeaf(script);

        bt::BTNode query = makeNode(0, bt::BTNodeType::EnvironmentQuery);
        query.properties["queryName"] = std::string("NearestEnemy");
        query.properties["targetKey"] = std::string("target");
        query.properties["resultEntityKey"] = std::string("enemyId");
        addLeaf(query);

        addLeaf(makeNode(0, bt::BTNodeType::LineOfSight));

        bt::BTNode subTree = makeNode(0, bt::BTNodeType::SubTree);
        subTree.properties["treePath"] = std::string("assets/ai/Sub.vfBehaviorTree");
        addLeaf(subTree);

        bt::BTNode dynamic = makeNode(0, bt::BTNodeType::DynamicSubTree);
        dynamic.properties["defaultTreePath"] = std::string("assets/ai/Sub.vfBehaviorTree");
        addLeaf(dynamic);

        addLeaf(makeNode(0, bt::BTNodeType::FollowRoute));

        addParent(makeNode(0, bt::BTNodeType::Inverter));
        addParent(makeNode(0, bt::BTNodeType::Repeater));
        addParent(makeNode(0, bt::BTNodeType::Succeeder));
        addParent(makeNode(0, bt::BTNodeType::RepeatUntilFail));
        addParent(makeNode(0, bt::BTNodeType::Cooldown));
        addParent(makeNode(0, bt::BTNodeType::TimeLimit));

        bt::BTNode condition = makeNode(0, bt::BTNodeType::BlackboardCondition);
        condition.properties["key"] = std::string("flag");
        condition.properties["compareValue"] = true;
        addParent(condition);

        bt::BTNode eqsService = makeNode(0, bt::BTNodeType::Service);
        eqsService.properties["queryName"] = std::string("NearestEnemy");
        addParent(eqsService);

        bt::BTNode losService = makeNode(0, bt::BTNodeType::Service);
        losService.properties = catalog::defaultProperties(bt::BTNodeType::Service, "LineOfSightRefresh");
        addParent(losService);

        bt::BTNode focusService = makeNode(0, bt::BTNodeType::Service);
        focusService.properties = catalog::defaultProperties(bt::BTNodeType::Service, "FocusUpdate");
        addParent(focusService);

        bt::BTNode distanceService = makeNode(0, bt::BTNodeType::Service);
        distanceService.properties = catalog::defaultProperties(bt::BTNodeType::Service, "TargetDistance");
        addParent(distanceService);

        bt::BTNode parallel = makeNode(0, bt::BTNodeType::Parallel);
        addParent(parallel);
        bt::BTNode selector = makeNode(0, bt::BTNodeType::Selector);
        addParent(selector);

        data.graph.blackboardKeys.push_back(key("target", Type::Entity, services::EntityHandle::invalid()));
        data.graph.blackboardKeys.push_back(key("route", Type::Entity, services::EntityHandle::invalid()));
        data.graph.blackboardKeys.push_back(key("spot", Type::Vec3, glm::vec3(1.0f, 2.0f, 3.0f)));
        data.graph.blackboardKeys.push_back(key("hits", Type::Int, int32_t{0}));
        data.graph.blackboardKeys.push_back(key("flag", Type::Bool, false));
        data.graph.blackboardKeys.push_back(key("eqsResult", Type::Vec3, glm::vec3(0.0f)));
        data.graph.blackboardKeys.push_back(key("enemyId", Type::Int, int32_t{-1}));
        data.graph.blackboardKeys.push_back(key("targetVisible", Type::Bool, false));
        data.graph.blackboardKeys.push_back(key("focusPoint", Type::Vec3, glm::vec3(0.0f)));
        data.graph.blackboardKeys.push_back(key("targetDistance", Type::Float, 0.0f));
        finalizeIds(data.graph);

        const auto diagnostics = catalog::validateStrict(data, fakeContext());
        INFO(allMessages(diagnostics));
        CHECK(diagnostics.empty());
    }

    TEST_CASE("validateStrict rejects each rule")
    {
        const catalog::StrictContext context = fakeContext();

        SUBCASE("base tree is clean")
        {
            const auto diagnostics = catalog::validateStrict(makeBaseTree(), context);
            INFO(allMessages(diagnostics));
            CHECK(diagnostics.empty());
        }

        SUBCASE("unknown parameter")
        {
            auto data = makeBaseTree();
            data.graph.findNodeById(3)->properties["speed"] = 1.0f;
            const auto diagnostics = catalog::validateStrict(data, context);
            CHECK(hasError(diagnostics, "unknown parameter 'speed'"));
            CHECK(hasError(diagnostics, "'Wait' (Wait, id 3)"));
        }

        SUBCASE("missing required parameter")
        {
            auto data = makeBaseTree();
            replaceNode3(data, bt::BTNodeType::PlayAnimation).properties.erase("stateName");
            CHECK(hasError(catalog::validateStrict(data, context), "missing required parameter 'stateName'"));
        }

        SUBCASE("required string must not be empty")
        {
            auto data = makeBaseTree();
            replaceNode3(data, bt::BTNodeType::PlayAnimation); // seed stateName ""
            CHECK(hasError(catalog::validateStrict(data, context), "'stateName' must not be empty"));
        }

        SUBCASE("int stored for a float parameter")
        {
            auto data = makeBaseTree();
            data.graph.findNodeById(3)->properties["duration"] = int32_t{2};
            CHECK(hasError(catalog::validateStrict(data, context), "parameter 'duration' must be Float (it is Int)"));
        }

        SUBCASE("value out of range")
        {
            auto data = makeBaseTree();
            data.graph.findNodeById(3)->properties["duration"] = 100.0f;
            CHECK(hasError(catalog::validateStrict(data, context), "'duration' = 100 is outside 0..60"));
        }

        SUBCASE("int range")
        {
            auto data = makeBaseTree();
            replaceNode3WithParent(data, bt::BTNodeType::Repeater).properties["repeatCount"] = int32_t{-1};
            CHECK(hasError(catalog::validateStrict(data, context), "'repeatCount' = -1 is outside 0..1000"));
        }

        SUBCASE("enum value must be canonical")
        {
            auto data = makeBaseTree();
            replaceNode3(data, bt::BTNodeType::Log).properties["level"] = std::string("warning");
            CHECK(hasError(catalog::validateStrict(data, context), "must be one of Info, Warn, Error"));
        }

        SUBCASE("undeclared key")
        {
            auto data = makeBaseTree();
            replaceNode3(data, bt::BTNodeType::MoveTo).properties["targetKey"] = std::string("ghost");
            CHECK(hasError(catalog::validateStrict(data, context), "blackboard key 'ghost' (targetKey) is not declared"));
        }

        SUBCASE("MoveTo needs a position or entity key")
        {
            auto data = makeBaseTree();
            replaceNode3(data, bt::BTNodeType::MoveTo).properties["targetKey"] = std::string("flag");
            CHECK(hasError(catalog::validateStrict(data, context), "targetKey needs Vec3, Entity or Int"));
        }

        SUBCASE("FollowRoute needs an entity key")
        {
            auto data = makeBaseTree();
            replaceNode3(data, bt::BTNodeType::FollowRoute).properties["routeKey"] = std::string("spot");
            CHECK(hasError(catalog::validateStrict(data, context), "routeKey needs Entity or Int"));
        }

        SUBCASE("empty key where one is required")
        {
            auto data = makeBaseTree();
            replaceNode3(data, bt::BTNodeType::MoveTo).properties["targetKey"] = std::string("");
            CHECK(hasError(catalog::validateStrict(data, context), "parameter 'targetKey' must name a blackboard key"));
        }

        SUBCASE("compareValue must match the key type")
        {
            auto data = makeBaseTree();
            auto& condition = replaceNode3WithParent(data, bt::BTNodeType::BlackboardCondition);
            condition.properties["key"] = std::string("distance");
            condition.properties["compareValue"] = true;
            CHECK(hasError(catalog::validateStrict(data, context), "does not match key 'distance' (Float)"));
        }

        SUBCASE("numeric operator on a Bool key")
        {
            auto data = makeBaseTree();
            auto& condition = replaceNode3WithParent(data, bt::BTNodeType::BlackboardCondition);
            condition.properties["key"] = std::string("flag");
            condition.properties["compareOp"] = std::string(">");
            condition.properties["compareValue"] = true;
            CHECK(hasError(catalog::validateStrict(data, context), "compareOp '>' is numeric"));
        }

        SUBCASE("conditions cannot compare Vec3 keys")
        {
            auto data = makeBaseTree();
            auto& condition = replaceNode3WithParent(data, bt::BTNodeType::BlackboardCondition);
            condition.properties["key"] = std::string("spot");
            condition.properties["compareValue"] = glm::vec3(0.0f);
            CHECK(hasError(catalog::validateStrict(data, context), "key needs Float, Int, Bool or String"));
        }

        SUBCASE("decorator with two children")
        {
            auto data = makeBaseTree();
            replaceNode3WithParent(data, bt::BTNodeType::Inverter);
            data.graph.nodes.push_back(makeNode(5, bt::BTNodeType::Wait));
            link(data.graph, 3, 5);
            CHECK(hasError(catalog::validateStrict(data, context), "needs exactly 1 child (has 2)"));
        }

        SUBCASE("composite without children")
        {
            auto data = makeBaseTree();
            replaceNode3(data, bt::BTNodeType::Selector);
            CHECK(hasError(catalog::validateStrict(data, context), "needs at least 1 child (has 0)"));
        }

        SUBCASE("task with a child")
        {
            auto data = makeBaseTree();
            data.graph.nodes.push_back(makeNode(4, bt::BTNodeType::Wait));
            link(data.graph, 3, 4);
            CHECK(hasError(catalog::validateStrict(data, context), "cannot have children (has 1)"));
        }

        SUBCASE("ScriptTask class must match the script")
        {
            auto data = makeBaseTree();
            auto& script = replaceNode3(data, bt::BTNodeType::ScriptTask);
            script.scriptPath = "game/ai/Attack.mt";
            script.scriptClassName = "Other";
            CHECK(hasError(catalog::validateStrict(data, context), "does not match the class 'Attack'"));
        }

        SUBCASE("ScriptTask needs its class name")
        {
            auto data = makeBaseTree();
            auto& script = replaceNode3(data, bt::BTNodeType::ScriptTask);
            script.scriptPath = "game/ai/Attack.mt";
            CHECK(hasError(catalog::validateStrict(data, context), "missing scriptClassName: it must be 'Attack'"));

            catalog::StrictContext unchecked;
            CHECK(hasError(catalog::validateStrict(data, unchecked), "missing scriptClassName"));
        }

        SUBCASE("ScriptTask script must be a ScriptTask")
        {
            auto data = makeBaseTree();
            auto& script = replaceNode3(data, bt::BTNodeType::ScriptTask);
            script.scriptPath = "game/ai/Missing.mt";
            CHECK(hasError(catalog::validateStrict(data, context), "is not a ScriptTask"));
        }

        SUBCASE("ScriptTask without a script")
        {
            auto data = makeBaseTree();
            replaceNode3(data, bt::BTNodeType::ScriptTask);
            CHECK(hasError(catalog::validateStrict(data, context), "missing required parameter 'script'"));
        }

        SUBCASE("EQS query must exist")
        {
            auto data = makeBaseTree();
            replaceNode3(data, bt::BTNodeType::EnvironmentQuery).properties["queryName"] = std::string("Missing");
            CHECK(hasError(catalog::validateStrict(data, context), "EQS query 'Missing' does not exist"));
        }

        SUBCASE("EQS query name required")
        {
            auto data = makeBaseTree();
            replaceNode3(data, bt::BTNodeType::EnvironmentQuery); // seed queryName ""
            CHECK(hasError(catalog::validateStrict(data, context), "'queryName' must name an EQS query"));
        }

        SUBCASE("SubTree path must exist")
        {
            auto data = makeBaseTree();
            replaceNode3(data, bt::BTNodeType::SubTree).properties["treePath"] = std::string("assets/ai/Nope.vfBehaviorTree");
            CHECK(hasError(catalog::validateStrict(data, context), "tree 'assets/ai/Nope.vfBehaviorTree' (treePath) does not exist"));
        }

        SUBCASE("DynamicSubTree with nothing set")
        {
            auto data = makeBaseTree();
            replaceNode3(data, bt::BTNodeType::DynamicSubTree);
            CHECK(hasError(catalog::validateStrict(data, context), "needs at least one of selectionKey, injectionTag or defaultTreePath"));
        }

        SUBCASE("EQSRefresh interval below 0.1")
        {
            auto data = makeBaseTree();
            auto& service = replaceNode3WithParent(data, bt::BTNodeType::Service);
            service.properties["queryName"] = std::string("NearestEnemy");
            service.properties["interval"] = 0.05f;
            CHECK(hasError(catalog::validateStrict(data, context), "interval >= 0.1"));
        }

        SUBCASE("variant param on another service type")
        {
            auto data = makeBaseTree();
            auto& service = replaceNode3WithParent(data, bt::BTNodeType::Service);
            service.properties = catalog::defaultProperties(bt::BTNodeType::Service, "TargetDistance");
            service.properties["targetKey"] = std::string("target");
            service.properties["queryName"] = std::string("NearestEnemy");
            data.graph.blackboardKeys.push_back(key("targetDistance", Type::Float, 0.0f));
            CHECK(hasError(catalog::validateStrict(data, context), "unknown parameter 'queryName' for serviceType TargetDistance"));
        }

        SUBCASE("unknown service type")
        {
            auto data = makeBaseTree();
            auto& service = replaceNode3WithParent(data, bt::BTNodeType::Service);
            service.properties = catalog::defaultProperties(bt::BTNodeType::Service, "");
            service.properties["serviceType"] = std::string("Bogus");
            service.properties.erase("queryName");
            service.properties.erase("resultKey");
            service.properties.erase("targetKey");
            service.properties.erase("resultEntityKey");
            CHECK(hasError(catalog::validateStrict(data, context), "'serviceType' = 'Bogus' must be one of"));
        }

        SUBCASE("Entity key with a default")
        {
            auto data = makeBaseTree();
            data.graph.blackboardKeys.push_back(key("who", Type::Entity, services::EntityHandle{5}));
            const auto diagnostics = catalog::validateStrict(data, context);
            CHECK(hasError(diagnostics, "Blackboard key 'who': an Entity key cannot have a default"));
            const auto it = std::find_if(diagnostics.begin(), diagnostics.end(), [](const validation::Diagnostic& d)
            {
                return d.message.find("'who'") != std::string::npos;
            });
            REQUIRE(it != diagnostics.end());
            CHECK(it->nodeId == 0u);
        }

        SUBCASE("blackboard key names and default types")
        {
            auto data = makeBaseTree();
            data.graph.blackboardKeys.push_back(key("", Type::Float, 0.0f));
            data.graph.blackboardKeys.push_back(key("flag", Type::Bool, false));
            data.graph.blackboardKeys.push_back(key("speed", Type::Float, int32_t{3}));
            const auto diagnostics = catalog::validateStrict(data, context);
            CHECK(hasError(diagnostics, "Blackboard key with an empty name"));
            CHECK(hasError(diagnostics, "Duplicate blackboard key 'flag'"));
            CHECK(hasError(diagnostics, "Blackboard key 'speed': its default is Int but the key is Float"));
        }
    }

    TEST_CASE("validateStrict onlyNodes restricts the per-node checks")
    {
        auto data = makeBaseTree();
        data.graph.findNodeById(3)->properties["speed"] = 1.0f; // per-node finding on node 3
        data.graph.nodes.push_back(makeNode(4, bt::BTNodeType::Wait));
        link(data.graph, 3, 4); // structural finding on node 3 (task with a child)

        data.graph.blackboardKeys.push_back(key("", Type::Float, 0.0f)); // graph-level finding

        catalog::StrictContext context = fakeContext();

        context.onlyNodes = std::unordered_set<uint32_t>{2};
        auto diagnostics = catalog::validateStrict(data, context);
        CHECK(!hasError(diagnostics, "unknown parameter 'speed'"));
        CHECK(!hasError(diagnostics, "cannot have children (has 1)")); // node 3 is untouched
        CHECK(hasError(diagnostics, "Blackboard key with an empty name")); // key checks always run

        context.onlyNodes = std::unordered_set<uint32_t>{3};
        diagnostics = catalog::validateStrict(data, context);
        CHECK(hasError(diagnostics, "unknown parameter 'speed'"));
        CHECK(hasError(diagnostics, "cannot have children (has 1)"));

        context.onlyNodes = std::unordered_set<uint32_t>{}; // no node is touched
        diagnostics = catalog::validateStrict(data, context);
        CHECK(!hasError(diagnostics, "unknown parameter 'speed'"));
        CHECK(!hasError(diagnostics, "cannot have children (has 1)"));
        CHECK(hasError(diagnostics, "Blackboard key with an empty name"));

        context.onlyNodes.reset(); // every node
        diagnostics = catalog::validateStrict(data, context);
        CHECK(hasError(diagnostics, "unknown parameter 'speed'"));
        CHECK(hasError(diagnostics, "cannot have children (has 1)"));
    }

    TEST_CASE("validateStrict skips checks whose context callback is unset")
    {
        auto data = makeBaseTree();
        auto& script = replaceNode3(data, bt::BTNodeType::ScriptTask);
        script.scriptPath = "game/ai/Whatever.mt";
        script.scriptClassName = "SomethingElse";

        data.graph.nodes.push_back(makeNode(4, bt::BTNodeType::EnvironmentQuery));
        data.graph.findNodeById(4)->properties["queryName"] = std::string("AnyName");
        link(data.graph, 2, 4);
        data.graph.nodes.push_back(makeNode(5, bt::BTNodeType::SubTree));
        data.graph.findNodeById(5)->properties["treePath"] = std::string("assets/ai/Anything.vfBehaviorTree");
        link(data.graph, 2, 5);
        finalizeIds(data.graph);

        const auto diagnostics = catalog::validateStrict(data, catalog::StrictContext{});
        INFO(allMessages(diagnostics));
        CHECK(diagnostics.empty());
    }

    TEST_CASE("category and param kind names are stable")
    {
        CHECK(std::string(catalog::categoryToString(catalog::NodeCategory::Root)) == "root");
        CHECK(std::string(catalog::categoryToString(catalog::NodeCategory::Composite)) == "composite");
        CHECK(std::string(catalog::categoryToString(catalog::NodeCategory::Decorator)) == "decorator");
        CHECK(std::string(catalog::categoryToString(catalog::NodeCategory::Service)) == "service");
        CHECK(std::string(catalog::categoryToString(catalog::NodeCategory::Task)) == "task");

        CHECK(std::string(catalog::paramKindToString(catalog::ParamKind::Float)) == "float");
        CHECK(std::string(catalog::paramKindToString(catalog::ParamKind::Int)) == "int");
        CHECK(std::string(catalog::paramKindToString(catalog::ParamKind::Bool)) == "bool");
        CHECK(std::string(catalog::paramKindToString(catalog::ParamKind::String)) == "string");
        CHECK(std::string(catalog::paramKindToString(catalog::ParamKind::Enum)) == "enum");
        CHECK(std::string(catalog::paramKindToString(catalog::ParamKind::KeyRef)) == "key");
        CHECK(std::string(catalog::paramKindToString(catalog::ParamKind::TypedValue)) == "value");
        CHECK(std::string(catalog::paramKindToString(catalog::ParamKind::TreePath)) == "treePath");
        CHECK(std::string(catalog::paramKindToString(catalog::ParamKind::ScriptPath)) == "script");
        CHECK(std::string(catalog::paramKindToString(catalog::ParamKind::EqsQuery)) == "eqsQuery");
    }
}
