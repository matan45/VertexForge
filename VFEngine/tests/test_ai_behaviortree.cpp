#include <doctest.h>
#include <behaviortree/BehaviorTreeTypes.hpp>
#include <behaviortree/BehaviorTreeRuntime.hpp>
#include <behaviortree/BehaviorTreeAsset.hpp>
#include <behaviortree/Blackboard.hpp>
#include <string>
#include <vector>
#include <utility>
#include <memory>

// ============================================================
// VK-1091: AI / Behavior Tree unit tests
// ============================================================

TEST_SUITE("AIBehaviorTree") {

// ---- Blackboard: basic operations ----

TEST_CASE("Blackboard: set/get/has/remove/clear") {
    behaviortree::Blackboard bb;
    CHECK_FALSE(bb.has("key"));

    bb.set("key", 42.0f);
    CHECK(bb.has("key"));

    auto val = bb.get("key");
    REQUIRE(std::holds_alternative<float>(val));
    CHECK(std::get<float>(val) == doctest::Approx(42.0f));

    bb.remove("key");
    CHECK_FALSE(bb.has("key"));

    bb.set("a", 1.0f);
    bb.set("b", 2.0f);
    bb.clear();
    CHECK_FALSE(bb.has("a"));
    CHECK_FALSE(bb.has("b"));
}

// ---- Blackboard: typed accessors ----

TEST_CASE("Blackboard: typed accessors with defaults") {
    behaviortree::Blackboard bb;

    // Defaults when keys are missing
    CHECK(bb.getFloat("f") == doctest::Approx(0.0f));
    CHECK(bb.getFloat("f", 7.5f) == doctest::Approx(7.5f));
    CHECK(bb.getInt("i") == 0);
    CHECK(bb.getInt("i", -1) == -1);
    CHECK(bb.getBool("b") == false);
    CHECK(bb.getBool("b", true) == true);
    CHECK(bb.getString("s") == "");
    CHECK(bb.getString("s", "hello") == "hello");
    CHECK(bb.getVec3("v") == glm::vec3{0.0f});
    CHECK(bb.getVec3("v", glm::vec3{1.0f, 2.0f, 3.0f}) == glm::vec3{1.0f, 2.0f, 3.0f});

    // Set and retrieve typed values
    bb.set("f", 3.14f);
    CHECK(bb.getFloat("f") == doctest::Approx(3.14f));

    bb.set("i", int32_t(99));
    CHECK(bb.getInt("i") == 99);

    bb.set("b", true);
    CHECK(bb.getBool("b") == true);

    bb.set("s", std::string("world"));
    CHECK(bb.getString("s") == "world");

    bb.set("v", glm::vec3{4.0f, 5.0f, 6.0f});
    auto v = bb.getVec3("v");
    CHECK(v.x == doctest::Approx(4.0f));
    CHECK(v.y == doctest::Approx(5.0f));
    CHECK(v.z == doctest::Approx(6.0f));
}

TEST_CASE("Blackboard: getAll returns all values") {
    behaviortree::Blackboard bb;
    bb.set("a", 1.0f);
    bb.set("b", int32_t(2));
    bb.set("c", true);

    const auto& all = bb.getAll();
    CHECK(all.size() == 3);
    CHECK(all.count("a") == 1);
    CHECK(all.count("b") == 1);
    CHECK(all.count("c") == 1);
}

// ---- Node classification ----

TEST_CASE("Node classification: isCompositeNode") {
    CHECK(behaviortree::isCompositeNode(behaviortree::BTNodeType::Sequence) == true);
    CHECK(behaviortree::isCompositeNode(behaviortree::BTNodeType::Selector) == true);
    CHECK(behaviortree::isCompositeNode(behaviortree::BTNodeType::Parallel) == true);

    CHECK(behaviortree::isCompositeNode(behaviortree::BTNodeType::Wait) == false);
    CHECK(behaviortree::isCompositeNode(behaviortree::BTNodeType::Log) == false);
}

TEST_CASE("Node classification: isDecoratorNode") {
    CHECK(behaviortree::isDecoratorNode(behaviortree::BTNodeType::Inverter) == true);
    CHECK(behaviortree::isDecoratorNode(behaviortree::BTNodeType::Repeater) == true);

    CHECK(behaviortree::isDecoratorNode(behaviortree::BTNodeType::Sequence) == false);
}

TEST_CASE("Node classification: isTaskNode") {
    CHECK(behaviortree::isTaskNode(behaviortree::BTNodeType::Wait) == true);
    CHECK(behaviortree::isTaskNode(behaviortree::BTNodeType::Log) == true);
    CHECK(behaviortree::isTaskNode(behaviortree::BTNodeType::MoveTo) == true);

    CHECK(behaviortree::isTaskNode(behaviortree::BTNodeType::Sequence) == false);
}

// ---- String <-> Enum: nodeTypeToString / stringToNodeType ----

TEST_CASE("String/Enum roundtrip: nodeTypeToString and stringToNodeType") {
    const behaviortree::BTNodeType allTypes[] = {
        behaviortree::BTNodeType::Root,
        behaviortree::BTNodeType::Sequence,
        behaviortree::BTNodeType::Selector,
        behaviortree::BTNodeType::Parallel,
        behaviortree::BTNodeType::Inverter,
        behaviortree::BTNodeType::Repeater,
        behaviortree::BTNodeType::Succeeder,
        behaviortree::BTNodeType::RepeatUntilFail,
        behaviortree::BTNodeType::Cooldown,
        behaviortree::BTNodeType::TimeLimit,
        behaviortree::BTNodeType::Wait,
        behaviortree::BTNodeType::Log,
        behaviortree::BTNodeType::MoveTo,
        behaviortree::BTNodeType::PlayAnimation,
        behaviortree::BTNodeType::SetBlackboardValue,
        behaviortree::BTNodeType::CheckBlackboardValue,
        behaviortree::BTNodeType::ScriptTask,
        behaviortree::BTNodeType::EnvironmentQuery,
        behaviortree::BTNodeType::LineOfSight,
        behaviortree::BTNodeType::BlackboardCondition,
        behaviortree::BTNodeType::SubTree
    };

    for (auto type : allTypes) {
        const char* str = behaviortree::nodeTypeToString(type);
        REQUIRE(str != nullptr);
        CHECK(behaviortree::stringToNodeType(str) == type);
    }
}

// ---- String <-> Enum: blackboardValueTypeToString / stringToBlackboardValueType ----

TEST_CASE("String/Enum roundtrip: blackboardValueTypeToString and stringToBlackboardValueType") {
    const behaviortree::BlackboardValueType allValueTypes[] = {
        behaviortree::BlackboardValueType::Float,
        behaviortree::BlackboardValueType::Int,
        behaviortree::BlackboardValueType::Bool,
        behaviortree::BlackboardValueType::String,
        behaviortree::BlackboardValueType::Vec3,
        behaviortree::BlackboardValueType::Entity
    };

    for (auto vt : allValueTypes) {
        const char* str = behaviortree::blackboardValueTypeToString(vt);
        REQUIRE(str != nullptr);
        CHECK(behaviortree::stringToBlackboardValueType(str) == vt);
    }
}

// ---- String <-> Enum: compareOpToString / stringToCompareOp ----

TEST_CASE("String/Enum roundtrip: compareOpToString and stringToCompareOp") {
    const behaviortree::CompareOp allOps[] = {
        behaviortree::CompareOp::Equal,
        behaviortree::CompareOp::NotEqual,
        behaviortree::CompareOp::Greater,
        behaviortree::CompareOp::Less,
        behaviortree::CompareOp::GreaterEqual,
        behaviortree::CompareOp::LessEqual
    };

    for (auto op : allOps) {
        const char* str = behaviortree::compareOpToString(op);
        REQUIRE(str != nullptr);
        CHECK(behaviortree::stringToCompareOp(str) == op);
    }
}

// ---- String <-> Enum: abortModeToString / stringToAbortMode ----

TEST_CASE("String/Enum roundtrip: abortModeToString and stringToAbortMode") {
    const behaviortree::AbortMode allModes[] = {
        behaviortree::AbortMode::None,
        behaviortree::AbortMode::Self,
        behaviortree::AbortMode::LowerPriority,
        behaviortree::AbortMode::Both
    };

    for (auto mode : allModes) {
        const char* str = behaviortree::abortModeToString(mode);
        REQUIRE(str != nullptr);
        CHECK(behaviortree::stringToAbortMode(str) == mode);
    }
}

TEST_CASE("Node classification: BlackboardCondition is a decorator") {
    CHECK(behaviortree::isDecoratorNode(behaviortree::BTNodeType::BlackboardCondition) == true);
    CHECK(behaviortree::isTaskNode(behaviortree::BTNodeType::BlackboardCondition) == false);
    CHECK(behaviortree::isCompositeNode(behaviortree::BTNodeType::BlackboardCondition) == false);
}

// ---- BlackboardCondition runtime semantics ----

namespace {

// Records aborts; long-running tasks are simulated with Wait nodes so no executor calls matter
struct AbortRecorder : behaviortree::IBTTaskExecutor {
    std::vector<std::pair<uint32_t, behaviortree::BTNodeType>> aborts;

    behaviortree::BTNodeStatus executeMoveTo(services::EntityHandle, const std::string&, float,
                                             behaviortree::Blackboard&, bool) override {
        return behaviortree::BTNodeStatus::Running;
    }
    behaviortree::BTNodeStatus executePlayAnimation(services::EntityHandle, const std::string&, bool) override {
        return behaviortree::BTNodeStatus::Success;
    }
    behaviortree::BTNodeStatus executeScriptTask(services::EntityHandle, const std::string&, const std::string&,
                                                 behaviortree::Blackboard&, float) override {
        return behaviortree::BTNodeStatus::Success;
    }
    behaviortree::BTNodeStatus executeLog(const std::string&, behaviortree::LogLevel) override {
        return behaviortree::BTNodeStatus::Success;
    }
    behaviortree::BTNodeStatus executeEnvironmentQuery(services::EntityHandle, const std::string&,
                                                       const std::string&, behaviortree::Blackboard&, bool) override {
        return behaviortree::BTNodeStatus::Running;
    }
    behaviortree::BTNodeStatus executeLineOfSight(services::EntityHandle, const std::string&, float, float,
                                                  behaviortree::Blackboard&) override {
        return behaviortree::BTNodeStatus::Success;
    }
    void onAbort(services::EntityHandle, const behaviortree::BTNode& node) override {
        aborts.emplace_back(node.id, node.type);
    }
};

behaviortree::BTNode makeBTNode(uint32_t id, behaviortree::BTNodeType type) {
    behaviortree::BTNode node;
    node.id = id;
    node.type = type;
    node.name = behaviortree::nodeTypeToString(type);
    return node;
}

void linkBTNodes(behaviortree::BTGraph& graph, uint32_t source, uint32_t target, uint32_t sortOrder) {
    behaviortree::BTLink link;
    link.id = graph.nextLinkId++;
    link.sourceNodeId = source;
    link.targetNodeId = target;
    link.sortOrder = sortOrder;
    graph.links.push_back(link);
}

behaviortree::BTNode makeConditionNode(uint32_t id, const std::string& key, const std::string& abortMode) {
    auto node = makeBTNode(id, behaviortree::BTNodeType::BlackboardCondition);
    node.properties["key"] = key;
    node.properties["compareOp"] = std::string("==");
    node.properties["compareValue"] = true;
    node.properties["abortMode"] = abortMode;
    return node;
}

behaviortree::BTNode makeWaitNode(uint32_t id, float duration) {
    auto node = makeBTNode(id, behaviortree::BTNodeType::Wait);
    node.properties["duration"] = duration;
    return node;
}

// root(1) -> condition(2, guards key) -> wait(3)
behaviortree::BehaviorTreeData makeGuardedWaitTree(const std::string& abortMode, bool keyDefault) {
    behaviortree::BehaviorTreeData data;
    data.name = "GuardedWait";
    data.graph.rootNodeId = 1;
    data.graph.nodes.push_back(makeBTNode(1, behaviortree::BTNodeType::Root));
    data.graph.nodes.push_back(makeConditionNode(2, "ok", abortMode));
    data.graph.nodes.push_back(makeWaitNode(3, 10.0f));
    linkBTNodes(data.graph, 1, 2, 0);
    linkBTNodes(data.graph, 2, 3, 0);
    data.graph.blackboardKeys.push_back({"ok", behaviortree::BlackboardValueType::Bool, keyDefault});
    return data;
}

// root(1) -> selector(2) -> [condition(3, guards "enemy") -> wait(4)] | [wait(5)]
behaviortree::BehaviorTreeData makeSelectorTree(const std::string& abortMode) {
    behaviortree::BehaviorTreeData data;
    data.name = "GuardedSelector";
    data.graph.rootNodeId = 1;
    data.graph.nodes.push_back(makeBTNode(1, behaviortree::BTNodeType::Root));
    data.graph.nodes.push_back(makeBTNode(2, behaviortree::BTNodeType::Selector));
    data.graph.nodes.push_back(makeConditionNode(3, "enemy", abortMode));
    data.graph.nodes.push_back(makeWaitNode(4, 10.0f));
    data.graph.nodes.push_back(makeWaitNode(5, 10.0f));
    linkBTNodes(data.graph, 1, 2, 0);
    linkBTNodes(data.graph, 2, 3, 0);
    linkBTNodes(data.graph, 2, 5, 1);
    linkBTNodes(data.graph, 3, 4, 0);
    data.graph.blackboardKeys.push_back({"enemy", behaviortree::BlackboardValueType::Bool, false});
    return data;
}

} // namespace

TEST_CASE("BlackboardCondition: gates child on entry") {
    behaviortree::BehaviorTreeRuntime runtime;
    runtime.init(std::make_shared<behaviortree::BehaviorTreeData>(makeGuardedWaitTree("None", false)), services::EntityHandle{});
    AbortRecorder executor;

    // Condition false: child must not run
    CHECK(runtime.tick(0.1f, &executor) == behaviortree::BTNodeStatus::Failure);
    CHECK(executor.aborts.empty());

    // Condition true: child runs
    runtime.getBlackboard().set("ok", true);
    CHECK(runtime.tick(0.1f, &executor) == behaviortree::BTNodeStatus::Running);
}

TEST_CASE("BlackboardCondition: None mode keeps running after condition turns false") {
    behaviortree::BehaviorTreeRuntime runtime;
    runtime.init(std::make_shared<behaviortree::BehaviorTreeData>(makeGuardedWaitTree("None", true)), services::EntityHandle{});
    AbortRecorder executor;

    CHECK(runtime.tick(0.1f, &executor) == behaviortree::BTNodeStatus::Running);

    // None mode does not observe while running: child keeps running
    runtime.getBlackboard().set("ok", false);
    CHECK(runtime.tick(0.1f, &executor) == behaviortree::BTNodeStatus::Running);
    CHECK(executor.aborts.empty());
}

TEST_CASE("BlackboardCondition: Self abort cancels running subtree") {
    behaviortree::BehaviorTreeRuntime runtime;
    runtime.init(std::make_shared<behaviortree::BehaviorTreeData>(makeGuardedWaitTree("Self", true)), services::EntityHandle{});
    AbortRecorder executor;

    CHECK(runtime.tick(0.1f, &executor) == behaviortree::BTNodeStatus::Running);
    CHECK(executor.aborts.empty());

    runtime.getBlackboard().set("ok", false);
    CHECK(runtime.tick(0.1f, &executor) == behaviortree::BTNodeStatus::Failure);
    REQUIRE(executor.aborts.size() == 1);
    CHECK(executor.aborts[0].first == 3);
    CHECK(executor.aborts[0].second == behaviortree::BTNodeType::Wait);

    // Abort fires exactly once
    CHECK(runtime.tick(0.1f, &executor) == behaviortree::BTNodeStatus::Failure);
    CHECK(executor.aborts.size() == 1);
}

TEST_CASE("Selector observer abort: LowerPriority preempts running branch") {
    behaviortree::BehaviorTreeRuntime runtime;
    runtime.init(std::make_shared<behaviortree::BehaviorTreeData>(makeSelectorTree("LowerPriority")), services::EntityHandle{});
    AbortRecorder executor;

    // Guard fails -> selector falls through to the low-priority wait
    CHECK(runtime.tick(0.1f, &executor) == behaviortree::BTNodeStatus::Running);
    CHECK(executor.aborts.empty());

    // Guard passes -> running low-priority branch is aborted, guarded branch takes over
    runtime.getBlackboard().set("enemy", true);
    CHECK(runtime.tick(0.1f, &executor) == behaviortree::BTNodeStatus::Running);
    REQUIRE(executor.aborts.size() == 1);
    CHECK(executor.aborts[0].first == 5);

    // Stable afterwards: no repeated aborts
    CHECK(runtime.tick(0.1f, &executor) == behaviortree::BTNodeStatus::Running);
    CHECK(executor.aborts.size() == 1);
}

TEST_CASE("Selector observer abort: None mode never preempts") {
    behaviortree::BehaviorTreeRuntime runtime;
    runtime.init(std::make_shared<behaviortree::BehaviorTreeData>(makeSelectorTree("None")), services::EntityHandle{});
    AbortRecorder executor;

    CHECK(runtime.tick(0.1f, &executor) == behaviortree::BTNodeStatus::Running);

    runtime.getBlackboard().set("enemy", true);
    CHECK(runtime.tick(0.1f, &executor) == behaviortree::BTNodeStatus::Running);
    CHECK(executor.aborts.empty());
}

TEST_CASE("Selector observer abort: Both preempts and self-aborts") {
    behaviortree::BehaviorTreeRuntime runtime;
    runtime.init(std::make_shared<behaviortree::BehaviorTreeData>(makeSelectorTree("Both")), services::EntityHandle{});
    AbortRecorder executor;

    // Fall through to low-priority branch
    CHECK(runtime.tick(0.1f, &executor) == behaviortree::BTNodeStatus::Running);

    // Preempt it
    runtime.getBlackboard().set("enemy", true);
    CHECK(runtime.tick(0.1f, &executor) == behaviortree::BTNodeStatus::Running);
    REQUIRE(executor.aborts.size() == 1);
    CHECK(executor.aborts[0].first == 5);

    // Condition turns false -> Self observation aborts the guarded wait, selector falls through again
    runtime.getBlackboard().set("enemy", false);
    CHECK(runtime.tick(0.1f, &executor) == behaviortree::BTNodeStatus::Running);
    REQUIRE(executor.aborts.size() == 2);
    CHECK(executor.aborts[1].first == 4);
}

// ---- Shared tree data ----

TEST_CASE("Shared tree data: runtimes share one asset but keep independent state") {
    auto shared = std::make_shared<const behaviortree::BehaviorTreeData>(makeGuardedWaitTree("None", true));
    AbortRecorder executor;

    behaviortree::BehaviorTreeRuntime runtimeA;
    behaviortree::BehaviorTreeRuntime runtimeB;
    runtimeA.init(shared, services::EntityHandle{});
    runtimeB.init(shared, services::EntityHandle{});

    // Same immutable data instance backs both runtimes
    CHECK(&runtimeA.getTreeData() == &runtimeB.getTreeData());

    // Per-agent blackboards stay independent
    runtimeB.getBlackboard().set("ok", false);
    CHECK(runtimeA.tick(0.1f, &executor) == behaviortree::BTNodeStatus::Running);
    CHECK(runtimeB.tick(0.1f, &executor) == behaviortree::BTNodeStatus::Failure);

    // Re-init with the same shared data resets state without touching the other runtime
    runtimeA.init(shared, services::EntityHandle{});
    CHECK(runtimeA.getBlackboard().getBool("ok") == true);
    CHECK(runtimeB.getBlackboard().getBool("ok") == false);
}

// ---- SubTree expansion ----

namespace {

behaviortree::BTNode makeSubTreeNode(uint32_t id, const std::string& treePath) {
    auto node = makeBTNode(id, behaviortree::BTNodeType::SubTree);
    node.properties["treePath"] = treePath;
    return node;
}

// root(1) -> selector(2) -> wait(3); declares childKey + shared
behaviortree::BehaviorTreeData makeChildTree() {
    behaviortree::BehaviorTreeData data;
    data.name = "Child";
    data.graph.rootNodeId = 1;
    data.graph.nodes.push_back(makeBTNode(1, behaviortree::BTNodeType::Root));
    data.graph.nodes.push_back(makeBTNode(2, behaviortree::BTNodeType::Selector));
    data.graph.nodes.push_back(makeWaitNode(3, 10.0f));
    linkBTNodes(data.graph, 1, 2, 0);
    linkBTNodes(data.graph, 2, 3, 0);
    data.graph.blackboardKeys.push_back({"childKey", behaviortree::BlackboardValueType::Float, 5.0f});
    data.graph.blackboardKeys.push_back({"shared", behaviortree::BlackboardValueType::Int, 7});
    return data;
}

// root(1) -> sequence(2) -> [wait(3), subtree(4 -> "child.bt")]; declares shared
behaviortree::BehaviorTreeData makeParentTree() {
    behaviortree::BehaviorTreeData data;
    data.name = "Parent";
    data.graph.rootNodeId = 1;
    data.graph.nodes.push_back(makeBTNode(1, behaviortree::BTNodeType::Root));
    data.graph.nodes.push_back(makeBTNode(2, behaviortree::BTNodeType::Sequence));
    data.graph.nodes.push_back(makeWaitNode(3, 0.05f));
    data.graph.nodes.push_back(makeSubTreeNode(4, "child.bt"));
    linkBTNodes(data.graph, 1, 2, 0);
    linkBTNodes(data.graph, 2, 3, 0);
    linkBTNodes(data.graph, 2, 4, 1);
    data.graph.blackboardKeys.push_back({"shared", behaviortree::BlackboardValueType::Int, 1});
    return data;
}

int countNodesOfType(const behaviortree::BTGraph& graph, behaviortree::BTNodeType type) {
    int count = 0;
    for (const auto& node : graph.nodes)
        if (node.type == type) ++count;
    return count;
}

} // namespace

TEST_CASE("SubTree expansion: splices child graph with remapped ids") {
    auto parent = makeParentTree();
    auto loader = [](const std::string& path) -> std::optional<behaviortree::BehaviorTreeData> {
        if (path == "child.bt") return makeChildTree();
        return std::nullopt;
    };

    REQUIRE(behaviortree::BehaviorTreeAsset::expandSubTrees(parent, loader));

    // SubTree node is gone, child content (minus its Root) is in
    CHECK(countNodesOfType(parent.graph, behaviortree::BTNodeType::SubTree) == 0);
    CHECK(countNodesOfType(parent.graph, behaviortree::BTNodeType::Selector) == 1);
    CHECK(countNodesOfType(parent.graph, behaviortree::BTNodeType::Root) == 1);
    CHECK(countNodesOfType(parent.graph, behaviortree::BTNodeType::Wait) == 2);

    // The sequence's second child is now the spliced selector, which still owns its wait
    auto sequenceChildren = parent.graph.getChildren(2);
    REQUIRE(sequenceChildren.size() == 2);
    CHECK(sequenceChildren[1]->type == behaviortree::BTNodeType::Selector);
    auto selectorChildren = parent.graph.getChildren(sequenceChildren[1]->id);
    REQUIRE(selectorChildren.size() == 1);
    CHECK(selectorChildren[0]->type == behaviortree::BTNodeType::Wait);

    // Blackboard merge: child-only key added, collision keeps the parent default
    REQUIRE(parent.graph.blackboardKeys.size() == 2);
    bool foundChildKey = false;
    for (const auto& keyDef : parent.graph.blackboardKeys) {
        if (keyDef.name == "childKey") foundChildKey = true;
        if (keyDef.name == "shared") {
            REQUIRE(std::holds_alternative<int32_t>(keyDef.defaultValue));
            CHECK(std::get<int32_t>(keyDef.defaultValue) == 1);
        }
    }
    CHECK(foundChildKey);

    // The expanded tree actually runs: wait(0.05) succeeds, then the spliced selector runs
    behaviortree::BehaviorTreeRuntime runtime;
    runtime.init(std::make_shared<behaviortree::BehaviorTreeData>(std::move(parent)), services::EntityHandle{});
    AbortRecorder executor;
    CHECK(runtime.tick(0.1f, &executor) == behaviortree::BTNodeStatus::Running);
}

TEST_CASE("SubTree expansion: cyclic references are rejected") {
    behaviortree::BehaviorTreeData treeA;
    treeA.graph.rootNodeId = 1;
    treeA.graph.nodes.push_back(makeBTNode(1, behaviortree::BTNodeType::Root));
    treeA.graph.nodes.push_back(makeSubTreeNode(2, "b.bt"));
    linkBTNodes(treeA.graph, 1, 2, 0);

    auto loader = [](const std::string& path) -> std::optional<behaviortree::BehaviorTreeData> {
        behaviortree::BehaviorTreeData data;
        data.graph.rootNodeId = 1;
        data.graph.nodes.push_back(makeBTNode(1, behaviortree::BTNodeType::Root));
        data.graph.nodes.push_back(makeSubTreeNode(2, path == "b.bt" ? "a.bt" : "b.bt"));
        linkBTNodes(data.graph, 1, 2, 0);
        return data;
    };

    CHECK_FALSE(behaviortree::BehaviorTreeAsset::expandSubTrees(treeA, loader));
}

TEST_CASE("SubTree expansion: missing reference fails") {
    auto parent = makeParentTree();
    auto loader = [](const std::string&) -> std::optional<behaviortree::BehaviorTreeData> {
        return std::nullopt;
    };

    CHECK_FALSE(behaviortree::BehaviorTreeAsset::expandSubTrees(parent, loader));
}

TEST_CASE("Node classification: SubTree is a task (leaf)") {
    CHECK(behaviortree::isTaskNode(behaviortree::BTNodeType::SubTree) == true);
    CHECK(behaviortree::hasOutputPin(behaviortree::BTNodeType::SubTree) == false);
}

} // TEST_SUITE
