#include <doctest.h>
#include <behaviortree/BehaviorTreeTypes.hpp>
#include <behaviortree/BehaviorTreeRuntime.hpp>
#include <behaviortree/BehaviorTreeAsset.hpp>
#include <behaviortree/BehaviorTreeValidation.hpp>
#include <behaviortree/BehaviorTreeDynamic.hpp>
#include <behaviortree/Blackboard.hpp>
#include <unordered_map>
#include <string>
#include <vector>
#include <utility>
#include <memory>
#include <algorithm>

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
        behaviortree::BTNodeType::SubTree,
        behaviortree::BTNodeType::Service,
        behaviortree::BTNodeType::DynamicSubTree
    };

    for (auto type : allTypes) {
        const char* str = behaviortree::nodeTypeToString(type);
        REQUIRE(str != nullptr);
        CHECK(behaviortree::stringToNodeType(str) == type);
    }
}

// ---- String <-> Enum: mappingDirectionToString / stringToMappingDirection (VK-1457) ----

TEST_CASE("String/Enum roundtrip: mappingDirectionToString and stringToMappingDirection") {
    const behaviortree::MappingDirection allDirs[] = {
        behaviortree::MappingDirection::In,
        behaviortree::MappingDirection::Out,
        behaviortree::MappingDirection::InOut
    };

    for (auto dir : allDirs) {
        const char* str = behaviortree::mappingDirectionToString(dir);
        REQUIRE(str != nullptr);
        CHECK(behaviortree::stringToMappingDirection(str) == dir);
    }
    // Unknown falls back to In (mirrors the other enum converters).
    CHECK(behaviortree::stringToMappingDirection("bogus") == behaviortree::MappingDirection::In);
}

// ---- Node classification: DynamicSubTree is a task (leaf) (VK-1457) ----

