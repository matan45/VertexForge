#include "test_mcp_bt_fixture.hpp"

#include "behaviortree/BehaviorTreeNodeCatalog.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// VK-1660: the MCP behavior-tree tools (bt_*), the BehaviorTree component binding and vf://docs/bt.
using namespace mcp_bt_test;

namespace
{
    namespace bttool = mcp::tools::bttool;

    nlohmann::json exampleTree()
    {
        return nlohmann::json::parse(bttool::workedExampleTree());
    }

    // Root 1 -> Main 2 -> [Pause 3, Hello 4, Pick 5 -> [Short 6, Long 7]] (ids in pre-order).
    nlohmann::json baseTree()
    {
        return nlohmann::json::parse(R"json({"name": "Base",
            "blackboard": [{"name": "alert", "type": "Bool"}, {"name": "count", "type": "Int", "default": 2},
                           {"name": "home", "type": "Vec3", "default": [1, 2, 3]}],
            "root": {"type": "Sequence", "name": "Main", "children": [
                {"type": "Wait", "name": "Pause", "params": {"duration": 2}},
                {"type": "Log", "name": "Hello", "params": {"message": "hi"}},
                {"type": "Selector", "name": "Pick", "children": [
                    {"type": "Wait", "name": "Short", "params": {"duration": 0.5}},
                    {"type": "Wait", "name": "Long", "params": {"duration": 5}}]}]}})json");
    }

    // A one-node tree whose top node is `node`, with the given keys.
    nlohmann::json treeWith(nlohmann::json node, nlohmann::json keys = nlohmann::json::array())
    {
        return {{"name", "Probe"}, {"blackboard", std::move(keys)}, {"root", std::move(node)}};
    }

    void writeEnemyScripts(BTToolFixture& fixture)
    {
        fixture.writeScript("game/ai/EnemyMeleeAttack.mt", scriptTaskSource("EnemyMeleeAttack"));
        fixture.writeScript("game/ai/Idle.mt", "@Script\npublic class Idle extends Behaviour {\n"
                                               "    public function onUpdate(float deltaTime): void {\n    }\n}\n");
        fixture.writeScript("lib/engine/Helper.mt", scriptTaskSource("Helper"));
    }

    // bt_create must refuse `tree` (an ArgError naming `needle`) and write nothing.
    void checkRefused(BTToolFixture& fixture, const nlohmann::json& tree, const std::string& needle)
    {
        const std::string error = fixture.argError("bt_create", {{"path", "ai/Refused.vfBehaviorTree"}, {"tree", tree}});
        INFO("error: " << error);
        CHECK(contains(error, needle));
        CHECK_FALSE(fs::exists(fixture.file("ai/Refused.vfBehaviorTree")));
    }

    uint32_t idOf(const nlohmann::json& summary)
    {
        return summary["id"].get<uint32_t>();
    }
}

