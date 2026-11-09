#include <doctest.h>
#include <behaviortree/BehaviorTreeTypes.hpp>
#include <behaviortree/BehaviorTreeRuntime.hpp>
#include <behaviortree/BehaviorTreeValidation.hpp>
#include <behaviortree/BehaviorTreeNodeCatalog.hpp>
#include <behaviortree/Blackboard.hpp>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

// ============================================================
// VK-1660: FollowRoute node type, MoveTo request and TargetDistance service plumbing
// (the route stepping itself is covered by test_bt_route_follower.cpp)
// ============================================================

namespace
{
    namespace bt = behaviortree;

    // Implements the pure executor hooks; FollowRoute is overridden only by RouteRecorder below.
    struct BaseTestExecutor : bt::IBTTaskExecutor
    {
        bt::BTNodeStatus executeMoveTo(services::EntityHandle, const bt::MoveToRequest&,
                                       bt::Blackboard&, bool) override
        {
            return bt::BTNodeStatus::Running;
        }
        bt::BTNodeStatus executePlayAnimation(services::EntityHandle, const std::string&, bool) override
        {
            return bt::BTNodeStatus::Success;
        }
        bt::BTNodeStatus executeScriptTask(services::EntityHandle, const std::string&, const std::string&,
                                           bt::Blackboard&, float) override
        {
            return bt::BTNodeStatus::Success;
        }
        bt::BTNodeStatus executeLog(const std::string&, bt::LogLevel) override
        {
            return bt::BTNodeStatus::Success;
        }
        bt::BTNodeStatus executeEnvironmentQuery(services::EntityHandle, const bt::BTNode&,
                                                 const bt::EnvironmentQueryRequest&, bt::Blackboard&, bool) override
        {
            return bt::BTNodeStatus::Running;
        }
        bt::BTNodeStatus executeLineOfSight(services::EntityHandle, const std::string&, float, float,
                                            bt::Blackboard&) override
        {
            return bt::BTNodeStatus::Success;
        }
    };

    struct RouteRecorder : BaseTestExecutor
    {
        int calls = 0;
        std::vector<bool> firstTicks;
        bt::FollowRouteRequest lastRequest;
        uint32_t lastNodeId = 0;

        bt::BTNodeStatus executeFollowRoute(services::EntityHandle, const bt::BTNode& node,
                                            const bt::FollowRouteRequest& request, bt::Blackboard&,
                                            float, bool isFirstTick) override
        {
            ++calls;
            firstTicks.push_back(isFirstTick);
            lastRequest = request;
            lastNodeId = node.id;
            return bt::BTNodeStatus::Running;
        }
    };

    struct MoveRecorder : BaseTestExecutor
    {
        int calls = 0;
        bt::MoveToRequest lastRequest;

        bt::BTNodeStatus executeMoveTo(services::EntityHandle, const bt::MoveToRequest& request,
                                       bt::Blackboard&, bool) override
        {
            ++calls;
            lastRequest = request;
            return bt::BTNodeStatus::Running;
        }
    };

    struct QueryRecorder : BaseTestExecutor
    {
        int calls = 0;
        uint32_t lastNodeId = 0;
        bt::EnvironmentQueryRequest lastRequest;

        bt::BTNodeStatus executeEnvironmentQuery(services::EntityHandle, const bt::BTNode& node,
                                                 const bt::EnvironmentQueryRequest& request, bt::Blackboard&,
                                                 bool) override
        {
            ++calls;
            lastNodeId = node.id;
            lastRequest = request;
            return bt::BTNodeStatus::Running;
        }
    };

    bt::BTNode makeNode(uint32_t id, bt::BTNodeType type)
    {
        bt::BTNode node;
        node.id = id;
        node.type = type;
        node.name = bt::nodeTypeToString(type);
        return node;
    }