TEST_CASE("Node classification: DynamicSubTree is a task leaf with no output pin") {
    CHECK(behaviortree::isTaskNode(behaviortree::BTNodeType::DynamicSubTree) == true);
    CHECK(behaviortree::isCompositeNode(behaviortree::BTNodeType::DynamicSubTree) == false);
    CHECK(behaviortree::isDecoratorNode(behaviortree::BTNodeType::DynamicSubTree) == false);
    CHECK(behaviortree::isServiceNode(behaviortree::BTNodeType::DynamicSubTree) == false);
    // Must have no output pin, else the editor lets it take children and abort won't fire.
    CHECK(behaviortree::hasOutputPin(behaviortree::BTNodeType::DynamicSubTree) == false);
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

// Manually-built graphs set node IDs directly, so bring nextNodeId up to the same
// invariant the JSON loader and editor maintain (nextNodeId > every node ID).
void finalizeGraphIds(behaviortree::BTGraph& graph) {
    for (const auto& node : graph.nodes)
        graph.nextNodeId = std::max(graph.nextNodeId, node.id + 1);
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
    finalizeGraphIds(data.graph);
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
    finalizeGraphIds(data.graph);
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

// ============================================================
// VK-1456: reactive blackboard + branch-scoped services
// ============================================================

// ---- Blackboard key-change versioning ----

TEST_CASE("Blackboard: version semantics") {
    behaviortree::Blackboard bb;
    CHECK(bb.getVersion("k") == 0); // absent -> 0

    bb.set("k", 1.0f);
    uint64_t v1 = bb.getVersion("k");
    CHECK(v1 != 0);

    // get() does not bump the version
    (void)bb.get("k");
    CHECK(bb.getVersion("k") == v1);

    // set-to-same-value still bumps (no false negatives for observers)
    bb.set("k", 1.0f);
    uint64_t v2 = bb.getVersion("k");
    CHECK(v2 > v1);

    // remove drops the version back to 0 (absence is observable)
    bb.remove("k");
    CHECK(bb.getVersion("k") == 0);

    bb.set("a", 1.0f);
    bb.set("b", 2.0f);
    bb.clear();
    CHECK(bb.getVersion("a") == 0);
    CHECK(bb.getVersion("b") == 0);
}

TEST_CASE("Blackboard: initializeFromGraph stamps non-zero versions") {
    // Regression guard: a default key left present-with-version-0 would let a later remove()/clear()
    // flip has() true->false without a version change, leaving a stale cached observer result.
    behaviortree::BTGraph graph;
    graph.blackboardKeys.push_back({"ok", behaviortree::BlackboardValueType::Bool, true});

    behaviortree::Blackboard bb;
    bb.initializeFromGraph(graph);
    CHECK(bb.has("ok"));
    CHECK(bb.getVersion("ok") != 0);

    bb.remove("ok");
    CHECK(bb.getVersion("ok") == 0);
}

TEST_CASE("BlackboardCondition Self: version-gated observation reacts to real changes only") {
    behaviortree::BehaviorTreeRuntime runtime;
    runtime.init(std::make_shared<behaviortree::BehaviorTreeData>(makeGuardedWaitTree("Self", true)), services::EntityHandle{});
    AbortRecorder executor;

    CHECK(runtime.tick(0.1f, &executor) == behaviortree::BTNodeStatus::Running);
    CHECK(executor.aborts.empty());

    // Re-setting the same value bumps the version but the condition stays true: no spurious abort
    runtime.getBlackboard().set("ok", true);
    CHECK(runtime.tick(0.1f, &executor) == behaviortree::BTNodeStatus::Running);
    CHECK(executor.aborts.empty());

    // A real change aborts exactly once
    runtime.getBlackboard().set("ok", false);
    CHECK(runtime.tick(0.1f, &executor) == behaviortree::BTNodeStatus::Failure);
    REQUIRE(executor.aborts.size() == 1);

    // Stable while unchanged: no repeated aborts
    CHECK(runtime.tick(0.1f, &executor) == behaviortree::BTNodeStatus::Failure);
    CHECK(executor.aborts.size() == 1);
}

// ---- Service nodes ----

namespace {

// Records service lifecycle calls; inherits the pure-virtual task impls + onAbort from AbortRecorder.
struct ServiceRecorder : AbortRecorder {
    std::vector<std::pair<uint32_t, std::string>> events;

    void onServiceStart(services::EntityHandle, const behaviortree::BTNode& node, behaviortree::Blackboard&) override {
        events.emplace_back(node.id, std::string("start"));
    }
    void onServiceTick(services::EntityHandle, const behaviortree::BTNode& node, behaviortree::Blackboard&, float) override {
        events.emplace_back(node.id, std::string("tick"));
    }
    void onServiceEnd(services::EntityHandle, const behaviortree::BTNode& node, behaviortree::Blackboard&) override {
        events.emplace_back(node.id, std::string("end"));
    }

    int count(const std::string& kind) const {
        int c = 0;
        for (const auto& e : events) if (e.second == kind) ++c;
        return c;
    }
};

behaviortree::BTNode makeServiceNode(uint32_t id, const std::string& serviceType,
                                     float interval, float deviation, bool runOnActivation) {
    auto node = makeBTNode(id, behaviortree::BTNodeType::Service);
    node.properties["serviceType"] = serviceType;
    node.properties["interval"] = interval;
    node.properties["randomDeviation"] = deviation;
    node.properties["runOnActivation"] = runOnActivation;
    return node;
}

// root(1) -> service(2) -> wait(3, effectively forever)
behaviortree::BehaviorTreeData makeServiceTree(float interval, float deviation, bool runOnActivation) {
    behaviortree::BehaviorTreeData data;
    data.name = "ServiceTree";
    data.graph.rootNodeId = 1;
    data.graph.nodes.push_back(makeBTNode(1, behaviortree::BTNodeType::Root));
    data.graph.nodes.push_back(makeServiceNode(2, "EQSRefresh", interval, deviation, runOnActivation));
    data.graph.nodes.push_back(makeWaitNode(3, 1000.0f));
    linkBTNodes(data.graph, 1, 2, 0);
    linkBTNodes(data.graph, 2, 3, 0);
    finalizeGraphIds(data.graph);
    return data;
}

} // namespace

TEST_CASE("Node classification: Service is its own category with an output pin") {
    using namespace behaviortree;
    CHECK(isServiceNode(BTNodeType::Service));
    CHECK_FALSE(isTaskNode(BTNodeType::Service));
    CHECK_FALSE(isDecoratorNode(BTNodeType::Service));
    CHECK_FALSE(isCompositeNode(BTNodeType::Service));
    CHECK(hasOutputPin(BTNodeType::Service)); // not a task -> keeps a child pin
    CHECK(std::string(nodeTypeToString(BTNodeType::Service)) == "Service");
    CHECK(stringToNodeType("Service") == BTNodeType::Service);
}

TEST_CASE("Service: fires at a deterministic fixed interval while its branch is active") {
    behaviortree::BehaviorTreeRuntime runtime;
    runtime.init(std::make_shared<behaviortree::BehaviorTreeData>(makeServiceTree(0.5f, 0.0f, false)), services::EntityHandle{});
    ServiceRecorder executor;

    // 20 ticks of 0.1s -> fires at cumulative 0.5/1.0/1.5/2.0 = 4 fires (runOnActivation off)
    for (int i = 0; i < 20; ++i)
        CHECK(runtime.tick(0.1f, &executor) == behaviortree::BTNodeStatus::Running);

    CHECK(executor.count("start") == 1);
    CHECK(executor.count("tick") == 4);
    CHECK(executor.count("end") == 0); // still active (child never completes)
}

TEST_CASE("Service: runOnActivation fires immediately, otherwise first fire is after one interval") {
    SUBCASE("runOnActivation = true") {
        behaviortree::BehaviorTreeRuntime runtime;
        runtime.init(std::make_shared<behaviortree::BehaviorTreeData>(makeServiceTree(0.5f, 0.0f, true)), services::EntityHandle{});
        ServiceRecorder executor;
        runtime.tick(0.1f, &executor); // activation frame
        CHECK(executor.count("start") == 1);
        CHECK(executor.count("tick") == 1);
    }
    SUBCASE("runOnActivation = false") {
        behaviortree::BehaviorTreeRuntime runtime;
        runtime.init(std::make_shared<behaviortree::BehaviorTreeData>(makeServiceTree(0.5f, 0.0f, false)), services::EntityHandle{});
        ServiceRecorder executor;
        for (int i = 0; i < 4; ++i) runtime.tick(0.1f, &executor); // cumulative 0.4 < 0.5
        CHECK(executor.count("tick") == 0);
        runtime.tick(0.1f, &executor); // cumulative 0.5 -> first fire
        CHECK(executor.count("tick") == 1);
    }
}

TEST_CASE("Service: interval sequence is deterministic across runtimes with the same ids") {
    auto data = std::make_shared<const behaviortree::BehaviorTreeData>(makeServiceTree(0.5f, 0.2f, false));
    behaviortree::BehaviorTreeRuntime ra, rb;
    ra.init(data, services::EntityHandle{7});
    rb.init(data, services::EntityHandle{7});
    ServiceRecorder a, b;

    // Lockstep: identical (entity id, node id) -> identical fire timing every tick
    for (int i = 0; i < 50; ++i) {
        ra.tick(0.1f, &a);
        rb.tick(0.1f, &b);
        CHECK(a.count("tick") == b.count("tick"));
    }
    CHECK(a.count("tick") > 0);
}

TEST_CASE("Service: stops with onServiceEnd exactly once when its branch self-aborts") {
    // root(1) -> condition(2,"go",Self) -> service(3) -> wait(4, forever)
    behaviortree::BehaviorTreeData data;
    data.name = "SelfAbortService";
    data.graph.rootNodeId = 1;
    data.graph.nodes.push_back(makeBTNode(1, behaviortree::BTNodeType::Root));
    data.graph.nodes.push_back(makeConditionNode(2, "go", "Self"));
    data.graph.nodes.push_back(makeServiceNode(3, "EQSRefresh", 0.5f, 0.0f, true));
    data.graph.nodes.push_back(makeWaitNode(4, 1000.0f));
    linkBTNodes(data.graph, 1, 2, 0);
    linkBTNodes(data.graph, 2, 3, 0);
    linkBTNodes(data.graph, 3, 4, 0);
    data.graph.blackboardKeys.push_back({"go", behaviortree::BlackboardValueType::Bool, true});
    finalizeGraphIds(data.graph);

    behaviortree::BehaviorTreeRuntime runtime;
    runtime.init(std::make_shared<behaviortree::BehaviorTreeData>(std::move(data)), services::EntityHandle{});
    ServiceRecorder executor;

    CHECK(runtime.tick(0.1f, &executor) == behaviortree::BTNodeStatus::Running);
    CHECK(executor.count("start") == 1);
    CHECK(executor.count("end") == 0);

    runtime.getBlackboard().set("go", false);
    CHECK(runtime.tick(0.1f, &executor) == behaviortree::BTNodeStatus::Failure);
    CHECK(executor.count("end") == 1); // deactivated the same frame the branch was aborted

    // No further start/end churn
    CHECK(runtime.tick(0.1f, &executor) == behaviortree::BTNodeStatus::Failure);
    CHECK(executor.count("start") == 1);
    CHECK(executor.count("end") == 1);
}

TEST_CASE("Service: deactivates the first frame after its Sequence branch advances") {
    // root(1) -> sequence(2) -> [ service(3)->wait(4, short), wait(5, forever) ]
    behaviortree::BehaviorTreeData data;
    data.name = "SequenceAdvanceService";
    data.graph.rootNodeId = 1;
    data.graph.nodes.push_back(makeBTNode(1, behaviortree::BTNodeType::Root));
    data.graph.nodes.push_back(makeBTNode(2, behaviortree::BTNodeType::Sequence));
    data.graph.nodes.push_back(makeServiceNode(3, "EQSRefresh", 0.5f, 0.0f, false));
    data.graph.nodes.push_back(makeWaitNode(4, 0.05f));
    data.graph.nodes.push_back(makeWaitNode(5, 1000.0f));
    linkBTNodes(data.graph, 1, 2, 0);
    linkBTNodes(data.graph, 2, 3, 0);
    linkBTNodes(data.graph, 2, 5, 1);
    linkBTNodes(data.graph, 3, 4, 0);
    finalizeGraphIds(data.graph);

    behaviortree::BehaviorTreeRuntime runtime;
    runtime.init(std::make_shared<behaviortree::BehaviorTreeData>(std::move(data)), services::EntityHandle{});
    ServiceRecorder executor;

    // Frame 1: wait(4) completes -> sequence advances to wait(5); service was still visited this frame
    CHECK(runtime.tick(0.1f, &executor) == behaviortree::BTNodeStatus::Running);
    CHECK(executor.count("start") == 1);
    CHECK(executor.count("end") == 0);

    // Frame 2: service branch no longer visited -> ends on the first not-visited frame
    CHECK(runtime.tick(0.1f, &executor) == behaviortree::BTNodeStatus::Running);
    CHECK(executor.count("end") == 1);
}

TEST_CASE("Service: endAllServices flushes active services (detach/reset path)") {
    behaviortree::BehaviorTreeRuntime runtime;
    runtime.init(std::make_shared<behaviortree::BehaviorTreeData>(makeServiceTree(0.5f, 0.0f, true)), services::EntityHandle{});
    ServiceRecorder executor;

    runtime.tick(0.1f, &executor);
    CHECK(executor.count("start") == 1);
    CHECK(executor.count("end") == 0);

    runtime.endAllServices(&executor);
    CHECK(executor.count("end") == 1);

    // Idempotent: nothing left to end
    runtime.endAllServices(&executor);
    CHECK(executor.count("end") == 1);
}

// ---- Service validation ----

namespace {

behaviortree::BehaviorTreeData makeServiceValidationTree(const behaviortree::BTNode& service, bool withChild) {
    behaviortree::BehaviorTreeData data;
    data.graph.rootNodeId = 1;
    data.graph.nodes.push_back(makeBTNode(1, behaviortree::BTNodeType::Root));
    data.graph.nodes.push_back(service);
    linkBTNodes(data.graph, 1, service.id, 0);
    if (withChild) {
        data.graph.nodes.push_back(makeWaitNode(3, 1.0f));
        linkBTNodes(data.graph, service.id, 3, 0);
    }
    data.graph.blackboardKeys.push_back({"eqsResult", behaviortree::BlackboardValueType::Vec3, glm::vec3{0.0f}});
    finalizeGraphIds(data.graph);
    return data;
}

} // namespace

TEST_CASE("Validation: Service node rules") {
    using namespace behaviortree;

    SUBCASE("valid service passes") {
        auto svc = makeServiceNode(2, "EQSRefresh", 0.5f, 0.0f, false);
        svc.properties["resultKey"] = std::string("eqsResult");
        auto report = validation::validateBehaviorTree(makeServiceValidationTree(svc, true));
        CHECK_FALSE(report.hasErrors());
    }

    SUBCASE("no child is an error") {
        auto svc = makeServiceNode(2, "EQSRefresh", 0.5f, 0.0f, false);
        svc.properties["resultKey"] = std::string("eqsResult");
        auto report = validation::validateBehaviorTree(makeServiceValidationTree(svc, false));
        CHECK(report.hasErrors());
    }

    SUBCASE("non-positive interval is an error") {
        auto svc = makeServiceNode(2, "EQSRefresh", 0.0f, 0.0f, false);
        svc.properties["resultKey"] = std::string("eqsResult");
        auto report = validation::validateBehaviorTree(makeServiceValidationTree(svc, true));
        CHECK(report.hasErrors());
    }

    SUBCASE("unknown service type is a warning") {
        auto svc = makeServiceNode(2, "Bogus", 0.5f, 0.0f, false);
        auto report = validation::validateBehaviorTree(makeServiceValidationTree(svc, true));
        CHECK(report.warningCount() > 0);
    }

    SUBCASE("deviation >= interval is a warning") {
        auto svc = makeServiceNode(2, "EQSRefresh", 0.5f, 0.5f, false);
        svc.properties["resultKey"] = std::string("eqsResult");
        auto report = validation::validateBehaviorTree(makeServiceValidationTree(svc, true));
        CHECK_FALSE(report.hasErrors());
        CHECK(report.warningCount() > 0);
    }
}

// ============================================================
// VK-1457: dynamic-subtree pure helpers (BehaviorTreeDynamic.hpp)
// ============================================================

TEST_CASE("resolveDynamicSubtreePath: precedence blackboard > injection > default") {
    behaviortree::BTNode node;
    node.type = behaviortree::BTNodeType::DynamicSubTree;
    node.properties["selectionKey"] = std::string("brain");
    node.properties["injectionTag"] = std::string("combat");
    node.properties["defaultTreePath"] = std::string("default.vfBehaviorTree");

    behaviortree::Blackboard bb;
    std::unordered_map<std::string, std::string> injections;

    SUBCASE("falls back to default when nothing else is set") {
        CHECK(behaviortree::resolveDynamicSubtreePath(node, bb, injections) == "default.vfBehaviorTree");
    }
    SUBCASE("injection tag beats default") {
        injections["combat"] = "combat.vfBehaviorTree";
        CHECK(behaviortree::resolveDynamicSubtreePath(node, bb, injections) == "combat.vfBehaviorTree");
    }
    SUBCASE("blackboard selection key beats injection and default") {
        injections["combat"] = "combat.vfBehaviorTree";
        bb.set("brain", std::string("chosen.vfBehaviorTree"));
        CHECK(behaviortree::resolveDynamicSubtreePath(node, bb, injections) == "chosen.vfBehaviorTree");
    }
    SUBCASE("empty blackboard string does not win") {
        bb.set("brain", std::string(""));
        injections["combat"] = "combat.vfBehaviorTree";
        CHECK(behaviortree::resolveDynamicSubtreePath(node, bb, injections) == "combat.vfBehaviorTree");
    }
    SUBCASE("non-string blackboard value is ignored") {
        bb.set("brain", 42);
        CHECK(behaviortree::resolveDynamicSubtreePath(node, bb, injections) == "default.vfBehaviorTree");
    }
}

TEST_CASE("applyMappingsIn / applyMappingsOut copy across isolated blackboards by direction") {
    std::vector<behaviortree::BlackboardMapping> mappings = {
        {"pTarget", "cTarget", behaviortree::MappingDirection::In},
        {"pResult", "cResult", behaviortree::MappingDirection::Out},
        {"pShared", "cShared", behaviortree::MappingDirection::InOut},
    };

    behaviortree::Blackboard parent;
    behaviortree::Blackboard child;
    parent.set("pTarget", glm::vec3{1.0f, 2.0f, 3.0f});
    parent.set("pShared", 10);
    parent.set("pResult", 0); // should NOT be copied inbound

    behaviortree::applyMappingsIn(mappings, parent, child);
    // In + InOut copied parent -> child
    CHECK(child.has("cTarget"));
    CHECK(child.getVec3("cTarget") == glm::vec3{1.0f, 2.0f, 3.0f});
    CHECK(child.getInt("cShared") == 10);
    // Out-only mapping did NOT flow inbound
    CHECK_FALSE(child.has("cResult"));

    // Child produces results, then copy outbound
    child.set("cResult", 99);
    child.set("cShared", 20);
    child.set("cTarget", glm::vec3{5.0f}); // In-only: must NOT flow back
    behaviortree::applyMappingsOut(mappings, parent, child);
    CHECK(parent.getInt("pResult") == 99);  // Out
    CHECK(parent.getInt("pShared") == 20);   // InOut flows back
    CHECK(parent.getVec3("pTarget") == glm::vec3{1.0f, 2.0f, 3.0f}); // In-only unchanged
}

TEST_CASE("computeActivePath: root to deepest running leaf") {
    behaviortree::BTGraph graph;
    graph.rootNodeId = 1;
    auto addNode = [&](uint32_t id, behaviortree::BTNodeType t) {
        behaviortree::BTNode n; n.id = id; n.type = t; graph.nodes.push_back(n);
    };
    auto link = [&](uint32_t s, uint32_t t, uint32_t order) {
        behaviortree::BTLink l; l.id = graph.nextLinkId++; l.sourceNodeId = s; l.targetNodeId = t; l.sortOrder = order;
        graph.links.push_back(l);
    };
    addNode(1, behaviortree::BTNodeType::Root);
    addNode(2, behaviortree::BTNodeType::Sequence);
    addNode(3, behaviortree::BTNodeType::Wait);   // first child (completed)
    addNode(4, behaviortree::BTNodeType::Wait);   // second child (running)
    link(1, 2, 0);
    link(2, 3, 0);
    link(2, 4, 1);

    std::unordered_map<uint32_t, behaviortree::BTNodeRuntime> states;
    states[1].lastStatus = behaviortree::BTNodeStatus::Running;
    states[2].lastStatus = behaviortree::BTNodeStatus::Running;
    states[2].currentChildIndex = 1;
    states[3].lastStatus = behaviortree::BTNodeStatus::Success;
    states[4].lastStatus = behaviortree::BTNodeStatus::Running;

    auto path = behaviortree::computeActivePath(graph, states, 1);
    REQUIRE(path.size() == 3);
    CHECK(path[0] == 1);
    CHECK(path[1] == 2);
    CHECK(path[2] == 4);

    SUBCASE("empty when root not running") {
        states[1].lastStatus = behaviortree::BTNodeStatus::Success;
        CHECK(behaviortree::computeActivePath(graph, states, 1).empty());
    }
}

TEST_CASE("captureSnapshotCore assembles per-node debug fields") {
    behaviortree::BTGraph graph;
    graph.rootNodeId = 1;
    behaviortree::BTNode root; root.id = 1; root.type = behaviortree::BTNodeType::Root; graph.nodes.push_back(root);
    behaviortree::BTNode leaf; leaf.id = 2; leaf.type = behaviortree::BTNodeType::Wait; graph.nodes.push_back(leaf);
    behaviortree::BTLink l; l.id = 1; l.sourceNodeId = 1; l.targetNodeId = 2; graph.links.push_back(l);

    std::unordered_map<uint32_t, behaviortree::BTNodeRuntime> states;
    states[1].lastStatus = behaviortree::BTNodeStatus::Running;
    states[2].lastStatus = behaviortree::BTNodeStatus::Running;
    states[2].elapsedTime = 1.25f;
    states[2].currentChildIndex = 0;
    states[2].lastCompletedStatus = behaviortree::BTNodeStatus::Success;

    behaviortree::Blackboard bb;
    bb.set("b", 1);
    bb.set("a", 2);

    behaviortree::BTRuntimeSnapshot snap;
    behaviortree::captureSnapshotCore(graph, states, bb, 1, snap);

    CHECK(snap.nodeStatuses.at(2) == behaviortree::BTNodeStatus::Running);
    CHECK(snap.elapsedTimes.at(2) == doctest::Approx(1.25f));
    CHECK(snap.lastResults.at(2) == behaviortree::BTNodeStatus::Success);
    REQUIRE(snap.activePath.size() == 2);
    CHECK(snap.activePath.front() == 1);
    CHECK(snap.activePath.back() == 2);
    // Blackboard is sorted by key.
    REQUIRE(snap.blackboard.size() == 2);
    CHECK(snap.blackboard[0].first == "a");
    CHECK(snap.blackboard[1].first == "b");
}

// ============================================================
// VK-1457: dynamic subtree nested runtime
// ============================================================

namespace {
    using TreeMap = std::unordered_map<std::string, std::shared_ptr<const behaviortree::BehaviorTreeData>>;

    behaviortree::BehaviorTreeRuntime::TreeResolver makeResolver(const TreeMap& trees) {
        return [&trees](const std::string& path) -> std::shared_ptr<const behaviortree::BehaviorTreeData> {
            auto it = trees.find(path);
            return it != trees.end() ? it->second : nullptr;
        };
    }

    behaviortree::BTNode makeSetNode(uint32_t id, const std::string& key, behaviortree::BlackboardValue value) {
        auto n = makeBTNode(id, behaviortree::BTNodeType::SetBlackboardValue);
        n.properties["key"] = key;
        n.properties["value"] = std::move(value);
        return n;
    }
} // namespace

TEST_CASE("DynamicSubTree: selects a nested tree by blackboard key and copies results back out") {
    using namespace behaviortree;
    // child: Root -> SetBlackboardValue(ranChild = true)
    auto child = std::make_shared<BehaviorTreeData>();
    child->graph.rootNodeId = 1;
    child->graph.nodes.push_back(makeBTNode(1, BTNodeType::Root));
    child->graph.nodes.push_back(makeSetNode(2, "ranChild", true));
    linkBTNodes(child->graph, 1, 2, 0);
    finalizeGraphIds(child->graph);

    TreeMap trees;
    trees["child.vfBehaviorTree"] = child;

    BehaviorTreeData parent;
    parent.graph.rootNodeId = 1;
    parent.graph.nodes.push_back(makeBTNode(1, BTNodeType::Root));
    auto dyn = makeBTNode(2, BTNodeType::DynamicSubTree);
    dyn.properties["selectionKey"] = std::string("brain");
    dyn.blackboardMappings.push_back({"childRan", "ranChild", MappingDirection::Out});
    parent.graph.nodes.push_back(dyn);
    linkBTNodes(parent.graph, 1, 2, 0);
    finalizeGraphIds(parent.graph);

    BehaviorTreeRuntime runtime;
    runtime.init(std::make_shared<BehaviorTreeData>(parent), services::EntityHandle{1});
    runtime.setTreeResolver(makeResolver(trees));
    runtime.setNestingContext(0, {"parent.vfBehaviorTree"});
    runtime.getBlackboard().set("brain", std::string("child.vfBehaviorTree"));

    AbortRecorder exec;
    CHECK(runtime.tick(0.1f, &exec) == BTNodeStatus::Success);
    CHECK(runtime.getBlackboard().getBool("childRan") == true);
}

TEST_CASE("DynamicSubTree: copy-in feeds the child, copy-out returns the result") {
    using namespace behaviortree;
    // child: Root -> Sequence[ CheckBlackboardValue(dst == 7), SetBlackboardValue(done = true) ]
    auto child = std::make_shared<BehaviorTreeData>();
    child->graph.rootNodeId = 1;
    child->graph.nodes.push_back(makeBTNode(1, BTNodeType::Root));
    child->graph.nodes.push_back(makeBTNode(2, BTNodeType::Sequence));
    auto check = makeBTNode(3, BTNodeType::CheckBlackboardValue);
    check.properties["key"] = std::string("dst");
    check.properties["compareOp"] = std::string("==");
    check.properties["compareValue"] = int32_t{7};
    child->graph.nodes.push_back(check);
    child->graph.nodes.push_back(makeSetNode(4, "done", true));
    linkBTNodes(child->graph, 1, 2, 0);
    linkBTNodes(child->graph, 2, 3, 0);
    linkBTNodes(child->graph, 2, 4, 1);
    finalizeGraphIds(child->graph);

    TreeMap trees;
    trees["c"] = child;

    BehaviorTreeData parent;
    parent.graph.rootNodeId = 1;
    parent.graph.nodes.push_back(makeBTNode(1, BTNodeType::Root));
    auto dyn = makeBTNode(2, BTNodeType::DynamicSubTree);
    dyn.properties["defaultTreePath"] = std::string("c");
    dyn.blackboardMappings.push_back({"src", "dst", MappingDirection::In});
    dyn.blackboardMappings.push_back({"result", "done", MappingDirection::Out});
    parent.graph.nodes.push_back(dyn);
    linkBTNodes(parent.graph, 1, 2, 0);
    finalizeGraphIds(parent.graph);

    BehaviorTreeRuntime runtime;
    runtime.init(std::make_shared<BehaviorTreeData>(parent), services::EntityHandle{1});
    runtime.setTreeResolver(makeResolver(trees));
    runtime.setNestingContext(0, {"parent"});
    runtime.getBlackboard().set("src", int32_t{7});

    AbortRecorder exec;
    CHECK(runtime.tick(0.1f, &exec) == BTNodeStatus::Success); // Success only if copy-in delivered dst==7
    CHECK(runtime.getBlackboard().getBool("result") == true);  // copy-out returned the child's done flag
}

TEST_CASE("DynamicSubTree: swapping the selection mid-run aborts the old nested tasks") {
    using namespace behaviortree;
    // childA: Root -> MoveTo (executor returns Running -> runs forever)
    auto childA = std::make_shared<BehaviorTreeData>();
    childA->graph.rootNodeId = 1;
    childA->graph.nodes.push_back(makeBTNode(1, BTNodeType::Root));
    childA->graph.nodes.push_back(makeBTNode(2, BTNodeType::MoveTo));
    linkBTNodes(childA->graph, 1, 2, 0);
    finalizeGraphIds(childA->graph);
    // childB: Root -> Log (Success)
    auto childB = std::make_shared<BehaviorTreeData>();
    childB->graph.rootNodeId = 1;
    childB->graph.nodes.push_back(makeBTNode(1, BTNodeType::Root));
    childB->graph.nodes.push_back(makeBTNode(2, BTNodeType::Log));
    linkBTNodes(childB->graph, 1, 2, 0);
    finalizeGraphIds(childB->graph);

    TreeMap trees;
    trees["A"] = childA;
    trees["B"] = childB;

    BehaviorTreeData parent;
    parent.graph.rootNodeId = 1;
    parent.graph.nodes.push_back(makeBTNode(1, BTNodeType::Root));
    auto dyn = makeBTNode(2, BTNodeType::DynamicSubTree);
    dyn.properties["selectionKey"] = std::string("brain");
    parent.graph.nodes.push_back(dyn);
    linkBTNodes(parent.graph, 1, 2, 0);
    finalizeGraphIds(parent.graph);

    BehaviorTreeRuntime runtime;
    runtime.init(std::make_shared<BehaviorTreeData>(parent), services::EntityHandle{1});
    runtime.setTreeResolver(makeResolver(trees));
    runtime.setNestingContext(0, {"parent"});

    AbortRecorder exec;
    runtime.getBlackboard().set("brain", std::string("A"));
    CHECK(runtime.tick(0.1f, &exec) == BTNodeStatus::Running); // childA MoveTo running
    CHECK(exec.aborts.empty());

    runtime.getBlackboard().set("brain", std::string("B"));
    CHECK(runtime.tick(0.1f, &exec) == BTNodeStatus::Success); // childB Log succeeds
    // The running MoveTo in childA must have been aborted on the swap.
    REQUIRE(exec.aborts.size() == 1);
    CHECK(exec.aborts[0].second == BTNodeType::MoveTo);
}

TEST_CASE("DynamicSubTree: empty resolution fails") {
    using namespace behaviortree;
    BehaviorTreeData parent;
    parent.graph.rootNodeId = 1;
    parent.graph.nodes.push_back(makeBTNode(1, BTNodeType::Root));
    parent.graph.nodes.push_back(makeBTNode(2, BTNodeType::DynamicSubTree)); // no key/tag/default
    linkBTNodes(parent.graph, 1, 2, 0);
    finalizeGraphIds(parent.graph);

    TreeMap trees;
    BehaviorTreeRuntime runtime;
    runtime.init(std::make_shared<BehaviorTreeData>(parent), services::EntityHandle{1});
    runtime.setTreeResolver(makeResolver(trees));
    runtime.setNestingContext(0, {"parent"});

    AbortRecorder exec;
    CHECK(runtime.tick(0.1f, &exec) == BTNodeStatus::Failure);
}

TEST_CASE("DynamicSubTree: self-referential cycle is rejected") {
    using namespace behaviortree;
    auto self = std::make_shared<BehaviorTreeData>();
    self->graph.rootNodeId = 1;
    self->graph.nodes.push_back(makeBTNode(1, BTNodeType::Root));
    self->graph.nodes.push_back(makeBTNode(2, BTNodeType::Log));
    linkBTNodes(self->graph, 1, 2, 0);
    finalizeGraphIds(self->graph);

    TreeMap trees;
    trees["self"] = self;

    BehaviorTreeData parent;
    parent.graph.rootNodeId = 1;
    parent.graph.nodes.push_back(makeBTNode(1, BTNodeType::Root));
    auto dyn = makeBTNode(2, BTNodeType::DynamicSubTree);
    dyn.properties["defaultTreePath"] = std::string("self");
    parent.graph.nodes.push_back(dyn);
    linkBTNodes(parent.graph, 1, 2, 0);
    finalizeGraphIds(parent.graph);

    BehaviorTreeRuntime runtime;
    runtime.init(std::make_shared<BehaviorTreeData>(parent), services::EntityHandle{1});
    runtime.setTreeResolver(makeResolver(trees));
    runtime.setNestingContext(0, {"self"}); // parent's own path == the dynamic target -> cycle

    AbortRecorder exec;
    CHECK(runtime.tick(0.1f, &exec) == BTNodeStatus::Failure);
}

TEST_CASE("DynamicSubTree: nesting depth limit is enforced") {
    using namespace behaviortree;
    auto leaf = std::make_shared<BehaviorTreeData>();
    leaf->graph.rootNodeId = 1;
    leaf->graph.nodes.push_back(makeBTNode(1, BTNodeType::Root));
    leaf->graph.nodes.push_back(makeBTNode(2, BTNodeType::Log));
    linkBTNodes(leaf->graph, 1, 2, 0);
    finalizeGraphIds(leaf->graph);

    TreeMap trees;
    trees["deep"] = leaf;

    BehaviorTreeData parent;
    parent.graph.rootNodeId = 1;
    parent.graph.nodes.push_back(makeBTNode(1, BTNodeType::Root));
    auto dyn = makeBTNode(2, BTNodeType::DynamicSubTree);
    dyn.properties["defaultTreePath"] = std::string("deep");
    parent.graph.nodes.push_back(dyn);
    linkBTNodes(parent.graph, 1, 2, 0);
    finalizeGraphIds(parent.graph);

    BehaviorTreeRuntime runtime;
    runtime.init(std::make_shared<BehaviorTreeData>(parent), services::EntityHandle{1});
    runtime.setTreeResolver(makeResolver(trees));
    // Already at the max depth (8): entering one more level must be refused.
    runtime.setNestingContext(8, {"a", "b", "c", "d", "e", "f", "g", "h"});

    AbortRecorder exec;
    CHECK(runtime.tick(0.1f, &exec) == BTNodeStatus::Failure);
}

TEST_CASE("DynamicSubTree: abortAll cancels in-flight nested tasks") {
    using namespace behaviortree;
    auto child = std::make_shared<BehaviorTreeData>();
    child->graph.rootNodeId = 1;
    child->graph.nodes.push_back(makeBTNode(1, BTNodeType::Root));
    child->graph.nodes.push_back(makeBTNode(2, BTNodeType::MoveTo));
    linkBTNodes(child->graph, 1, 2, 0);
    finalizeGraphIds(child->graph);

    TreeMap trees;
    trees["c"] = child;

    BehaviorTreeData parent;
    parent.graph.rootNodeId = 1;
    parent.graph.nodes.push_back(makeBTNode(1, BTNodeType::Root));
    auto dyn = makeBTNode(2, BTNodeType::DynamicSubTree);
    dyn.properties["defaultTreePath"] = std::string("c");
    parent.graph.nodes.push_back(dyn);
    linkBTNodes(parent.graph, 1, 2, 0);
    finalizeGraphIds(parent.graph);

    BehaviorTreeRuntime runtime;
    runtime.init(std::make_shared<BehaviorTreeData>(parent), services::EntityHandle{1});
    runtime.setTreeResolver(makeResolver(trees));
    runtime.setNestingContext(0, {"parent"});

    AbortRecorder exec;
    CHECK(runtime.tick(0.1f, &exec) == BTNodeStatus::Running);
    runtime.abortAll(&exec);
    // abortAll aborts the whole parent tree: the DynamicSubTree leaf itself AND the nested MoveTo.
    bool sawMoveTo = false, sawDynamic = false;
    for (const auto& a : exec.aborts) {
        if (a.second == BTNodeType::MoveTo) sawMoveTo = true;
        if (a.second == BTNodeType::DynamicSubTree) sawDynamic = true;
    }
    CHECK(sawMoveTo);   // in-flight nested task cancelled
    CHECK(sawDynamic);  // the dynamic-subtree leaf itself aborted
}

TEST_CASE("DynamicSubTree: nested services flush onServiceEnd exactly once via endAllServices") {
    using namespace behaviortree;
    // child: Root -> Service -> Wait(forever)
    auto child = std::make_shared<BehaviorTreeData>();
    child->graph.rootNodeId = 1;
    child->graph.nodes.push_back(makeBTNode(1, BTNodeType::Root));
    child->graph.nodes.push_back(makeServiceNode(2, "EQSRefresh", 0.5f, 0.0f, false));
    child->graph.nodes.push_back(makeWaitNode(3, 1000.0f));
    linkBTNodes(child->graph, 1, 2, 0);
    linkBTNodes(child->graph, 2, 3, 0);
    finalizeGraphIds(child->graph);

    TreeMap trees;
    trees["c"] = child;

    BehaviorTreeData parent;
    parent.graph.rootNodeId = 1;
    parent.graph.nodes.push_back(makeBTNode(1, BTNodeType::Root));
    auto dyn = makeBTNode(2, BTNodeType::DynamicSubTree);
    dyn.properties["defaultTreePath"] = std::string("c");
    parent.graph.nodes.push_back(dyn);
    linkBTNodes(parent.graph, 1, 2, 0);
    finalizeGraphIds(parent.graph);

    BehaviorTreeRuntime runtime;
    runtime.init(std::make_shared<BehaviorTreeData>(parent), services::EntityHandle{1});
    runtime.setTreeResolver(makeResolver(trees));
    runtime.setNestingContext(0, {"parent"});

    ServiceRecorder exec;
    CHECK(runtime.tick(0.1f, &exec) == BTNodeStatus::Running); // child Wait runs; nested service starts
    CHECK(exec.count("start") == 1);
    runtime.endAllServices(&exec); // parent flush must recurse into the nested runtime
    CHECK(exec.count("end") == 1);
}

TEST_CASE("DynamicSubTree: asset hot-reload (new data pointer, same path) rebuilds the nested runtime") {
    using namespace behaviortree;
    auto makeMoveTree = []() {
        auto t = std::make_shared<BehaviorTreeData>();
        t->graph.rootNodeId = 1;
        t->graph.nodes.push_back(makeBTNode(1, BTNodeType::Root));
        t->graph.nodes.push_back(makeBTNode(2, BTNodeType::MoveTo)); // runs forever
        linkBTNodes(t->graph, 1, 2, 0);
        finalizeGraphIds(t->graph);
        return t;
    };
    auto v1 = makeMoveTree();
    auto v2 = makeMoveTree(); // same shape, DIFFERENT pointer -> simulates a saved reload

    TreeMap trees;
    trees["c"] = v1;

    BehaviorTreeData parent;
    parent.graph.rootNodeId = 1;
    parent.graph.nodes.push_back(makeBTNode(1, BTNodeType::Root));
    auto dyn = makeBTNode(2, BTNodeType::DynamicSubTree);
    dyn.properties["defaultTreePath"] = std::string("c");
    parent.graph.nodes.push_back(dyn);
    linkBTNodes(parent.graph, 1, 2, 0);
    finalizeGraphIds(parent.graph);

    BehaviorTreeRuntime runtime;
    runtime.init(std::make_shared<BehaviorTreeData>(parent), services::EntityHandle{1});
    runtime.setTreeResolver(makeResolver(trees));
    runtime.setNestingContext(0, {"parent"});

    AbortRecorder exec;
    CHECK(runtime.tick(0.1f, &exec) == BTNodeStatus::Running); // built from v1, MoveTo running
    CHECK(exec.aborts.empty());

    trees["c"] = v2; // "reload": same path resolves to a new pointer
    CHECK(runtime.tick(0.1f, &exec) == BTNodeStatus::Running); // rebuilt from v2
    // The v1 nested MoveTo must have been aborted on rebuild.
    REQUIRE(exec.aborts.size() == 1);
    CHECK(exec.aborts[0].second == BTNodeType::MoveTo);
}

TEST_CASE("Debug recording captures enter/exit and abort events only when enabled") {
    using namespace behaviortree;
    BehaviorTreeData data;
    data.graph.rootNodeId = 1;
    data.graph.nodes.push_back(makeBTNode(1, BTNodeType::Root));
    data.graph.nodes.push_back(makeBTNode(2, BTNodeType::MoveTo)); // executor returns Running
    linkBTNodes(data.graph, 1, 2, 0);
    finalizeGraphIds(data.graph);

    BehaviorTreeRuntime runtime;
    runtime.init(std::make_shared<BehaviorTreeData>(data), services::EntityHandle{1});
    runtime.setDebugRecording(true);

    AbortRecorder exec;
    runtime.tick(0.1f, &exec);
    bool sawEnter = false;
    for (const auto& e : runtime.getExecutionEvents())
        if (e.nodeId == 2 && e.type == BTEventType::Enter) sawEnter = true;
    CHECK(sawEnter);
    CHECK(runtime.getAbortRecords().empty());

    runtime.abortAll(&exec);
    REQUIRE_FALSE(runtime.getAbortRecords().empty());
    CHECK(runtime.getAbortRecords().back().nodeId == 2);
}

// ============================================================
// VK-1457 code-review fixes
// ============================================================

TEST_CASE("DynamicSubTree: a nested task still Running at terminal completion is aborted (VK-1457 #3)") {
    using namespace behaviortree;
    // child "combat": Root -> Parallel(RequireOne)[ MoveTo (Running forever), Log (Success) ].
    // The Parallel returns Success on the Log while the MoveTo is still Running.
    auto child = std::make_shared<BehaviorTreeData>();
    child->graph.rootNodeId = 1;
    child->graph.nodes.push_back(makeBTNode(1, BTNodeType::Root));
    auto par = makeBTNode(2, BTNodeType::Parallel);
    par.properties["policy"] = std::string("RequireOne");
    child->graph.nodes.push_back(par);
    child->graph.nodes.push_back(makeBTNode(3, BTNodeType::MoveTo)); // executor returns Running
    child->graph.nodes.push_back(makeBTNode(4, BTNodeType::Log));    // executor returns Success
    linkBTNodes(child->graph, 1, 2, 0);
    linkBTNodes(child->graph, 2, 3, 0);
    linkBTNodes(child->graph, 2, 4, 1);
    finalizeGraphIds(child->graph);

    TreeMap trees;
    trees["combat"] = child;

    BehaviorTreeData parent;
    parent.graph.rootNodeId = 1;
    parent.graph.nodes.push_back(makeBTNode(1, BTNodeType::Root));
    auto dyn = makeBTNode(2, BTNodeType::DynamicSubTree);
    dyn.properties["defaultTreePath"] = std::string("combat");
    parent.graph.nodes.push_back(dyn);
    linkBTNodes(parent.graph, 1, 2, 0);
    finalizeGraphIds(parent.graph);

    BehaviorTreeRuntime runtime;
    runtime.init(std::make_shared<BehaviorTreeData>(parent), services::EntityHandle{1});
    runtime.setTreeResolver(makeResolver(trees));
    runtime.setNestingContext(0, {"parent"});

    AbortRecorder exec;
    // The nested tree returns Success this tick while its MoveTo is Running; the terminal teardown must
    // abort that MoveTo (pre-fix it called only endAllServices and leaked the nav order).
    CHECK(runtime.tick(0.1f, &exec) == BTNodeStatus::Success);
    REQUIRE(exec.aborts.size() == 1);
    CHECK(exec.aborts[0].second == BTNodeType::MoveTo);
}

TEST_CASE("DynamicSubTree: reset by a completing ancestor aborts a still-Running nested task (VK-1457 #2)") {
    using namespace behaviortree;
    // child "combat": Root -> MoveTo (Running forever).
    auto child = std::make_shared<BehaviorTreeData>();
    child->graph.rootNodeId = 1;
    child->graph.nodes.push_back(makeBTNode(1, BTNodeType::Root));
    child->graph.nodes.push_back(makeBTNode(2, BTNodeType::MoveTo)); // executor returns Running
    linkBTNodes(child->graph, 1, 2, 0);
    finalizeGraphIds(child->graph);

    TreeMap trees;
    trees["combat"] = child;

    // parent: Root -> Sequence[ Parallel(RequireOne)[ DynamicSubTree("combat"), Log ], Log ].
    // The Parallel completes (RequireOne) while the DynamicSubTree stays Running, then the Sequence
    // completes and resets its children — walking onto the still-Running DynamicSubTree.
    BehaviorTreeData parent;
    parent.graph.rootNodeId = 1;
    parent.graph.nodes.push_back(makeBTNode(1, BTNodeType::Root));
    parent.graph.nodes.push_back(makeBTNode(2, BTNodeType::Sequence));
    auto par = makeBTNode(3, BTNodeType::Parallel);
    par.properties["policy"] = std::string("RequireOne");
    parent.graph.nodes.push_back(par);
    auto dyn = makeBTNode(4, BTNodeType::DynamicSubTree);
    dyn.properties["defaultTreePath"] = std::string("combat");
    parent.graph.nodes.push_back(dyn);
    parent.graph.nodes.push_back(makeBTNode(5, BTNodeType::Log)); // Parallel's 2nd child -> Success
    parent.graph.nodes.push_back(makeBTNode(6, BTNodeType::Log)); // Sequence's 2nd child -> Success
    linkBTNodes(parent.graph, 1, 2, 0);
    linkBTNodes(parent.graph, 2, 3, 0); // Sequence -> Parallel
    linkBTNodes(parent.graph, 2, 6, 1); // Sequence -> Log(6)
    linkBTNodes(parent.graph, 3, 4, 0); // Parallel -> DynamicSubTree
    linkBTNodes(parent.graph, 3, 5, 1); // Parallel -> Log(5)
    finalizeGraphIds(parent.graph);

    BehaviorTreeRuntime runtime;
    runtime.init(std::make_shared<BehaviorTreeData>(parent), services::EntityHandle{1});
    runtime.setTreeResolver(makeResolver(trees));
    runtime.setNestingContext(0, {"parent"});

    AbortRecorder exec;
    // Whole tree completes in one tick; the reset must abort the nested MoveTo (pre-fix resetSubtreeState
    // dropped the nested runtime with no executor, leaking the nav order).
    CHECK(runtime.tick(0.1f, &exec) == BTNodeStatus::Success);
    REQUIRE(exec.aborts.size() == 1);
    CHECK(exec.aborts[0].second == BTNodeType::MoveTo);
}

TEST_CASE("Service: a large dt is capped, not fired hundreds of times in one tick (VK-1457 #5)") {
    behaviortree::BehaviorTreeRuntime runtime;
    runtime.init(std::make_shared<behaviortree::BehaviorTreeData>(makeServiceTree(0.5f, 0.0f, false)),
                 services::EntityHandle{});
    ServiceRecorder executor;

    // One tick with a huge dt (frame hitch). Pre-fix this drained ~200 fires (100 / 0.5); the per-tick
    // cap bounds it to a small constant.
    CHECK(runtime.tick(100.0f, &executor) == behaviortree::BTNodeStatus::Running);
    CHECK(executor.count("tick") >= 1);
    CHECK(executor.count("tick") <= 4);
}

TEST_CASE("getNodeProperty: typed lookup returns the value on type match, default otherwise (VK-1457 #10)") {
    using namespace behaviortree;
    BTNode node;
    node.properties["f"] = 2.5f;
    node.properties["i"] = int32_t{7};
    node.properties["s"] = std::string("hi");

    CHECK(getNodeProperty<float>(node, "f", 0.0f) == doctest::Approx(2.5f));
    CHECK(getNodeProperty<int32_t>(node, "i", -1) == 7);
    CHECK(getNodeProperty<std::string>(node, "s", std::string{}) == "hi");
    CHECK(getNodeProperty<float>(node, "missing", 9.0f) == doctest::Approx(9.0f)); // absent -> default
    CHECK(getNodeProperty<int32_t>(node, "f", -1) == -1);                          // type mismatch -> default
}

TEST_CASE("SubTree expansion: entry map records where each authored SubTree node was spliced (VK-1457 #6)") {
    auto parent = makeParentTree(); // Root -> Sequence[ Wait, SubTree(4 -> "child.bt") ]
    auto loader = [](const std::string& path) -> std::optional<behaviortree::BehaviorTreeData> {
        if (path == "child.bt") return makeChildTree(); // Root -> Selector -> Wait
        return std::nullopt;
    };

    std::unordered_map<uint32_t, uint32_t> entryMap;
    REQUIRE(behaviortree::BehaviorTreeAsset::expandSubTrees(parent, loader, 8, &entryMap));

    // Authored SubTree node 4 maps to the spliced entry node (the child's Selector), now under a
    // re-mapped id in the expanded graph; the original SubTree node id is gone.
    REQUIRE(entryMap.count(4) == 1);
    const behaviortree::BTNode* entry = parent.graph.findNodeById(entryMap.at(4));
    REQUIRE(entry != nullptr);
    CHECK(entry->type == behaviortree::BTNodeType::Selector);
    CHECK(parent.graph.findNodeById(4) == nullptr);
}

} // TEST_SUITE