TEST_SUITE("MCP behavior tree tools")
{
    TEST_CASE("bt tools: registration, affinity, flags and closed schemas")
    {
        BTToolFixture fixture("VertexForge_McpBt_Registration");
        for (const char* name : {"bt_list_node_types", "bt_create", "bt_get", "bt_set", "bt_edit", "bt_debug_state",
                                 "bt_get_blackboard", "bt_set_blackboard", "bt_assign", "bt_unassign"})
        {
            CAPTURE(name);
            const mcp::ToolDef& def = fixture.tool(name);
            CHECK(def.inputSchema["additionalProperties"] == false);
            CHECK_FALSE(def.description.empty());
        }
        CHECK(fixture.tool("bt_list_node_types").affinity == mcp::ThreadAffinity::Worker);
        CHECK(fixture.tool("bt_list_node_types").readOnly);
        CHECK(fixture.tool("bt_debug_state").affinity == mcp::ThreadAffinity::Worker);
        CHECK(fixture.tool("bt_debug_state").readOnly);
        CHECK(fixture.tool("bt_debug_state").timeout == std::chrono::milliseconds(15000));
        CHECK(fixture.tool("bt_create").affinity == mcp::ThreadAffinity::Main);
        CHECK(fixture.tool("bt_create").timeout == std::chrono::milliseconds(15000));
        CHECK(fixture.tool("bt_get").readOnly);
        CHECK(fixture.tool("bt_get_blackboard").readOnly);
        CHECK(fixture.tool("bt_set").destructive);
        CHECK(fixture.tool("bt_unassign").destructive);
        CHECK_FALSE(fixture.tool("bt_edit").readOnly);
        CHECK(contains(fixture.argError("bt_get", {{"path", "a.vfBehaviorTree"}, {"bogus", 1}}), "unknown argument 'bogus'"));
    }

    TEST_CASE("bt_create writes the worked example strictly; bt_get and bt_set round-trip it")
    {
        BTToolFixture fixture("VertexForge_McpBt_Create");
        writeEnemyScripts(fixture);

        const nlohmann::json created =
            fixture.callOk("bt_create", {{"path", "ai/EnemyAI.vfBehaviorTree"}, {"tree", exampleTree()}});
        CHECK(created["path"] == "ai/EnemyAI.vfBehaviorTree");
        CHECK(created["contentHash"].get<std::string>().size() == 16);
        REQUIRE(created["nodes"].size() == 7);
        // Root 1, then the nodes in pre-order.
        CHECK(created["nodes"][0] == nlohmann::json{{"id", 2}, {"name", "Sense Target"}, {"type", "Service"}});
        CHECK(created["nodes"][3] == nlohmann::json{{"id", 5}, {"name", "Attack"}, {"type", "ScriptTask"}});
        CHECK(created["nodes"][6] == nlohmann::json{{"id", 8}, {"name", "Patrol"}, {"type", "FollowRoute"}});
        CHECK(created["keys"].size() == 4);
        CHECK(fixture.savedAssets.size() == 1);

        const bt::BehaviorTreeData data = fixture.load("ai/EnemyAI.vfBehaviorTree");
        CHECK(data.graph.rootNodeId == 1);
        const bt::BTNode service = nodeNamed(data, "Sense Target");
        CHECK(property<std::string>(service, "serviceType") == "TargetDistance");
        CHECK(property<float>(service, "interval") == doctest::Approx(0.2f));
        CHECK(property<float>(service, "randomDeviation") == 0.0f);  // optional parameters are written
        const bt::BTNode attackRange = nodeNamed(data, "In Attack Range");
        CHECK(property<float>(attackRange, "compareValue") == 2.0f);   // a float, never an Int
        CHECK(property<std::string>(attackRange, "abortMode") == "Both");
        const bt::BTNode patrol = nodeNamed(data, "Patrol");
        CHECK(property<std::string>(patrol, "mode") == "Loop");        // case-insensitive in, canonical out
        const bt::BTNode attack = nodeNamed(data, "Attack");
        CHECK(attack.scriptPath == "game/ai/EnemyMeleeAttack.mt");
        CHECK(attack.scriptClassName == "EnemyMeleeAttack");
        CHECK(childrenOf(data, nodeNamed(data, "Brain").id) == std::vector<uint32_t>{4, 6, 8});
        // Tidy layout: depth x 140.
        CHECK(nodeNamed(data, "Brain").position.y == doctest::Approx(2 * bttool::layoutRow));

        const nlohmann::json got = fixture.callOk("bt_get", {{"path", "ai/EnemyAI.vfBehaviorTree"}});
        CHECK(got["contentHash"] == created["contentHash"]);
        CHECK(got["tree"]["root"]["id"] == 2);
        CHECK(got["tree"]["root"]["child"]["children"][2]["params"]["mode"] == "Loop");
        CHECK(got["tree"]["root"]["child"]["children"][0]["child"]["params"]["script"] == "game/ai/EnemyMeleeAttack.mt");
        CHECK(got["validation"]["errors"].empty());
        CHECK(got["validation"]["strict"].empty());
        CHECK_FALSE(got.contains("legacyParams"));

        // The tree bt_get reports passes back unchanged.
        const nlohmann::json same = fixture.callOk("bt_set", {{"path", "ai/EnemyAI.vfBehaviorTree"},
                                                              {"tree", got["tree"]},
                                                              {"expectedContentHash", got["contentHash"]}});
        CHECK(same["changed"] == false);
        CHECK(same["contentHash"] == created["contentHash"]);
        CHECK(fixture.savedAssets.size() == 1);

        // bt_create refuses an existing file; bt_set refuses a stale hash.
        CHECK(contains(fixture.failure("bt_create", {{"path", "ai/EnemyAI.vfBehaviorTree"}, {"tree", exampleTree()}}),
                       "already exists"));
        CHECK(contains(fixture.failure("bt_set", {{"path", "ai/EnemyAI.vfBehaviorTree"}, {"tree", exampleTree()},
                                                  {"expectedContentHash", "0000000000000001"}}),
                       "changed on disk"));
        CHECK(contains(fixture.argError("bt_set", {{"path", "ai/EnemyAI.vfBehaviorTree"}, {"tree", exampleTree()},
                                                   {"expectedContentHash", "xyz"}}),
                       "16 hex digits"));
    }

    TEST_CASE("bt_create keeps given ids, assigns the rest in pre-order and unwraps a Root")
    {
        BTToolFixture fixture("VertexForge_McpBt_Ids");
        const nlohmann::json tree = treeWith({{"type", "ROOT"}, {"child", {
            {"type", "sequence"}, {"id", 10}, {"children", nlohmann::json::array({
                {{"type", "Wait"}, {"params", {{"duration", 2}}}},
                {{"type", "Wait"}, {"id", 4}, {"name", "Given"}}})}}}});
        const nlohmann::json created = fixture.callOk("bt_create", {{"path", "ai/Ids.vfBehaviorTree"}, {"tree", tree}});
        CHECK(contains(created["warnings"].dump(), "Root is implicit"));

        const bt::BehaviorTreeData data = fixture.load("ai/Ids.vfBehaviorTree");
        CHECK(data.graph.rootNodeId == 11);  // above the largest given id
        CHECK(childrenOf(data, 11) == std::vector<uint32_t>{10});
        CHECK(childrenOf(data, 10) == std::vector<uint32_t>{12, 4});
        const bt::BTNode wait = *data.graph.findNodeById(12);
        CHECK(wait.name == "Wait");  // the name defaults to the type
        CHECK(property<float>(wait, "duration") == 2.0f);  // 2 is stored as a float

        checkRefused(fixture, treeWith({{"type", "Sequence"}, {"id", 3}, {"children", nlohmann::json::array({
                                            {{"type", "Wait"}, {"id", 3}}})}}),
                     "id 3 is also given at root");
        checkRefused(fixture, treeWith({{"type", "Wait"}, {"id", 0}}), "root.id: must be a positive integer");
    }

    TEST_CASE("bt_create refuses every strictness problem with its JSON path and writes nothing")
    {
        BTToolFixture fixture("VertexForge_McpBt_Strict");
        writeEnemyScripts(fixture);
        fixture.eqsQueries["Near"] = true;
        fixture.eqsQueries["Broken"] = false;

        const nlohmann::json flag = nlohmann::json::array({{{"name", "flag"}, {"type", "Bool"}}});
        const nlohmann::json count = nlohmann::json::array({{{"name", "count"}, {"type", "Int"}}});

        checkRefused(fixture, treeWith({{"type", "Jump"}}), "root.type: unknown node type 'Jump'");
        checkRefused(fixture, treeWith({{"type", "Wait"}, {"params", {{"length", 1}}}}),
                     "root.params.length: unknown parameter of Wait");
        checkRefused(fixture, treeWith({{"type", "Wait"}, {"color", 1}}), "root.color: unknown node key");
        checkRefused(fixture, treeWith({{"type", "BlackboardCondition"}, {"child", {{"type", "Wait"}}}}),
                     "root.params.key: missing required parameter");
        checkRefused(fixture, treeWith({{"type", "Inverter"}, {"children", nlohmann::json::array({
                                            {{"type", "Wait"}}, {{"type", "Wait"}}})}}),
                     "needs exactly 1 child (has 2)");
        checkRefused(fixture, treeWith({{"type", "Wait"}, {"child", {{"type", "Wait"}}}}), "cannot have children (has 1)");
        checkRefused(fixture, treeWith({{"type", "Repeater"}, {"params", {{"repeatCount", 2.5}}}, {"child", {{"type", "Wait"}}}}),
                     "root.params.repeatCount: must be an integer");
        checkRefused(fixture, treeWith({{"type", "Wait"}, {"params", {{"duration", 100}}}}), "is outside 0..60");
        checkRefused(fixture, treeWith({{"type", "FollowRoute"}, {"params", {{"mode", "zigzag"}}}},
                                       nlohmann::json::array({{{"name", "route"}, {"type", "Entity"}}})),
                     "must be one of Loop, PingPong, Once");
        checkRefused(fixture, treeWith({{"type", "MoveTo"}, {"params", {{"targetKey", "enemy"}}}}),
                     "blackboard key 'enemy' is not declared");
        checkRefused(fixture, treeWith({{"type", "MoveTo"}, {"params", {{"targetKey", "flag"}}}}, flag),
                     "is Bool; targetKey needs Vec3, Entity or Int");
        checkRefused(fixture, treeWith({{"type", "MoveTo"}}), "not given, so it uses the key 'target'");
        checkRefused(fixture, treeWith({{"type", "BlackboardCondition"},
                                        {"params", {{"key", "count"}, {"compareValue", 2.5}}},
                                        {"child", {{"type", "Wait"}}}}, count),
                     "root.params.compareValue: must be an integer");
        checkRefused(fixture, treeWith({{"type", "BlackboardCondition"},
                                        {"params", {{"key", "flag"}, {"compareOp", "<"}, {"compareValue", true}}},
                                        {"child", {{"type", "Wait"}}}}, flag),
                     "compares numbers and needs a Float or Int key");
        checkRefused(fixture, treeWith({{"type", "CheckBlackboardValue"}, {"params", {{"key", "home"}, {"compareValue", 1}}}},
                                       nlohmann::json::array({{{"name", "home"}, {"type", "Vec3"}}})),
                     "is Vec3; key needs Float, Int, Bool or String");
        checkRefused(fixture, treeWith({{"type", "ScriptTask"}, {"params", {{"script", "game/ai/Missing.mt"}}}}),
                     "no script at 'game/ai/Missing.mt'");
        checkRefused(fixture, treeWith({{"type", "ScriptTask"}, {"params", {{"script", "lib/engine/Helper.mt"}}}}),
                     "must be under game/");
        checkRefused(fixture, treeWith({{"type", "ScriptTask"}, {"params", {{"script", "game/ai/Idle.mt"}}}}),
                     "is not a ScriptTask");
        checkRefused(fixture, treeWith({{"type", "ScriptTask"}}), "root.params.script: missing required parameter");
        checkRefused(fixture, treeWith({{"type", "SubTree"}, {"params", {{"treePath", "ai/Nope.vfBehaviorTree"}}}}),
                     "no behavior tree at 'ai/Nope.vfBehaviorTree'");
        const nlohmann::json eqsKeys = nlohmann::json::array({{{"name", "eqsResult"}, {"type", "Vec3"}}});
        checkRefused(fixture, treeWith({{"type", "EnvironmentQuery"}, {"params", {{"queryName", "Nope"}}}}, eqsKeys),
                     "EQS query 'Nope' does not exist");
        checkRefused(fixture, treeWith({{"type", "EnvironmentQuery"}, {"params", {{"queryName", "Broken"}}}}, eqsKeys),
                     "EQS query 'Broken' is invalid: tests[0]: unknown test type");
        checkRefused(fixture, treeWith({{"type", "Service"},
                                        {"params", {{"serviceType", "eqsrefresh"}, {"queryName", "Near"}, {"interval", 0.05}}},
                                        {"child", {{"type", "Wait"}}}}, eqsKeys),
                     "needs interval >= 0.1");
        checkRefused(fixture, treeWith({{"type", "Service"}, {"child", {{"type", "Wait"}}}}),
                     "root.params.serviceType: missing required parameter");
        checkRefused(fixture, treeWith({{"type", "DynamicSubTree"}}), "needs at least one of selectionKey");
        checkRefused(fixture, treeWith({{"type", "Wait"}}, nlohmann::json::array({{{"name", "t"}, {"type", "Entity"},
                                                                                   {"default", 5}}})),
                     "blackboard[0].default: an Entity key has no default");
        checkRefused(fixture, treeWith({{"type", "Wait"}}, nlohmann::json::array({{{"name", "a"}, {"type", "Float"}},
                                                                                  {{"name", "a"}, {"type", "Int"}}})),
                     "duplicate key 'a'");

        // Every problem of a call is reported at once, with paths below the children.
        const std::string many = fixture.argError("bt_create", {{"path", "ai/Refused.vfBehaviorTree"}, {"tree", treeWith(
            {{"type", "Sequence"}, {"children", nlohmann::json::array({
                {{"type", "Wait"}, {"params", {{"duration", -1}}}},
                {{"type", "Bogus"}},
                {{"type", "Log"}, {"params", {{"level", "loud"}}}}})}})}});
        CHECK(contains(many, "has 3 problems"));
        CHECK(contains(many, "root.children[0].params.duration"));
        CHECK(contains(many, "root.children[1].type"));
        CHECK(contains(many, "root.children[2].params.level"));
        CHECK_FALSE(fs::exists(fixture.file("ai/Refused.vfBehaviorTree")));

        // Outside the asset folder, or another extension.
        CHECK(contains(fixture.argError("bt_create", {{"path", "../Out.vfBehaviorTree"}, {"tree", treeWith({{"type", "Wait"}})}}),
                       "outside the allowed root"));
        CHECK(contains(fixture.argError("bt_create", {{"path", "ai/Tree.json"}, {"tree", treeWith({{"type", "Wait"}})}}),
                       ".vfBehaviorTree extension"));
    }

    TEST_CASE("SubTree paths must exist, and a tree cannot include itself")
    {
        BTToolFixture fixture("VertexForge_McpBt_SubTree");
        fixture.callOk("bt_create", {{"path", "ai/Sub.vfBehaviorTree"}, {"tree", treeWith({{"type", "Wait"}})}});
        fixture.callOk("bt_create", {{"path", "ai/Outer.vfBehaviorTree"}, {"tree", treeWith(
            {{"type", "SubTree"}, {"params", {{"treePath", "ai/Sub.vfBehaviorTree"}}}})}});
        const nlohmann::json got = fixture.callOk("bt_get", {{"path", "ai/Outer.vfBehaviorTree"}});
        CHECK(got["tree"]["root"]["params"]["treePath"] == "ai/Sub.vfBehaviorTree");

        const std::string self = fixture.argError("bt_set", {{"path", "ai/Sub.vfBehaviorTree"}, {"tree", treeWith(
            {{"type", "SubTree"}, {"params", {{"treePath", "ai/Sub.vfBehaviorTree"}}}})}});
        CHECK(contains(self, "a tree cannot include itself"));
    }

    TEST_CASE("bt_edit applies every operation in order, saves once and keeps ids")
    {
        BTToolFixture fixture("VertexForge_McpBt_Edit");
        const std::string path = "ai/Base.vfBehaviorTree";
        fixture.callOk("bt_create", {{"path", path}, {"tree", baseTree()}});
        const std::size_t saves = fixture.savedAssets.size();

        const nlohmann::json edited = fixture.callOk("bt_edit", {{"path", path}, {"ops", nlohmann::json::array({
            {{"op", "setParams"}, {"node", "Pause"}, {"params", {{"duration", 3}}}},
            {{"op", "rename"}, {"node", 4}, {"name", "Greet"}},
            {{"op", "addChild"}, {"parent", "Pick"}, {"index", 0}, {"node", {{"type", "Log"}, {"name", "First"}}}},
            {{"op", "move"}, {"node", "Long"}, {"parent", "Main"}, {"index", 0}},
            {{"op", "wrap"}, {"node", "Greet"}, {"with", {{"type", "Inverter"}, {"name", "Not"}}}},
            {{"op", "replace"}, {"node", "Main"}, {"with", {{"type", "Selector"}, {"name", "Choose"}}}}})}});
        CHECK(edited["changed"] == true);
        CHECK(fixture.savedAssets.size() == saves + 1);  // one save for all ops

        const bt::BehaviorTreeData data = fixture.load(path);
        const bt::BTNode choose = *data.graph.findNodeById(2);  // replaced in place: same id
        CHECK(choose.type == bt::BTNodeType::Selector);
        CHECK(choose.name == "Choose");
        CHECK(childrenOf(data, 2) == std::vector<uint32_t>{7, 3, 9, 5});  // Long, Pause, Not, Pick
        CHECK(property<float>(*data.graph.findNodeById(3), "duration") == 3.0f);
        CHECK(data.graph.findNodeById(4)->name == "Greet");
        CHECK(childrenOf(data, 9) == std::vector<uint32_t>{4});            // Not wraps Greet
        CHECK(childrenOf(data, 5) == std::vector<uint32_t>{8, 6});         // First (new id 8), Short
        const bt::BTNode first = *data.graph.findNodeById(8);
        CHECK(property<std::string>(first, "level") == "Info");            // a new node gets every default
        CHECK(data.graph.nextNodeId == 10);

        std::vector<std::string> touched;
        for (const nlohmann::json& node : edited["touched"])
        {
            touched.push_back(node["name"].get<std::string>());
        }
        CHECK(std::find(touched.begin(), touched.end(), "Choose") != touched.end());
        CHECK(std::find(touched.begin(), touched.end(), "First") != touched.end());

        // unwrap and remove.
        const std::string hash = edited["contentHash"].get<std::string>();
        fixture.callOk("bt_edit", {{"path", path}, {"expectedContentHash", hash}, {"ops", nlohmann::json::array({
            {{"op", "unwrap"}, {"node", "Not"}},
            {{"op", "remove"}, {"node", "Pick"}}})}});
        const bt::BehaviorTreeData after = fixture.load(path);
        CHECK(childrenOf(after, 2) == std::vector<uint32_t>{7, 3, 4});
        CHECK(after.graph.findNodeById(5) == nullptr);
        CHECK(after.graph.findNodeById(6) == nullptr);
        CHECK(after.graph.findNodeById(8) == nullptr);
        CHECK(after.graph.findNodeById(9) == nullptr);
    }

    TEST_CASE("bt_edit refuses bad operations and writes nothing")
    {
        BTToolFixture fixture("VertexForge_McpBt_EditRefused");
        const std::string path = "ai/Base.vfBehaviorTree";
        const nlohmann::json created = fixture.callOk("bt_create", {{"path", path}, {"tree", baseTree()}});
        const auto edit = [&](nlohmann::json ops) { return fixture.argError("bt_edit", {{"path", path}, {"ops", ops}}); };

        CHECK(contains(edit(nlohmann::json::array({{{"op", "setParams"}, {"node", "Nobody"}, {"params", {{"duration", 1}}}}})),
                       "no node named 'Nobody'"));
        CHECK(contains(edit(nlohmann::json::array({{{"op", "addChild"}, {"parent", "Main"},
                                                    {"node", {{"type", "Wait"}, {"name", "Pause"}}}},
                                                   {{"op", "rename"}, {"node", "Pause"}, {"name", "X"}}})),
                       "is ambiguous (ids 3, 8)"));
        CHECK(contains(edit(nlohmann::json::array({{{"op", "remove"}, {"node", "Main"}}})), "top node"));
        CHECK(contains(edit(nlohmann::json::array({{{"op", "remove"}, {"node", 1}}})), "Root node is implicit"));
        CHECK(contains(edit(nlohmann::json::array({{{"op", "move"}, {"node", "Pick"}, {"parent", "Short"}}})), "below itself"));
        CHECK(contains(edit(nlohmann::json::array({{{"op", "addChild"}, {"parent", "Pause"}, {"node", {{"type", "Wait"}}}}})),
                       "cannot have children"));
        CHECK(contains(edit(nlohmann::json::array({{{"op", "wrap"}, {"node", "Pause"},
                                                    {"with", {{"type", "Inverter"}, {"child", {{"type", "Wait"}}}}}}})),
                       "must not have children"));
        CHECK(contains(edit(nlohmann::json::array({{{"op", "unwrap"}, {"node", "Main"}}})), "has 3 children"));
        CHECK(contains(edit(nlohmann::json::array({{{"op", "replace"}, {"node", "Pick"}, {"with", {{"type", "Wait"}}}}})),
                       "cannot have children, and 'Pick'"));
        CHECK(contains(edit(nlohmann::json::array({{{"op", "jump"}}})), "ops[0].op must be one of"));
        CHECK(contains(edit(nlohmann::json::array({{{"op", "rename"}, {"node", "Pause"}, {"name", "X"}, {"extra", 1}}})),
                       "unknown key 'extra' for rename"));
        CHECK(contains(edit(nlohmann::json::array({{{"op", "setParams"}, {"node", "Pause"}, {"params", {{"duration", 99}}}}})),
                       "ops[0].params.duration: = 99 is outside 0..60"));
        // All or nothing: the second op fails, the first is not written.
        CHECK(contains(edit(nlohmann::json::array({{{"op", "rename"}, {"node", "Pause"}, {"name", "Renamed"}},
                                                   {{"op", "remove"}, {"node", "Nobody"}}})),
                       "ops[1].node"));
        const nlohmann::json now = fixture.callOk("bt_get", {{"path", path}});
        CHECK(now["contentHash"] == created["contentHash"]);
        CHECK(contains(fixture.argError("bt_edit", {{"path", path}, {"ops", nlohmann::json::array()}}), "1-64 operations"));
    }

    TEST_CASE("bt_edit keeps untouched legacy parameters and drops them from a node it touches")
    {
        BTToolFixture fixture("VertexForge_McpBt_Legacy");
        const std::string path = "ai/Base.vfBehaviorTree";
        fixture.callOk("bt_create", {{"path", path}, {"tree", baseTree()}});

        // An editor-made tree: a stale property and an Int where a Float belongs.
        bt::BehaviorTreeData data = fixture.load(path);
        data.graph.findNodeById(3)->properties["oldThing"] = 7;
        data.graph.findNodeById(6)->properties["duration"] = int32_t{1};
        data.graph.findNodeById(6)->properties["legacy"] = std::string("x");
        REQUIRE(bt::BehaviorTreeAsset::save(fixture.file(path).string(), data));

        const nlohmann::json got = fixture.callOk("bt_get", {{"path", path}});
        REQUIRE(got.contains("legacyParams"));
        CHECK(got["legacyParams"].size() == 2);
        CHECK_FALSE(got["validation"]["strict"].empty());

        const nlohmann::json edited = fixture.callOk("bt_edit", {{"path", path}, {"ops", nlohmann::json::array({
            {{"op", "rename"}, {"node", "Short"}, {"name", "Brief"}}})}});
        CHECK(contains(edited["warnings"].dump(), "dropped legacy"));
        const bt::BehaviorTreeData after = fixture.load(path);
        CHECK(after.graph.findNodeById(3)->properties.contains("oldThing"));    // untouched: kept
        CHECK_FALSE(after.graph.findNodeById(6)->properties.contains("legacy")); // touched: dropped
        CHECK(property<float>(*after.graph.findNodeById(6), "duration") == 1.0f);
    }

    TEST_CASE("bt_set_blackboard edits tree keys and refuses to remove a key a node uses")
    {
        BTToolFixture fixture("VertexForge_McpBt_Keys");
        const std::string path = "ai/Guarded.vfBehaviorTree";
        fixture.callOk("bt_create", {{"path", path}, {"tree", treeWith(
            {{"type", "BlackboardCondition"}, {"name", "Counting"}, {"params", {{"key", "count"}, {"compareOp", ">"},
                                                                                {"compareValue", 1}}},
             {"child", {{"type", "Wait"}}}},
            nlohmann::json::array({{{"name", "count"}, {"type", "Int"}}, {{"name", "spare"}, {"type", "String"}}}))}});

        const nlohmann::json keys = fixture.callOk("bt_get_blackboard", {{"path", path}});
        REQUIRE(keys["keys"].size() == 2);
        CHECK(keys["keys"][0]["usedBy"][0]["param"] == "key");
        CHECK(keys["keys"][1]["usedBy"].empty());

        const std::string used = fixture.argError("bt_set_blackboard", {{"path", path},
                                                                        {"removeKeys", nlohmann::json::array({"count"})}});
        CHECK(contains(used, "key 'count' is still used by 'Counting'"));
        // A type the node cannot use.
        CHECK(contains(fixture.argError("bt_set_blackboard", {{"path", path}, {"keys", nlohmann::json::array({
                           {{"name", "count"}, {"type", "Vec3"}}})}}),
                       "the nodes using them"));

        const nlohmann::json changed = fixture.callOk("bt_set_blackboard", {{"path", path},
            {"keys", nlohmann::json::array({{{"name", "speed"}, {"type", "float"}, {"default", 3}}})},
            {"removeKeys", nlohmann::json::array({"spare"})}});
        CHECK(changed["changed"] == true);
        const bt::BehaviorTreeData data = fixture.load(path);
        REQUIRE(data.graph.blackboardKeys.size() == 2);
        CHECK(data.graph.blackboardKeys[1].name == "speed");
        CHECK(std::get<float>(data.graph.blackboardKeys[1].defaultValue) == 3.0f);

        CHECK(contains(fixture.argError("bt_set_blackboard", {{"path", path}, {"values", {{"a", 1}}}}), "belongs to"));
        CHECK(contains(fixture.argError("bt_set_blackboard", {{"path", path}}), "needs 'keys'"));
    }

    TEST_CASE("bt_assign, start values and bt_unassign: one undo step each, entity ids re-resolved")
    {
        BTToolFixture fixture("VertexForge_McpBt_Assign");
        writeEnemyScripts(fixture);
        fixture.callOk("bt_create", {{"path", "ai/EnemyAI.vfBehaviorTree"}, {"tree", exampleTree()}});
        const uint32_t enemy = fixture.makeEntity("Enemy");
        const uint32_t player = fixture.makeEntity("Player");
        const uint32_t route = fixture.makeEntity("Route");

        const nlohmann::json assigned = fixture.callOk("bt_assign", {
            {"entity", enemy}, {"tree", "ai/EnemyAI.vfBehaviorTree"},
            {"blackboard", {{"target", player}, {"route", {{"entity", route}}}}}});
        CHECK(assigned["added"] == true);
        CHECK(assigned["blackboard"]["target"] == nlohmann::json{{"entity", player}, {"name", "Player"}});
        CHECK(assigned["blackboard"]["route"] == nlohmann::json{{"entity", route}, {"name", "Route"}});
        CHECK(contains(assigned["warnings"].dump(), "neither a NavmeshAgent nor a VolumetricAgent"));
        REQUIRE(fixture.undoStack.size() == 1);
        CHECK(batchSize(fixture.undoStack.back()) == 1);

        const nlohmann::json component = fixture.callOk("component_get", {{"entity", enemy}, {"type", "BehaviorTree"}});
        CHECK(component["data"]["tree"] == "ai/EnemyAI.vfBehaviorTree");
        CHECK(component["data"]["enabled"] == true);

        fixture.undoLast();
        CHECK_FALSE(bttool::componentData(handleOf(enemy)).has_value());
        fixture.redoLast();
        REQUIRE(bttool::componentData(handleOf(enemy)).has_value());
        CHECK(fixture.callOk("component_get", {{"entity", enemy}, {"type", "BehaviorTree"}})["data"]["blackboard"]["target"]
              == nlohmann::json{{"entity", player}, {"name", "Player"}});

        // Per-entity values must fit the tree's keys.
        CHECK(contains(fixture.argError("bt_set_blackboard", {{"entity", enemy}, {"values", {{"hits", 2.5}}}}),
                       "blackboard.hits: must be an integer"));
        CHECK(contains(fixture.argError("bt_set_blackboard", {{"entity", enemy}, {"values", {{"nope", 1}}}}),
                       "the tree has no key 'nope'"));
        CHECK(contains(fixture.argError("bt_set_blackboard", {{"entity", enemy}, {"values", {{"target", 999999}}}}),
                       "does not exist"));
        const nlohmann::json set = fixture.callOk("bt_set_blackboard", {{"entity", enemy}, {"values", {{"hits", 3}}}});
        CHECK(set["changed"] == true);
        CHECK(set["blackboard"]["hits"] == 3);
        REQUIRE(fixture.undoStack.size() == 2);
        const std::size_t steps = fixture.undoStack.size();
        CHECK(fixture.callOk("bt_set_blackboard", {{"entity", enemy}, {"values", {{"hits", 3}}}})["changed"] == false);
        CHECK(fixture.undoStack.size() == steps);  // nothing changed, nothing pushed

        // The target is re-created under a new id (an undo of its delete): the recorded id is remapped.
        fixture.deleteEntity(player);
        const uint32_t playerAgain = fixture.makeEntity("Player");
        mcp::undo::EntityIdRemap::instance().record(player, playerAgain);
        fixture.undoLast();  // back to the values before hits was set
        nlohmann::json values = fixture.callOk("component_get", {{"entity", enemy}, {"type", "BehaviorTree"}})["data"]["blackboard"];
        CHECK(values["target"] == nlohmann::json{{"entity", playerAgain}, {"name", "Player"}});
        CHECK_FALSE(values.contains("hits"));

        // Gone without a remap: the value is stored by name and resolves to the next entity with that name.
        fixture.deleteEntity(playerAgain);
        nlohmann::json unresolved = fixture.callOk("component_get", {{"entity", enemy}, {"type", "BehaviorTree"}})["data"];
        CHECK(unresolved["blackboard"]["target"]["unresolved"] == true);
        CHECK(contains(unresolved["warnings"].dump(), "names entity 'Player', which does not exist now"));
        const uint32_t playerThird = fixture.makeEntity("Player");
        fixture.redoLast();
        values = fixture.callOk("component_get", {{"entity", enemy}, {"type", "BehaviorTree"}})["data"]["blackboard"];
        CHECK(values["target"] == nlohmann::json{{"entity", playerThird}, {"name", "Player"}});
        CHECK(values["hits"] == 3);

        // Re-assigning another tree drops the values it has no key for.
        fixture.callOk("bt_create", {{"path", "ai/Plain.vfBehaviorTree"}, {"tree", treeWith(
            {{"type", "MoveTo"}}, nlohmann::json::array({{{"name", "target"}, {"type", "Entity"}}}))}});
        const nlohmann::json reassigned = fixture.callOk("bt_assign", {{"entity", enemy}, {"tree", "ai/Plain.vfBehaviorTree"}});
        CHECK(reassigned["added"] == false);
        CHECK(contains(reassigned["warnings"].dump(), "dropped the start value(s)"));
        CHECK(reassigned["blackboard"].contains("target"));
        CHECK_FALSE(reassigned["blackboard"].contains("hits"));

        // bt_unassign removes the component; undo restores it with its values.
        const nlohmann::json removed = fixture.callOk("bt_unassign", {{"entity", enemy}});
        CHECK(removed["removed"] == true);
        CHECK_FALSE(bttool::componentData(handleOf(enemy)).has_value());
        fixture.undoLast();
        REQUIRE(bttool::componentData(handleOf(enemy)).has_value());
        CHECK(fixture.callOk("bt_get_blackboard", {{"entity", enemy}})["tree"] == "ai/Plain.vfBehaviorTree");

        CHECK(contains(fixture.failure("bt_assign", {{"entity", fixture.makeEntity("Other")}, {"tree", "ai/Nope.vfBehaviorTree"}}),
                       "no behavior tree at"));
    }

    TEST_CASE("bt_get_blackboard {entity} reports initial, start and live values")
    {
        BTToolFixture fixture("VertexForge_McpBt_EntityBlackboard");
        writeEnemyScripts(fixture);
        fixture.callOk("bt_create", {{"path", "ai/EnemyAI.vfBehaviorTree"}, {"tree", exampleTree()}});
        const uint32_t enemy = fixture.makeEntity("Enemy");
        const uint32_t player = fixture.makeEntity("Player");
        fixture.callOk("bt_assign", {{"entity", enemy}, {"tree", "ai/EnemyAI.vfBehaviorTree"},
                                     {"blackboard", {{"target", player}, {"hits", 2}}}});

        const nlohmann::json edit = fixture.callOk("bt_get_blackboard", {{"entity", enemy}});
        CHECK(edit["mode"] == "edit");
        REQUIRE(edit["keys"].size() == 4);
        CHECK(edit["keys"][0]["name"] == "target");
        CHECK(edit["keys"][0]["start"] == nlohmann::json{{"entity", player}, {"name", "Player"}});
        CHECK(edit["keys"][2]["name"] == "targetDistance");
        CHECK(edit["keys"][2]["start"] == 1000000.0);
        CHECK_FALSE(edit["keys"][2].contains("initial"));
        CHECK(edit["keys"][3]["initial"] == 2);
        CHECK(edit["staleValues"].empty());
    }

    TEST_CASE("Play mode: tree writes work, component writes are refused, live values need a running tree")
    {
        BTToolFixture fixture("VertexForge_McpBt_Play");
        writeEnemyScripts(fixture);
        fixture.callOk("bt_create", {{"path", "ai/EnemyAI.vfBehaviorTree"}, {"tree", exampleTree()}});
        const uint32_t enemy = fixture.makeEntity("Enemy");
        const uint32_t player = fixture.makeEntity("Player");
        fixture.callOk("bt_assign", {{"entity", enemy}, {"tree", "ai/EnemyAI.vfBehaviorTree"}});
        CHECK(contains(fixture.failure("bt_set_blackboard", {{"entity", enemy}, {"values", {{"hits", 1}}}, {"live", true}}),
                       "needs Play mode"));

        fixture.playMode = true;
        const std::size_t steps = fixture.undoStack.size();
        CHECK(contains(fixture.failure("bt_assign", {{"entity", enemy}, {"tree", "ai/EnemyAI.vfBehaviorTree"}}), "Play mode"));
        CHECK(contains(fixture.failure("bt_unassign", {{"entity", enemy}}), "Play mode"));
        CHECK(contains(fixture.failure("bt_set_blackboard", {{"entity", enemy}, {"values", {{"hits", 1}}}}), "Play mode"));
        CHECK(contains(fixture.failure("component_set", {{"entity", enemy}, {"type", "BehaviorTree"},
                                                         {"data", {{"enabled", false}}}}),
                       "Play mode"));
        CHECK(fixture.undoStack.size() == steps);

        // Asset writes are allowed in Play mode.
        fixture.callOk("bt_create", {{"path", "ai/Later.vfBehaviorTree"}, {"tree", treeWith({{"type", "Wait"}})}});
        fixture.callOk("bt_edit", {{"path", "ai/EnemyAI.vfBehaviorTree"}, {"ops", nlohmann::json::array({
            {{"op", "setParams"}, {"node", "In Chase Range"}, {"params", {{"compareValue", 15}}}}})}});
        CHECK(property<float>(nodeNamed(fixture.load("ai/EnemyAI.vfBehaviorTree"), "In Chase Range"), "compareValue") == 15.0f);
        fixture.callOk("bt_set_blackboard", {{"path", "ai/Later.vfBehaviorTree"},
                                             {"keys", nlohmann::json::array({{{"name", "x"}, {"type", "Int"}}})}});

        CHECK(contains(fixture.failure("bt_set_blackboard", {{"entity", enemy}, {"values", {{"hits", 1}}}, {"live", true}}),
                       "no running behavior tree"));

        // A running tree: live values are type-checked against its keys and written to the runtime.
        bt::BTRuntimeState state;
        state.attached = true;
        state.enabled = true;
        state.status = "running";
        auto tree = std::make_shared<bt::BehaviorTreeData>(fixture.load("ai/EnemyAI.vfBehaviorTree"));
        state.tree = tree;
        state.treePath = fixture.file("ai/EnemyAI.vfBehaviorTree").generic_string();
        fixture.provider.runtime[enemy] = {state};
        CHECK(contains(fixture.argError("bt_set_blackboard", {{"entity", enemy}, {"values", {{"hits", 1.5}}}, {"live", true}}),
                       "values.hits: must be an integer"));
        CHECK(contains(fixture.argError("bt_set_blackboard", {{"entity", enemy}, {"values", {{"hits", nullptr}}}, {"live", true}}),
                       "cannot be removed"));
        const nlohmann::json live = fixture.callOk("bt_set_blackboard", {{"entity", enemy}, {"live", true},
                                                                        {"values", {{"hits", 4}, {"target", player}}}});
        CHECK(live["set"]["hits"] == 4);
        REQUIRE(fixture.provider.writes.size() == 2);
        CHECK(std::get<int32_t>(fixture.provider.writes[0].value) == 4);
        CHECK(std::get<services::EntityHandle>(fixture.provider.writes[1].value).id == player);
        CHECK(fixture.undoStack.size() == steps);  // live values are not undoable

        fixture.playMode = false;
        fixture.loadPending = true;
        CHECK(contains(fixture.failure("bt_assign", {{"entity", enemy}, {"tree", "ai/EnemyAI.vfBehaviorTree"}}),
                       "scene load"));
    }

    TEST_CASE("bt_debug_state maps the live runtime, flags SubTree nodes and samples leaf switches")
    {
        BTToolFixture fixture("VertexForge_McpBt_Debug");
        const uint32_t enemy = fixture.makeEntity("Enemy");
        const uint32_t player = fixture.makeEntity("Player");

        const nlohmann::json edit = fixture.callWorker("bt_debug_state", {{"entity", enemy}}).structured;
        CHECK(edit["attached"] == false);
        CHECK(edit["mode"] == "edit");
        CHECK(contains(edit["note"].get<std::string>(), "Play mode"));

        // Root 1 -> Brain 2 -> [Idle 3, Sub Main 10 (spliced from SubTree node 4) -> Sub Wait 11]
        auto tree = std::make_shared<bt::BehaviorTreeData>();
        tree->name = "Live";
        const auto addNode = [&tree](uint32_t id, bt::BTNodeType type, const char* name)
        {
            bt::BTNode node;
            node.id = id;
            node.type = type;
            node.name = name;
            tree->graph.nodes.push_back(node);
        };
        addNode(1, bt::BTNodeType::Root, "Root");
        addNode(2, bt::BTNodeType::Selector, "Brain");
        addNode(3, bt::BTNodeType::Wait, "Idle");
        addNode(10, bt::BTNodeType::Sequence, "Sub Main");
        addNode(11, bt::BTNodeType::Wait, "Sub Wait");
        tree->graph.links = {bt::BTLink{1, 1, 2, 0}, bt::BTLink{2, 2, 3, 0}, bt::BTLink{3, 2, 10, 1},
                             bt::BTLink{4, 10, 11, 0}};
        tree->graph.rootNodeId = 1;

        bt::BTRuntimeState deep;
        deep.attached = true;
        deep.enabled = true;
        deep.status = "running";
        deep.tickCount = 42;
        deep.treePath = fixture.file("ai/Live.vfBehaviorTree").generic_string();
        deep.tree = tree;
        deep.snapshot.valid = true;
        deep.snapshot.activePath = {1, 2, 10, 11};
        deep.snapshot.nodeStatuses = {{1, bt::BTNodeStatus::Running}, {2, bt::BTNodeStatus::Running},
                                      {3, bt::BTNodeStatus::Failure}, {10, bt::BTNodeStatus::Running},
                                      {11, bt::BTNodeStatus::Running}};
        deep.snapshot.elapsedTimes = {{11, 0.25f}};
        deep.snapshot.subtreeEntryMap = {{4, 10}};
        deep.snapshot.blackboard = {{"dist", bt::BlackboardValue{3.5f}}, {"target", bt::BlackboardValue{handleOf(player)}}};

        bt::BTRuntimeState idle = deep;
        idle.snapshot.activePath = {1, 2, 3};
        idle.snapshot.nodeStatuses = {{1, bt::BTNodeStatus::Running}, {2, bt::BTNodeStatus::Running},
                                      {3, bt::BTNodeStatus::Running}};
        idle.tickCount = 50;

        fixture.provider.runtime[enemy] = {deep};
        fixture.playMode = true;
        const nlohmann::json state = fixture.callWorker("bt_debug_state", {{"entity", enemy}}).structured;
        CHECK(state["attached"] == true);
        CHECK(state["tick"] == 42);
        CHECK(state["tree"] == "ai/Live.vfBehaviorTree");
        REQUIRE(state["activePath"].size() == 3);  // the Root is implicit
        CHECK(state["activePath"][0] == nlohmann::json{{"id", 2}, {"name", "Brain"}, {"type", "Selector"}});
        CHECK(state["activePath"][1] == nlohmann::json{{"id", 10}, {"name", "Sub Main"}, {"type", "Sequence"},
                                                       {"subtree", true}, {"subtreeNode", 4}});
        CHECK(state["activeLeaf"]["id"] == 11);
        CHECK(state["activeLeaf"]["subtree"] == true);
        REQUIRE(state["running"].size() == 3);
        CHECK(state["running"][2]["elapsed"] == 0.25);
        CHECK(state["blackboard"]["dist"] == 3.5);
        CHECK(state["blackboard"]["target"] == nlohmann::json{{"entity", player}, {"name", "Player"}});
        CHECK(fixture.provider.debugTargets.empty());  // the editor's debug target is never touched

        // Sampling: the running leaf moves from the SubTree's wait to Idle.
        fixture.provider.runtime[enemy] = {deep, deep, idle};
        fixture.provider.reads.clear();
        const nlohmann::json sampled =
            fixture.callWorker("bt_debug_state", {{"entity", enemy}, {"sampleSeconds", 0.3}, {"sampleIntervalMs", 50}}).structured;
        REQUIRE(sampled["samples"].size() >= 3);
        CHECK(sampled["samples"][0]["leaf"]["name"] == "Sub Wait");
        REQUIRE(sampled["switches"].size() >= 1);
        CHECK(sampled["switches"][0]["from"] == "Sub Wait");
        CHECK(sampled["switches"][0]["to"] == "Idle");
        CHECK(sampled["activeLeaf"]["name"] == "Idle");

        CHECK_THROWS_AS((fixture.callWorker("bt_debug_state", {{"entity", enemy}, {"sampleSeconds", 11}})), mcp::ArgError);
        CHECK_THROWS_AS((fixture.callWorker("bt_debug_state", {{"entity", enemy}, {"sampleIntervalMs", 10}})),
                        mcp::ArgError);
    }

    TEST_CASE("bt_list_node_types lists the catalogue, EQS templates and ScriptTask scripts")
    {
        BTToolFixture fixture("VertexForge_McpBt_List");
        writeEnemyScripts(fixture);
        fixture.eqsQueries["Near"] = true;

        const nlohmann::json all = fixture.callWorker("bt_list_node_types", nlohmann::json::object()).structured;
        CHECK(all["nodes"].size() == bt::catalog::nodes().size());
        REQUIRE(all["scriptTasks"].size() == 1);  // Idle has no tick; lib/ is not listed
        CHECK(all["scriptTasks"][0]["path"] == "game/ai/EnemyMeleeAttack.mt");
        CHECK(all["scriptTasks"][0]["class"] == "EnemyMeleeAttack");
        CHECK(all["scriptTasks"][0]["hooks"]["onAbort"] == true);
        CHECK(all["scriptTasks"][0]["hooks"]["onEnd"] == false);
        REQUIRE(all["eqsQueries"].size() == 1);
        CHECK(all["eqsQueries"][0]["name"] == "Near");

        const nlohmann::json moveTo = fixture.callWorker("bt_list_node_types", {{"type", "moveto"}}).structured;
        REQUIRE(moveTo["nodes"].size() == 1);
        CHECK(moveTo["nodes"][0]["type"] == "MoveTo");
        CHECK(moveTo["nodes"][0]["params"][0]["name"] == "targetKey");
        CHECK(moveTo["nodes"][0]["params"][0]["keyTypes"] == nlohmann::json::array({"Vec3", "Entity", "Int"}));
        CHECK_THROWS_AS((fixture.callWorker("bt_list_node_types", {{"type", "Teleport"}})), mcp::ArgError);
        CHECK_FALSE(fixture.callWorker("bt_list_node_types", {{"includeScripts", false}}).structured.contains("scriptTasks"));
    }

    TEST_CASE("vf://docs/bt has its twelve sections and the generated catalogues")
    {
        const std::string doc = bttool::buildBehaviorTreeDoc();
        for (const char* heading : {"## 1. Ticking model", "## 2. Workflow", "## 3. The tree JSON", "## 4. Node catalogue",
                                    "## 5. Reactivity", "## 6. Movement", "## 7. Blackboard", "## 8. EQS",
                                    "## 9. ScriptTasks", "## 10. Worked example", "## 11. Debugging", "## 12. Limits"})
        {
            CAPTURE(heading);
            CHECK(contains(doc, heading));
        }
        for (const bt::catalog::NodeSpec& spec : bt::catalog::nodes())
        {
            CAPTURE(spec.name);
            CHECK(contains(doc, "### " + spec.name + " - "));
        }
        CHECK(contains(doc, "**Ring**"));
        CHECK(contains(doc, "**Trace** (boolean)"));
        CHECK(contains(doc, "must keep returning Running while its condition holds"));
        CHECK(contains(doc, bttool::workedExampleTree()));
        CHECK(contains(doc, "section 12 of vf://docs/mtype-api"));
    }
}