    void link(bt::BTGraph& graph, uint32_t source, uint32_t target, uint32_t sortOrder)
    {
        bt::BTLink l;
        l.id = graph.nextLinkId++;
        l.sourceNodeId = source;
        l.targetNodeId = target;
        l.sortOrder = sortOrder;
        graph.links.push_back(l);
    }

    // Root -> FollowRoute(id 2) with the given properties.
    bt::BehaviorTreeData makeRouteTree(std::unordered_map<std::string, bt::BlackboardValue> properties)
    {
        bt::BehaviorTreeData data;
        data.name = "RouteTree";
        data.graph.nodes.push_back(makeNode(1, bt::BTNodeType::Root));
        auto route = makeNode(2, bt::BTNodeType::FollowRoute);
        route.properties = std::move(properties);
        data.graph.nodes.push_back(route);
        data.graph.rootNodeId = 1;
        link(data.graph, 1, 2, 0);
        data.graph.nextNodeId = 3;

        bt::BlackboardKeyDef routeKey;
        routeKey.name = "patrol";
        routeKey.type = bt::BlackboardValueType::Entity;
        routeKey.defaultValue = services::EntityHandle::invalid();
        data.graph.blackboardKeys.push_back(routeKey);
        return data;
    }

    // Root -> MoveTo(id 2) with the given properties.
    bt::BehaviorTreeData makeMoveTree(std::unordered_map<std::string, bt::BlackboardValue> properties)
    {
        bt::BehaviorTreeData data;
        data.name = "MoveTree";
        data.graph.nodes.push_back(makeNode(1, bt::BTNodeType::Root));
        auto move = makeNode(2, bt::BTNodeType::MoveTo);
        move.properties = std::move(properties);
        data.graph.nodes.push_back(move);
        data.graph.rootNodeId = 1;
        link(data.graph, 1, 2, 0);
        data.graph.nextNodeId = 3;
        return data;
    }

    // Root -> EnvironmentQuery(id 2) with the given properties.
    bt::BehaviorTreeData makeQueryTree(std::unordered_map<std::string, bt::BlackboardValue> properties)
    {
        bt::BehaviorTreeData data;
        data.name = "QueryTree";
        data.graph.nodes.push_back(makeNode(1, bt::BTNodeType::Root));
        auto query = makeNode(2, bt::BTNodeType::EnvironmentQuery);
        query.properties = std::move(properties);
        data.graph.nodes.push_back(query);
        data.graph.rootNodeId = 1;
        link(data.graph, 1, 2, 0);
        data.graph.nextNodeId = 3;
        return data;
    }

    template <typename T>
    T catalogDefault(bt::BTNodeType type, const std::string& param)
    {
        const auto properties = bt::catalog::defaultProperties(type);
        const auto it = properties.find(param);
        REQUIRE(it != properties.end());
        REQUIRE(std::holds_alternative<T>(it->second));
        return std::get<T>(it->second);
    }

    bool hasWarningContaining(const bt::validation::ValidationReport& report, const std::string& text)
    {
        return std::any_of(report.diagnostics.begin(), report.diagnostics.end(), [&](const auto& d)
        {
            return d.severity == bt::validation::Severity::Warning && d.message.find(text) != std::string::npos;
        });
    }
}

TEST_SUITE("BTFollowRouteTask")
{
    TEST_CASE("FollowRoute is a task leaf and round-trips its type string")
    {
        CHECK(bt::isTaskNode(bt::BTNodeType::FollowRoute));
        CHECK_FALSE(bt::isCompositeNode(bt::BTNodeType::FollowRoute));
        CHECK_FALSE(bt::isDecoratorNode(bt::BTNodeType::FollowRoute));
        CHECK_FALSE(bt::isServiceNode(bt::BTNodeType::FollowRoute));
        CHECK_FALSE(bt::hasOutputPin(bt::BTNodeType::FollowRoute));
        CHECK(std::string(bt::nodeTypeToString(bt::BTNodeType::FollowRoute)) == "FollowRoute");
        CHECK(bt::stringToNodeType("FollowRoute") == bt::BTNodeType::FollowRoute);
    }

    TEST_CASE("RouteMode string conversion round-trips and falls back to Loop")
    {
        for (auto mode : {bt::RouteMode::Loop, bt::RouteMode::PingPong, bt::RouteMode::Once})
        {
            CHECK(bt::stringToRouteMode(bt::routeModeToString(mode)) == mode);
        }
        CHECK(std::string(bt::routeModeToString(bt::RouteMode::PingPong)) == "PingPong");
        CHECK(bt::stringToRouteMode("zigzag") == bt::RouteMode::Loop);
        CHECK(bt::stringToRouteMode("loop") == bt::RouteMode::Loop); // canonical spelling is "Loop"
    }

    TEST_CASE("tickTask hands every FollowRoute property to the executor")
    {
        auto data = makeRouteTree({
            {"routeKey", std::string("patrol")},
            {"mode", std::string("PingPong")},
            {"arrivalDistance", 2.0f},
            {"dwell", 1.5f},
            {"startAtNearest", false},
            {"legTimeout", 0.0f},
        });

        bt::BehaviorTreeRuntime runtime;
        runtime.init(std::make_shared<bt::BehaviorTreeData>(data), services::EntityHandle{});
        RouteRecorder executor;

        CHECK(runtime.tick(0.1f, &executor) == bt::BTNodeStatus::Running);
        CHECK(runtime.tick(0.1f, &executor) == bt::BTNodeStatus::Running);

        REQUIRE(executor.calls == 2);
        CHECK(executor.firstTicks[0] == true);
        CHECK(executor.firstTicks[1] == false);
        CHECK(executor.lastNodeId == 2);
        CHECK(executor.lastRequest.routeKey == "patrol");
        CHECK(executor.lastRequest.mode == bt::RouteMode::PingPong);
        CHECK(executor.lastRequest.arrivalDistance == doctest::Approx(2.0f));
        CHECK(executor.lastRequest.dwell == doctest::Approx(1.5f));
        CHECK(executor.lastRequest.startAtNearest == false);
        CHECK(executor.lastRequest.legTimeout == doctest::Approx(0.0f));
    }

    TEST_CASE("missing FollowRoute properties take the request defaults")
    {
        bt::BehaviorTreeRuntime runtime;
        runtime.init(std::make_shared<bt::BehaviorTreeData>(makeRouteTree({})), services::EntityHandle{});
        RouteRecorder executor;
        runtime.tick(0.1f, &executor);

        const bt::FollowRouteRequest defaults;
        REQUIRE(executor.calls == 1);
        CHECK(executor.lastRequest.routeKey == defaults.routeKey);
        CHECK(executor.lastRequest.mode == defaults.mode);
        CHECK(executor.lastRequest.arrivalDistance == doctest::Approx(defaults.arrivalDistance));
        CHECK(executor.lastRequest.dwell == doctest::Approx(defaults.dwell));
        CHECK(executor.lastRequest.startAtNearest == defaults.startAtNearest);
        CHECK(executor.lastRequest.legTimeout == doctest::Approx(defaults.legTimeout));
    }

    TEST_CASE("tickTask hands every MoveTo property to the executor")
    {
        bt::BehaviorTreeRuntime runtime;
        runtime.init(std::make_shared<bt::BehaviorTreeData>(makeMoveTree({
            {"targetKey", std::string("enemy")},
            {"arrivalDistance", 1.5f},
            {"repathDistance", 0.0f},
            {"stopOnArrival", false},
        })), services::EntityHandle{});
        MoveRecorder executor;
        CHECK(runtime.tick(0.1f, &executor) == bt::BTNodeStatus::Running);

        REQUIRE(executor.calls == 1);
        CHECK(executor.lastRequest.targetKey == "enemy");
        CHECK(executor.lastRequest.arrivalDistance == doctest::Approx(1.5f));
        CHECK(executor.lastRequest.repathDistance == doctest::Approx(0.0f));
        CHECK(executor.lastRequest.stopOnArrival == false);
    }

    TEST_CASE("missing MoveTo properties take the request defaults")
    {
        bt::BehaviorTreeRuntime runtime;
        runtime.init(std::make_shared<bt::BehaviorTreeData>(makeMoveTree({})), services::EntityHandle{});
        MoveRecorder executor;
        runtime.tick(0.1f, &executor);

        const bt::MoveToRequest defaults;
        REQUIRE(executor.calls == 1);
        CHECK(executor.lastRequest.targetKey == defaults.targetKey);
        CHECK(executor.lastRequest.arrivalDistance == doctest::Approx(defaults.arrivalDistance));
        CHECK(executor.lastRequest.repathDistance == doctest::Approx(defaults.repathDistance));
        CHECK(executor.lastRequest.stopOnArrival == defaults.stopOnArrival);
    }

    TEST_CASE("tickTask hands every EnvironmentQuery property and the node to the executor")
    {
        bt::BehaviorTreeRuntime runtime;
        runtime.init(std::make_shared<bt::BehaviorTreeData>(makeQueryTree({
            {"queryName", std::string("NearestEnemy")},
            {"resultKey", std::string("enemyPos")},
            {"targetKey", std::string("target")},
            {"resultEntityKey", std::string("enemyId")},
        })), services::EntityHandle{});
        QueryRecorder executor;
        CHECK(runtime.tick(0.1f, &executor) == bt::BTNodeStatus::Running);

        REQUIRE(executor.calls == 1);
        CHECK(executor.lastNodeId == 2);
        CHECK(executor.lastRequest.queryName == "NearestEnemy");
        CHECK(executor.lastRequest.resultKey == "enemyPos");
        CHECK(executor.lastRequest.targetKey == "target");
        CHECK(executor.lastRequest.resultEntityKey == "enemyId");
    }

    TEST_CASE("missing EnvironmentQuery properties take the request defaults")
    {
        bt::BehaviorTreeRuntime runtime;
        runtime.init(std::make_shared<bt::BehaviorTreeData>(makeQueryTree({})), services::EntityHandle{});
        QueryRecorder executor;
        runtime.tick(0.1f, &executor);

        const bt::EnvironmentQueryRequest defaults;
        REQUIRE(executor.calls == 1);
        CHECK(executor.lastRequest.queryName == defaults.queryName);
        CHECK(executor.lastRequest.resultKey == defaults.resultKey);
        CHECK(executor.lastRequest.targetKey.empty());
        CHECK(executor.lastRequest.resultEntityKey.empty());
    }

    TEST_CASE("the catalogue seeds EnvironmentQuery with the request defaults")
    {
        const bt::EnvironmentQueryRequest query;
        CHECK(catalogDefault<std::string>(bt::BTNodeType::EnvironmentQuery, "queryName") == query.queryName);
        CHECK(catalogDefault<std::string>(bt::BTNodeType::EnvironmentQuery, "resultKey") == query.resultKey);
        CHECK(catalogDefault<std::string>(bt::BTNodeType::EnvironmentQuery, "targetKey") == query.targetKey);
        CHECK(catalogDefault<std::string>(bt::BTNodeType::EnvironmentQuery, "resultEntityKey") ==
              query.resultEntityKey);
    }

    TEST_CASE("the catalogue seeds MoveTo and FollowRoute with the request defaults")
    {
        const bt::MoveToRequest move;
        CHECK(catalogDefault<std::string>(bt::BTNodeType::MoveTo, "targetKey") == move.targetKey);
        CHECK(catalogDefault<float>(bt::BTNodeType::MoveTo, "arrivalDistance") == doctest::Approx(move.arrivalDistance));
        CHECK(catalogDefault<float>(bt::BTNodeType::MoveTo, "repathDistance") == doctest::Approx(move.repathDistance));
        CHECK(catalogDefault<bool>(bt::BTNodeType::MoveTo, "stopOnArrival") == move.stopOnArrival);

        const bt::FollowRouteRequest route;
        CHECK(catalogDefault<std::string>(bt::BTNodeType::FollowRoute, "routeKey") == route.routeKey);
        CHECK(bt::stringToRouteMode(catalogDefault<std::string>(bt::BTNodeType::FollowRoute, "mode")) == route.mode);
        CHECK(catalogDefault<float>(bt::BTNodeType::FollowRoute, "arrivalDistance") == doctest::Approx(route.arrivalDistance));
        CHECK(catalogDefault<float>(bt::BTNodeType::FollowRoute, "dwell") == doctest::Approx(route.dwell));
        CHECK(catalogDefault<bool>(bt::BTNodeType::FollowRoute, "startAtNearest") == route.startAtNearest);
        CHECK(catalogDefault<float>(bt::BTNodeType::FollowRoute, "legTimeout") == doctest::Approx(route.legTimeout));
    }

    TEST_CASE("an executor without route support fails the task")
    {
        bt::BehaviorTreeRuntime runtime;
        runtime.init(std::make_shared<bt::BehaviorTreeData>(makeRouteTree({})), services::EntityHandle{});
        BaseTestExecutor executor;
        CHECK(runtime.tick(0.1f, &executor) == bt::BTNodeStatus::Failure);
    }

    TEST_CASE("validation: FollowRoute mode and route key")
    {
        auto good = makeRouteTree({{"routeKey", std::string("patrol")}, {"mode", std::string("Once")}});
        auto goodReport = bt::validation::validateBehaviorTree(good);
        CHECK_FALSE(hasWarningContaining(goodReport, "FollowRoute property 'mode'"));
        CHECK_FALSE(hasWarningContaining(goodReport, "routeKey"));

        auto badMode = makeRouteTree({{"routeKey", std::string("patrol")}, {"mode", std::string("Zigzag")}});
        CHECK(hasWarningContaining(bt::validation::validateBehaviorTree(badMode), "FollowRoute property 'mode'"));

        auto undeclared = makeRouteTree({{"routeKey", std::string("nowhere")}});
        CHECK(hasWarningContaining(bt::validation::validateBehaviorTree(undeclared), "nowhere"));
    }

    TEST_CASE("validation: TargetDistance is a supported service type that checks its keys")
    {
        bt::BehaviorTreeData data;
        data.name = "Sense";
        data.graph.nodes.push_back(makeNode(1, bt::BTNodeType::Root));
        auto service = makeNode(2, bt::BTNodeType::Service);
        service.properties["serviceType"] = std::string("TargetDistance");
        service.properties["interval"] = 0.2f;
        service.properties["targetKey"] = std::string("target");
        data.graph.nodes.push_back(service);
        auto wait = makeNode(3, bt::BTNodeType::Wait);
        wait.properties["duration"] = 1.0f;
        data.graph.nodes.push_back(wait);
        data.graph.rootNodeId = 1;
        link(data.graph, 1, 2, 0);
        link(data.graph, 2, 3, 0);

        bt::BlackboardKeyDef target;
        target.name = "target";
        target.type = bt::BlackboardValueType::Entity;
        target.defaultValue = services::EntityHandle::invalid();
        data.graph.blackboardKeys.push_back(target);

        // distanceKey missing -> the referenced-key warning, but serviceType itself is supported.
        auto report = bt::validation::validateBehaviorTree(data);
        CHECK_FALSE(hasWarningContaining(report, "unsupported value 'TargetDistance'"));
        CHECK(hasWarningContaining(report, "distanceKey"));

        bt::BlackboardKeyDef distance;
        distance.name = "targetDistance";
        distance.type = bt::BlackboardValueType::Float;
        distance.defaultValue = 1000000.0f;
        data.graph.blackboardKeys.push_back(distance);
        data.graph.nodes[1].properties["distanceKey"] = std::string("targetDistance");
        CHECK_FALSE(hasWarningContaining(bt::validation::validateBehaviorTree(data), "distanceKey"));
    }
}
