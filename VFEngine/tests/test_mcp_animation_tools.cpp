#include "test_mcp_animation_fixture.hpp"

#include "tools/AnimationToolSupport.hpp"

#include "animator/AnimatorValidation.hpp"
#include "retargeting/RetargetBoneMapping.hpp"

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// VK-1656: the MCP animation tools (animation_list_clips, animation_get_state_machine,
// animation_set_state_machine, animation_preview, animation_get_runtime_state) and the pure edit /
// layout / coverage helpers behind them (mcp/tools/AnimationToolSupport).
namespace
{
    using mcpanim::contains;
    namespace anim = mcp::tools::anim;
    using animator::AnimatorParameterType;
    using animator::ComparisonOperator;

    const std::vector<std::string> heroBones{"Hips", "Spine", "Head", "LeftArm"};
    const std::vector<int32_t> heroParents{-1, 0, 1, 1};

    asset::AssetRef fakeClip(uint64_t id, const std::string& name)
    {
        return asset::AssetRef::fromGUIDAndPath(asset::AssetGUID::fromValue(id), "clips/" + name);
    }

    // Pure tests resolve clips by name, without files or an asset database.
    anim::ClipResolver fakeClips()
    {
        return [](const std::string& path, const std::string& argument) -> asset::AssetRef
        {
            static const std::map<std::string, uint64_t> known{
                {"Idle.vfAnim", 1}, {"Run.vfAnim", 2}, {"Walk.vfAnim", 3}, {"Jump.vfAnim", 4}};
            const auto it = known.find(path);
            if (it == known.end())
            {
                throw mcp::ArgError("'" + argument + "': no clip " + path);
            }
            return fakeClip(it->second, path);
        };
    }

    nlohmann::json hexNamer(const asset::AssetRef& ref)
    {
        return ref.isValid() ? nlohmann::json(ref.toHexString()) : nlohmann::json(nullptr);
    }

    // Idle (the default) and Run on Float "speed": Idle -> Run when speed > 0.1, back when <= 0.1.
    nlohmann::json locomotionEdit(const std::string& idleClip, const std::string& runClip)
    {
        return {
            {"parameters", nlohmann::json::array({{{"name", "speed"}, {"type", "Float"}, {"default", 0.0}}})},
            {"states", nlohmann::json::array({{{"name", "Idle"}, {"clip", idleClip}},
                                              {{"name", "Run"}, {"clip", runClip}}})},
            {"transitions", nlohmann::json::array({
                {{"from", "Idle"}, {"to", "Run"}, {"blendDuration", 0.2},
                 {"conditions", nlohmann::json::array({{{"parameter", "speed"}, {"op", ">"}, {"value", 0.1}}})}},
                {{"from", "Run"}, {"to", "Idle"}, {"blendDuration", 0.25},
                 {"conditions", nlohmann::json::array({{{"parameter", "speed"}, {"op", "<="}, {"value", 0.1}}})}}
            })},
            {"defaultState", "Idle"}
        };
    }

    animator::AnimatorData locomotion()
    {
        animator::AnimatorData data = anim::newAnimator("Hero", "Idle", fakeClip(1, "Idle.vfAnim"), 1.0f, true);
        anim::applyAnimatorEdit(data, locomotionEdit("Idle.vfAnim", "Run.vfAnim"), 0, fakeClips());
        return data;
    }

    std::string argErrorOf(animator::AnimatorData data, const nlohmann::json& edit, uint32_t layer = 0)
    {
        try
        {
            anim::applyAnimatorEdit(data, edit, layer, fakeClips());
        }
        catch (const mcp::ArgError& e)
        {
            return e.what();
        }
        catch (const std::exception& e)
        {
            return std::string("<not an ArgError: ") + e.what() + ">";
        }
        return "<no error>";
    }

    const animator::AnimatorTransition* transitionBetween(const animator::AnimatorGraph& graph, const std::string& from,
                                                          const std::string& to)
    {
        const animator::AnimatorState* source = graph.findStateByName(from);
        const animator::AnimatorState* target = graph.findStateByName(to);
        const uint32_t sourceId = from == "Any" ? 0 : (source != nullptr ? source->id : UINT32_MAX);
        for (const animator::AnimatorTransition& transition : graph.transitions)
        {
            if (transition.sourceStateId == sourceId && target != nullptr && transition.targetStateId == target->id)
            {
                return &transition;
            }
        }
        return nullptr;
    }

    // A two-layer animator: "Base" (Idle) and "Upper" (Wave), each with its own parameter.
    animator::AnimatorData layered()
    {
        animator::AnimatorData data;
        data.name = "Layered";
        animator::AnimationLayerData base;
        base.name = "Base";
        base.graph = anim::newAnimator("x", "Idle", fakeClip(1, "Idle.vfAnim"), 1.0f, true).graph;
        base.graph.parameters.push_back({"speed", AnimatorParameterType::Float, 0.0f});
        animator::AnimationLayerData upper;
        upper.name = "Upper";
        upper.graph = anim::newAnimator("x", "Wave", fakeClip(4, "Jump.vfAnim"), 1.0f, true).graph;
        upper.graph.parameters.push_back({"waving", AnimatorParameterType::Bool, false});
        data.layers = {base, upper};
        data.graph = data.layers[0].graph;
        return data;
    }

    // Entities: 1 "Hero" (skinned, plays Hero.vfAnimator), 4 "Ally" (skinned, no animator),
    // 5 "Guard" (skinned, plays Other.vfAnimator), 2 "Crate" (static).
    struct ToolFixture : mcpanim::AnimationFixture
    {
        asset::AssetRef heroMesh;
        asset::AssetRef heroAnimator;
        asset::AssetRef otherAnimator;

        ToolFixture()
            : mcpanim::AnimationFixture("VertexForge_McpAnimationTools")
        {
            heroMesh = engine.addMesh("characters/hero/Hero.vfMesh", heroBones, heroParents);
            const asset::AssetRef idle = engine.addClip("characters/hero/Hero_Idle.vfAnim", heroBones);
            engine.addClip("characters/hero/Hero_Run.vfAnim", {"Hips", "Spine", "Head"});
            engine.addClip("characters/hero/Hero_Wave.vfAnim", {"Spine", "Head", "LeftArm"});
            engine.addClip("other/Fall.vfAnim", {"Hips"});
            heroAnimator = engine.addAnimator("characters/hero/Hero.vfAnimator",
                                              anim::newAnimator("Hero", "Idle", idle, 1.0f, true));
            otherAnimator = engine.addAnimator("anims/Other.vfAnimator",
                                               anim::newAnimator("Other", "Idle", idle, 1.0f, true));
            const asset::AssetRef crate = engine.addMesh("props/Crate.vfMesh", {}, {});

            engine.addEntity(1, "Hero", heroMesh);
            engine.meshOf(1).animatorRef = heroAnimator;
            engine.addEntity(4, "Ally", heroMesh);
            engine.addEntity(5, "Guard", heroMesh);
            engine.meshOf(5).animatorRef = otherAnimator;
            engine.addEntity(2, "Crate", crate);
        }

        nlohmann::json heroGraph()
        {
            mcpanim::StoredAnimator* stored = engine.animatorAt("characters/hero/Hero.vfAnimator");
            REQUIRE(stored != nullptr);
            return anim::graphJson(anim::baseGraph(stored->data), hexNamer);
        }
    };
}

TEST_SUITE("McpAnimationTools")
{
    TEST_CASE("registering the animation tools dispatches nothing")
    {
        mcpanim::DispatcherScope scope; // no handlers at all: any dispatch would throw
        mcp::MainThreadQueue queue;
        mcp::ToolRegistry registry;
        const mcp::tools::ToolContext context{queue};
        CHECK_NOTHROW(mcp::tools::registerAnimationTools(registry, context));
        CHECK(registry.size() == 5);
    }

    TEST_CASE("five Main-affinity tools with closed schemas and their annotations")
    {
        ToolFixture fixture;
        struct Expected
        {
            const char* name;
            bool readOnly;
            bool destructive;
            long long timeoutMs;
        };
        for (const Expected& entry : {Expected{"animation_list_clips", true, false, 30000},
                                      Expected{"animation_get_state_machine", true, false, 10000},
                                      Expected{"animation_set_state_machine", false, true, 10000},
                                      Expected{"animation_preview", false, false, 10000},
                                      Expected{"animation_get_runtime_state", true, false, 10000}})
        {
            CAPTURE(entry.name);
            const mcp::ToolDef& def = fixture.tool(entry.name);
            CHECK(def.affinity == mcp::ThreadAffinity::Main);
            CHECK(def.readOnly == entry.readOnly);
            CHECK(def.destructive == entry.destructive);
            CHECK(def.timeout.count() == entry.timeoutMs);
            CHECK(def.inputSchema["type"] == "object");
            CHECK(def.inputSchema["additionalProperties"] == false);
        }
        const nlohmann::json& states = fixture.tool("animation_set_state_machine").inputSchema["properties"]["states"];
        CHECK(states["items"]["additionalProperties"] == false);
        CHECK(states["items"]["properties"].contains("blendTree"));
        CHECK(contains(fixture.argError("animation_list_clips", {{"entity", 1}, {"foo", 1}}), "unknown argument 'foo'"));
    }

    // ------------------------------------------------------------------
    // Pure edit semantics
    // ------------------------------------------------------------------

    TEST_CASE("applyAnimatorEdit upserts by name and lays new states out on the grid")
    {
        animator::AnimatorData data = anim::newAnimator("Hero", "Idle", fakeClip(1, "Idle.vfAnim"), 1.0f, true);
        const anim::EditSummary summary = anim::applyAnimatorEdit(data, {
            {"parameters", nlohmann::json::array({{{"name", "speed"}, {"type", "float"}}})},
            {"states", nlohmann::json::array({{{"name", "Idle"}, {"playbackSpeed", 1.5}},
                                              {{"name", "Run"}, {"clip", "Run.vfAnim"}, {"loop", false}}})}
        }, 0, fakeClips());

        const animator::AnimatorGraph& graph = data.graph;
        REQUIRE(graph.states.size() == 2);
        CHECK(graph.states[0].id == 1);
        CHECK(graph.states[0].playbackSpeed == 1.5f);
        CHECK(graph.states[0].animationRef == fakeClip(1, "Idle.vfAnim"));
        CHECK(graph.states[1].id == 2);
        CHECK(graph.states[1].name == "Run");
        CHECK(graph.states[1].animationRef == fakeClip(2, "Run.vfAnim"));
        CHECK_FALSE(graph.states[1].loop);
        CHECK(graph.states[1].position == glm::vec2(550.0f, 100.0f)); // (300, 100) holds Idle
        CHECK(graph.nextStateId == 3);
        REQUIRE(graph.parameters.size() == 1);
        CHECK(graph.parameters[0].type == AnimatorParameterType::Float);
        CHECK(std::get<float>(graph.parameters[0].defaultValue) == 0.0f);
        CHECK(summary.statesAdded == std::vector<std::string>{"Run"});
        CHECK(summary.statesUpdated == std::vector<std::string>{"Idle"});
        CHECK(summary.parametersAdded == std::vector<std::string>{"speed"});

        // Updating a parameter's type resets its default to that type's zero unless one is given.
        anim::applyAnimatorEdit(data, {{"parameters", nlohmann::json::array({{{"name", "speed"}, {"type", "Int"}}})}},
                                0, fakeClips());
        CHECK(graph.parameters[0].type == AnimatorParameterType::Int);
        CHECK(std::get<int32_t>(graph.parameters[0].defaultValue) == 0);
    }

    TEST_CASE("nextFreePosition skips occupied cells and wraps after four columns")
    {
        animator::AnimatorGraph graph;
        CHECK(anim::nextFreePosition(graph) == anim::layoutOrigin);
        for (int i = 0; i < 4; ++i)
        {
            animator::AnimatorState state;
            state.id = static_cast<uint32_t>(i + 1);
            state.position = anim::nextFreePosition(graph);
            graph.states.push_back(state);
        }
        CHECK(graph.states[3].position == glm::vec2(1050.0f, 100.0f));
        CHECK(anim::nextFreePosition(graph) == glm::vec2(300.0f, 250.0f));

        // A node a little off a cell (within 100 px) still blocks it.
        graph.states.clear();
        animator::AnimatorState nearby;
        nearby.position = glm::vec2(340.0f, 160.0f);
        graph.states.push_back(nearby);
        CHECK(anim::nextFreePosition(graph) == glm::vec2(550.0f, 100.0f));
    }

    TEST_CASE("a renamed state keeps its id and transitions; listed pairs replace their transitions in place")
    {
        animator::AnimatorData data = locomotion();
        anim::applyAnimatorEdit(data, {
            {"states", nlohmann::json::array({{{"name", "Jump"}, {"clip", "Jump.vfAnim"}}})},
            {"transitions", nlohmann::json::array({
                {{"from", "Idle"}, {"to", "Jump"}, {"hasExitTime", true}, {"exitTime", 0.9}}})}
        }, 0, fakeClips());
        // Idle -> Run (1), Run -> Idle (2), Idle -> Jump (3).
        REQUIRE(data.graph.transitions.size() == 3);

        const anim::EditSummary summary = anim::applyAnimatorEdit(data, {
            {"states", nlohmann::json::array({{{"name", "Run"}, {"rename", "Sprint"}}})},
            {"transitions", nlohmann::json::array({
                {{"from", "Idle"}, {"to", "Sprint"}, {"conditions", nlohmann::json::array({{{"parameter", "speed"}, {"op", ">"}, {"value", 2}}})}},
                {{"from", "Idle"}, {"to", "Sprint"}, {"conditions", nlohmann::json::array({{{"parameter", "speed"}, {"op", ">"}, {"value", 0.5}}})}}
            })}
        }, 0, fakeClips());

        const animator::AnimatorGraph& graph = data.graph;
        const animator::AnimatorState* sprint = graph.findStateByName("Sprint");
        REQUIRE(sprint != nullptr);
        CHECK(sprint->id == 2);
        CHECK(graph.findStateByName("Run") == nullptr);
        REQUIRE(summary.statesRenamed.size() == 1);
        CHECK(summary.statesRenamed[0] == std::pair<std::string, std::string>{"Run", "Sprint"});

        // The old Idle -> Run (id 1) is replaced at its slot by the two new ones, in the given order.
        REQUIRE(graph.transitions.size() == 4);
        CHECK(graph.transitions[0].targetStateId == 2);
        CHECK(std::get<float>(graph.transitions[0].conditions[0].value) == 2.0f);
        CHECK(graph.transitions[1].targetStateId == 2);
        CHECK(std::get<float>(graph.transitions[1].conditions[0].value) == 0.5f);
        CHECK(graph.transitions[0].id == 4);
        CHECK(graph.transitions[1].id == 5);
        CHECK(graph.transitions[2].id == 2); // Sprint -> Idle kept its id: the rename did not touch it
        CHECK(anim::transitionJson(graph, graph.transitions[2])["from"] == "Sprint");
        CHECK(graph.transitions[3].id == 3);
        CHECK(summary.transitionsRemoved == 1);
        CHECK(summary.transitionsAdded == 2);

        // exitTime enables exit time unless hasExitTime says otherwise.
        const animator::AnimatorTransition* jump = transitionBetween(graph, "Idle", "Jump");
        REQUIRE(jump != nullptr);
        CHECK(jump->hasExitTime);
        CHECK(jump->exitTime == doctest::Approx(0.9f));
        anim::applyAnimatorEdit(data, {{"transitions", nlohmann::json::array({
            {{"from", "Idle"}, {"to", "Jump"}, {"hasExitTime", false}, {"exitTime", 0.5},
             {"conditions", nlohmann::json::array({{{"parameter", "speed"}, {"op", "<"}, {"value", -1}}})}}})}},
            0, fakeClips());
        CHECK_FALSE(transitionBetween(data.graph, "Idle", "Jump")->hasExitTime);
    }

    TEST_CASE("remove drops states with their transitions and needs a new default for the default state")
    {
        animator::AnimatorData data = locomotion();
        CHECK(contains(argErrorOf(data, {{"remove", {{"states", {"Idle"}}}}}), "name the new default state"));
        CHECK(contains(argErrorOf(data, {{"remove", {{"transitions", nlohmann::json::array({{{"from", "Idle"}, {"to", "Idle"}}})}}}}),
                       "there is no transition from Idle to Idle"));
        CHECK(contains(argErrorOf(data, {{"remove", {{"parameters", {"jump"}}}}}), "no parameter named \"jump\""));
        CHECK(contains(argErrorOf(data, {{"replace", true}, {"remove", {{"states", {"Run"}}}}}), "cannot be combined"));

        const anim::EditSummary summary = anim::applyAnimatorEdit(data, {
            {"remove", {{"states", {"Idle"}}, {"parameters", {"speed"}}}},
            {"defaultState", "Run"}
        }, 0, fakeClips());
        CHECK(data.graph.states.size() == 1);
        CHECK(data.graph.transitions.empty());
        CHECK(data.graph.parameters.empty());
        CHECK(data.graph.defaultStateId == data.graph.findStateByName("Run")->id);
        CHECK(summary.transitionsRemoved == 2);
        CHECK(summary.defaultState == std::optional<std::string>("Run"));
    }

    TEST_CASE("replace:true makes each given list the whole list; listed states keep their ids")
    {
        animator::AnimatorData data = locomotion();
        CHECK(contains(argErrorOf(data, {{"replace", true}, {"states", nlohmann::json::array({{{"name", "Run"}}})}}),
                       "default state was removed"));

        anim::applyAnimatorEdit(data, {
            {"replace", true},
            {"states", nlohmann::json::array({{{"name", "Run"}}, {{"name", "Walk"}, {"clip", "Walk.vfAnim"}}})},
            {"defaultState", "Walk"},
            {"transitions", nlohmann::json::array({{{"from", "Walk"}, {"to", "Run"}, {"hasExitTime", true}}})}
        }, 0, fakeClips());

        const animator::AnimatorGraph& graph = data.graph;
        REQUIRE(graph.states.size() == 2);
        CHECK(graph.states[0].name == "Run");
        CHECK(graph.states[0].id == 2);
        CHECK(graph.states[1].name == "Walk");
        CHECK(graph.states[1].id == 3);
        REQUIRE(graph.transitions.size() == 1);
        CHECK(graph.transitions[0].id == 3);
        CHECK(graph.parameters.size() == 1); // not given: kept
        CHECK(graph.defaultStateId == 3);
    }

    TEST_CASE("replace:true with an empty state list clears the graph; there is no default to name")
    {
        animator::AnimatorData data = locomotion();
        const anim::EditSummary summary =
            anim::applyAnimatorEdit(data, {{"replace", true}, {"states", nlohmann::json::array()}}, 0, fakeClips());
        CHECK(data.graph.states.empty());
        CHECK(data.graph.transitions.empty());
        CHECK(data.graph.defaultStateId == 0);
        CHECK(data.graph.parameters.size() == 1); // not given: kept
        CHECK(summary.statesRemoved == std::vector<std::string>{"Idle", "Run"});
        CHECK(summary.transitionsRemoved == 2);
        CHECK_FALSE(animator::hasErrors(animator::validateAnimator(data))); // an empty graph is only a warning
    }

    TEST_CASE("condition values are typed by their parameter")
    {
        animator::AnimatorData data = locomotion();
        anim::applyAnimatorEdit(data, {{"parameters", nlohmann::json::array({
            {{"name", "lane"}, {"type", "Int"}, {"default", 2}},
            {{"name", "grounded"}, {"type", "Bool"}, {"default", true}},
            {{"name", "jump"}, {"type", "Trigger"}}})}}, 0, fakeClips());
        CHECK(std::get<int32_t>(data.graph.findParameter("lane")->defaultValue) == 2);
        CHECK(std::get<bool>(data.graph.findParameter("grounded")->defaultValue));

        const auto condition = [](const nlohmann::json& value)
        {
            return nlohmann::json{{"transitions", nlohmann::json::array({{{"from", "Idle"}, {"to", "Run"},
                                                                         {"conditions", nlohmann::json::array({value})}}})}};
        };
        CHECK(contains(argErrorOf(data, condition({{"parameter", "speed"}, {"op", ">"}, {"value", true}})), "must be a number"));
        CHECK(contains(argErrorOf(data, condition({{"parameter", "lane"}, {"op", "=="}, {"value", 1.5}})), "must be an integer"));
        CHECK(contains(argErrorOf(data, condition({{"parameter", "grounded"}, {"op", ">"}, {"value", true}})), "== or != only"));
        CHECK(contains(argErrorOf(data, condition({{"parameter", "grounded"}, {"op", "=="}, {"value", 1}})), "must be a boolean"));
        CHECK(contains(argErrorOf(data, condition({{"parameter", "jump"}, {"op", "=="}, {"value", false}})), "value true"));
        CHECK(contains(argErrorOf(data, condition({{"parameter", "jump"}, {"op", "!="}})), "value true"));
        CHECK(contains(argErrorOf(data, condition({{"parameter", "speed"}, {"value", 1}})), ".op' is required"));
        CHECK(contains(argErrorOf(data, condition({{"parameter", "fly"}, {"op", ">"}, {"value", 1}})), "declare it in 'parameters'"));
        CHECK(contains(argErrorOf(data, condition({{"parameter", "speed"}, {"op", "=>"}, {"value", 1}})), "must be one of"));

        anim::applyAnimatorEdit(data, condition({{"parameter", "speed"}, {"op", ">="}, {"value", 1}}), 0, fakeClips());
        CHECK(std::get<float>(transitionBetween(data.graph, "Idle", "Run")->conditions[0].value) == 1.0f);
        anim::applyAnimatorEdit(data, condition({{"parameter", "lane"}, {"op", "!="}, {"value", 3.0}}), 0, fakeClips());
        CHECK(std::get<int32_t>(transitionBetween(data.graph, "Idle", "Run")->conditions[0].value) == 3);
        anim::applyAnimatorEdit(data, condition({{"parameter", "grounded"}, {"op", "!="}, {"value", false}}), 0, fakeClips());
        CHECK(transitionBetween(data.graph, "Idle", "Run")->conditions[0].op == ComparisonOperator::NotEqual);
        anim::applyAnimatorEdit(data, condition({{"parameter", "jump"}}), 0, fakeClips());
        const animator::TransitionCondition& trigger = transitionBetween(data.graph, "Idle", "Run")->conditions[0];
        CHECK(trigger.op == ComparisonOperator::Equal);
        CHECK(std::get<bool>(trigger.value));
    }

    TEST_CASE("names, keys and the Any State are checked strictly")
    {
        animator::AnimatorData data = locomotion();
        CHECK(contains(argErrorOf(data, {{"states", nlohmann::json::array({{{"name", "Any"}}})}}), "reserved"));
        CHECK(contains(argErrorOf(data, {{"states", nlohmann::json::array({{{"name", "Idle"}, {"rename", "Run"}}})}}),
                       "already exists"));
        CHECK(contains(argErrorOf(data, {{"states", nlohmann::json::array({{{"name", "Idle"}}, {{"name", "Idle"}}})}}),
                       "listed twice"));
        CHECK(contains(argErrorOf(data, {{"states", nlohmann::json::array({{{"name", "Idle"}, {"events", 1}}})}}),
                       "unknown states[0] key 'events'"));
        CHECK(contains(argErrorOf(data, {{"states", nlohmann::json::array({{{"name", "Idle"}, {"clip", "Nope.vfAnim"}}})}}),
                       "no clip Nope.vfAnim"));
        CHECK(contains(argErrorOf(data, {{"parameters", nlohmann::json::array({{{"name", "jump"}}})}}), "needs a 'type'"));
        CHECK(contains(argErrorOf(data, {{"transitions", nlohmann::json::array({{{"from", "Idle"}, {"to", "Any"}}})}}),
                       "Any State"));
        CHECK(contains(argErrorOf(data, {{"transitions", nlohmann::json::array({{{"from", "Ghost"}, {"to", "Idle"}}})}}),
                       "no state named \"Ghost\""));
        CHECK(contains(argErrorOf(data, {{"defaultState", "Ghost"}}), "no state named \"Ghost\""));
        CHECK(contains(argErrorOf(data, {{"glow", true}}), "unknown edit key 'glow'"));
        CHECK(contains(argErrorOf(data, {{"states", nlohmann::json::array({{{"name", "Run"}, {"blendTree",
            {{"type", "1D"}, {"parameter", "speed"}, {"parameterY", "speed"}, {"entries", nlohmann::json::array()}}}}})}}),
            "only applies to a 2D blend tree"));

        // "Any" is the Any State as a transition source.
        anim::applyAnimatorEdit(data, {{"parameters", nlohmann::json::array({{{"name", "hit"}, {"type", "Trigger"}}})},
                                       {"transitions", nlohmann::json::array({{{"from", "Any"}, {"to", "Idle"},
                                           {"conditions", nlohmann::json::array({{{"parameter", "hit"}}})}}})}},
                                0, fakeClips());
        const animator::AnimatorTransition* any = transitionBetween(data.graph, "Any", "Idle");
        REQUIRE(any != nullptr);
        CHECK(any->sourceStateId == 0);
        CHECK(anim::transitionJson(data.graph, *any)["from"] == "Any");
    }

    TEST_CASE("blend trees: 1D thresholds, 2D positions, null removes")
    {
        animator::AnimatorData data = locomotion();
        anim::applyAnimatorEdit(data, {{"states", nlohmann::json::array({
            {{"name", "Move"}, {"blendTree", {{"type", "1D"}, {"parameter", "speed"}, {"entries", nlohmann::json::array({
                {{"clip", "Walk.vfAnim"}, {"threshold", 1}}, {{"clip", "Run.vfAnim"}, {"threshold", 4}}})}}}},
            {{"name", "Strafe"}, {"blendTree", {{"type", "2d"}, {"parameter", "speed"}, {"parameterY", "speed"},
                {"entries", nlohmann::json::array({{{"clip", "Walk.vfAnim"}, {"position", {0, 1}}}})}}}}})}},
            0, fakeClips());

        const animator::AnimatorState* move = data.graph.findStateByName("Move");
        REQUIRE(move != nullptr);
        REQUIRE(move->blendTree.has_value());
        CHECK(move->blendTree->type == animator::BlendTreeType::BlendTree1D);
        REQUIRE(move->blendTree->entries.size() == 2);
        CHECK(move->blendTree->entries[1].threshold == 4.0f);
        CHECK(move->blendTree->entries[1].animationRef == fakeClip(2, "Run.vfAnim"));
        const animator::AnimatorState* strafe = data.graph.findStateByName("Strafe");
        REQUIRE(strafe != nullptr);
        CHECK(strafe->blendTree->type == animator::BlendTreeType::BlendTree2D);
        CHECK(strafe->blendTree->entries[0].position == glm::vec2(0.0f, 1.0f));
        const nlohmann::json json = anim::stateJson(*strafe, hexNamer);
        CHECK(json["blendTree"]["type"] == "2D");
        CHECK(json["blendTree"]["entries"][0]["position"] == nlohmann::json::array({0.0, 1.0}));

        anim::applyAnimatorEdit(data, {{"states", nlohmann::json::array({{{"name", "Move"}, {"blendTree", nullptr}}})}},
                                0, fakeClips());
        CHECK_FALSE(data.graph.findStateByName("Move")->blendTree.has_value());
    }

    TEST_CASE("a layered file edits one layer and mirrors layer 0 into the top-level graph")
    {
        animator::AnimatorData data = layered();
        // Layer 1 may test layer 0's parameter: the runtime merges every layer's parameters.
        anim::applyAnimatorEdit(data, {
            {"states", nlohmann::json::array({{{"name", "Point"}, {"clip", "Walk.vfAnim"}}})},
            {"transitions", nlohmann::json::array({{{"from", "Wave"}, {"to", "Point"},
                {"conditions", nlohmann::json::array({{{"parameter", "speed"}, {"op", ">"}, {"value", 1}}})}}})}
        }, 1, fakeClips());
        CHECK(data.layers[1].graph.states.size() == 2);
        CHECK(data.layers[0].graph.states.size() == 1);
        CHECK(data.graph.states.size() == 1); // still layer 0

        anim::applyAnimatorEdit(data, {{"states", nlohmann::json::array({{{"name", "Run"}, {"clip", "Run.vfAnim"}}})}}, 0,
                                fakeClips());
        CHECK(data.layers[0].graph.states.size() == 2);
        CHECK(anim::graphJson(data.graph, hexNamer) == anim::graphJson(data.layers[0].graph, hexNamer));

        CHECK(contains(argErrorOf(data, {{"defaultState", "Wave"}}, 2), "has 2 layers"));
        data.layers[1].sourceMode = animator::LayerSourceMode::DirectClip;
        CHECK(contains(argErrorOf(data, {{"defaultState", "Wave"}}, 1), "DirectClip"));
        CHECK(contains(argErrorOf(locomotion(), {{"defaultState", "Idle"}}, 1), "single state machine"));
    }

    TEST_CASE("a parameter declared by several layers has the first layer's type, like the runtime")
    {
        animator::AnimatorData data = layered();
        // Layer 1 declares "speed" again, as a Bool: layer 0's Float declaration wins at runtime.
        data.layers[1].graph.parameters.push_back({"speed", AnimatorParameterType::Bool, false});
        anim::applyAnimatorEdit(data, {
            {"states", nlohmann::json::array({{{"name", "Point"}, {"clip", "Walk.vfAnim"}}})},
            {"transitions", nlohmann::json::array({{{"from", "Wave"}, {"to", "Point"},
                {"conditions", nlohmann::json::array({{{"parameter", "speed"}, {"op", ">"}, {"value", 0.5}}})}}})}
        }, 1, fakeClips());
        const animator::AnimatorTransition* point = transitionBetween(data.layers[1].graph, "Wave", "Point");
        REQUIRE(point != nullptr);
        REQUIRE(point->conditions.size() == 1);
        CHECK(std::get<float>(point->conditions[0].value) == 0.5f);
        CHECK_FALSE(animator::hasErrors(animator::validateAnimator(data)));
    }

    TEST_CASE("clip coverage: native bone names, retarget roles, the root bone decides")
    {
        services::MeshSkeletonInfo skeleton;
        skeleton.success = true;
        skeleton.skinned = true;
        skeleton.boneNames = heroBones;
        skeleton.parents = heroParents;
        CHECK(anim::firstRootBone(skeleton) == std::optional<std::size_t>(0));

        anim::ClipCoverage half = anim::nativeCoverage(skeleton, {"Hips", "Spine", "Tail"}, 0.5f);
        CHECK(half.matchedBones == 2);
        CHECK(half.skeletonBones == 4);
        CHECK(half.coverage == 0.5f);
        CHECK(half.rootMatched);
        CHECK(half.compatible);
        CHECK_FALSE(anim::nativeCoverage(skeleton, {"Hips", "Spine"}, 0.6f).compatible);
        const anim::ClipCoverage rootless = anim::nativeCoverage(skeleton, {"Spine", "Head", "LeftArm"}, 0.5f);
        CHECK(rootless.coverage == 0.75f);
        CHECK_FALSE(rootless.rootMatched);
        CHECK_FALSE(rootless.compatible);

        retargeting::HumanoidRigData source;
        source.bindings = {{retargeting::HumanoidBoneRole::Hips, "mixamorig:Hips"},
                           {retargeting::HumanoidBoneRole::Spine, "mixamorig:Spine"},
                           {retargeting::HumanoidBoneRole::Head, "mixamorig:Head"}};
        retargeting::HumanoidRigData target;
        target.bindings = {{retargeting::HumanoidBoneRole::Hips, "Hips"},
                           {retargeting::HumanoidBoneRole::Spine, "Spine"},
                           {retargeting::HumanoidBoneRole::Head, "Head"}};
        retargeting::RetargetMapData map;
        const retargeting::RetargetBoneMapping mapping(source, target, map);
        const anim::ClipCoverage retargeted =
            anim::retargetCoverage(skeleton, mapping, {"mixamorig:Hips", "mixamorig:Spine", "Head"}, 0.5f);
        CHECK(retargeted.matchedBones == 2); // "Head" is not the source rig's head bone: no fallback by name
        CHECK(retargeted.rootMatched);
        CHECK(retargeted.compatible);

        // A non-humanoid bone above the hips: a retargeted pose is rooted at the Hips role, so the Hips bone
        // decides there; played natively the skeleton's first root bone still does.
        services::MeshSkeletonInfo rooted = skeleton;
        rooted.boneNames = {"Root", "Hips", "Spine", "Head"};
        rooted.parents = {-1, 0, 1, 2};
        CHECK(anim::retargetRootBone(rooted, mapping) == std::optional<std::size_t>(1));
        const anim::ClipCoverage underRoot =
            anim::retargetCoverage(rooted, mapping, {"mixamorig:Hips", "mixamorig:Spine", "mixamorig:Head"}, 0.5f);
        CHECK(underRoot.matchedBones == 3);
        CHECK(underRoot.rootMatched);
        CHECK(underRoot.compatible);
        CHECK_FALSE(anim::nativeCoverage(rooted, {"Hips", "Spine", "Head"}, 0.5f).rootMatched);

        map.overrides.push_back({retargeting::HumanoidBoneRole::Hips, false, false});
        const retargeting::RetargetBoneMapping disabled(source, target, map);
        CHECK_FALSE(anim::retargetCoverage(skeleton, disabled, {"mixamorig:Hips", "mixamorig:Spine"}, 0.0f).rootMatched);
    }

    // ------------------------------------------------------------------
    // Tools against the fake engine
    // ------------------------------------------------------------------

    TEST_CASE("animation_set_state_machine edits an existing animator; undo and redo rewrite the file")
    {
        ToolFixture fixture;
        const nlohmann::json before = fixture.heroGraph();
        nlohmann::json args = locomotionEdit("characters/hero/Hero_Idle.vfAnim", "characters/hero/Hero_Run.vfAnim");
        args["entity"] = 1;

        const nlohmann::json result = fixture.callOk("animation_set_state_machine", args);
        CHECK(result["animator"] == "characters/hero/Hero.vfAnimator");
        CHECK(result["created"] == false);
        CHECK(result["saved"] == true);
        CHECK(result["defaultState"] == "Idle");
        CHECK(result["states"].size() == 2);
        CHECK(result["transitions"].size() == 2);
        CHECK(result["transitions"][0]["conditions"][0] == nlohmann::json{{"parameter", "speed"}, {"op", ">"}, {"value", 0.1}});
        CHECK(result["states"][1]["clip"] == "characters/hero/Hero_Run.vfAnim");
        CHECK(result["changes"]["statesAdded"] == nlohmann::json::array({"Run"}));
        CHECK(result["usedBy"] == nlohmann::json::array({{{"entity", 1}, {"name", "Hero"}}}));
        for (const nlohmann::json& issue : result["issues"])
        {
            CHECK(issue["level"] == "warning");
        }

        const nlohmann::json after = fixture.heroGraph();
        CHECK(after != before);
        REQUIRE(fixture.engine.undoStack.size() == 1);
        CHECK(mcpanim::batchSize(fixture.engine.undoStack[0]) == 1); // the asset entry only
        CHECK(fixture.engine.undoStack[0]->getDescription() == "MCP: Edit animator Hero");
        CHECK(fixture.engine.undoStack[0]->getMemoryFootprint() > 0);

        fixture.engine.undoStack[0]->undo();
        CHECK(fixture.heroGraph() == before);
        fixture.engine.undoStack[0]->execute();
        CHECK(fixture.heroGraph() == after);
        fixture.engine.undoStack[0]->undo();
        CHECK(fixture.heroGraph() == before);
    }

    TEST_CASE("an animator undo is skipped when the file changed since the edit")
    {
        ToolFixture fixture;
        fixture.callOk("animation_set_state_machine",
                       {{"animatorRef", "characters/hero/Hero.vfAnimator"},
                        {"states", nlohmann::json::array({{{"name", "Idle"}, {"playbackSpeed", 2}}})}});
        REQUIRE(fixture.engine.undoStack.size() == 1);

        // Someone else saves the file (the Animator Editor, say).
        mcpanim::StoredAnimator* stored = fixture.engine.animatorAt("characters/hero/Hero.vfAnimator");
        REQUIRE(stored != nullptr);
        stored->data.graph.states[0].playbackSpeed = 3.0f;
        stored->hash = 424242;

        CHECK_NOTHROW(fixture.engine.undoStack[0]->undo()); // logged and consumed, not thrown
        CHECK(stored->data.graph.states[0].playbackSpeed == 3.0f);
        CHECK(stored->hash == 424242u);
    }

    TEST_CASE("validation errors fail the call and nothing is saved")
    {
        ToolFixture fixture;
        const nlohmann::json before = fixture.heroGraph();
        const std::string error = fixture.failure("animation_set_state_machine", {
            {"entity", 1},
            {"states", nlohmann::json::array({{{"name", "Run"}, {"clip", "characters/hero/Hero_Run.vfAnim"}}})},
            {"transitions", nlohmann::json::array({{{"from", "Any"}, {"to", "Run"}}})}
        });
        CHECK(contains(error, "was not saved"));
        CHECK(contains(error, "fires after every blend"));
        CHECK(fixture.heroGraph() == before);
        CHECK_FALSE(fixture.engine.logged("SaveAnimator"));
        CHECK(fixture.engine.undoStack.empty());

        // Bad arguments are argument errors before anything is read.
        CHECK(contains(fixture.argError("animation_set_state_machine", {{"entity", 1}, {"animatorRef", "x.vfAnimator"},
                                                                        {"defaultState", "Idle"}}),
                       "exactly one of"));
        CHECK(contains(fixture.argError("animation_set_state_machine", {{"entity", 1}}), "nothing to do"));
        CHECK(contains(fixture.argError("animation_set_state_machine", {{"entity", 1}, {"states", nlohmann::json::array({
                           {{"name", "Run"}, {"clip", "characters/hero/Missing.vfAnim"}}})}}), "no clip at"));
    }

    TEST_CASE("create: a new animator next to the entity's mesh, assigned; the file itself is not undone")
    {
        ToolFixture fixture;
        nlohmann::json args = locomotionEdit("characters/hero/Hero_Idle.vfAnim", "characters/hero/Hero_Run.vfAnim");
        args["entity"] = 4;
        CHECK(contains(fixture.failure("animation_set_state_machine", args), "pass create:true"));

        args["create"] = true;
        const nlohmann::json result = fixture.callOk("animation_set_state_machine", args);
        CHECK(result["created"] == true);
        CHECK(result["animator"] == "characters/hero/Ally.vfAnimator");
        CHECK(result["assigned"] == nlohmann::json::array({4}));
        mcpanim::StoredAnimator* created = fixture.engine.animatorAt("characters/hero/Ally.vfAnimator");
        REQUIRE(created != nullptr);
        CHECK(created->data.name == "Ally");
        CHECK(created->data.graph.states.size() == 2);
        CHECK(fixture.engine.meshOf(4).animatorRef.isValid());

        REQUIRE(fixture.engine.undoStack.size() == 1);
        CHECK(mcpanim::batchSize(fixture.engine.undoStack[0]) == 1); // the assignment, no asset entry
        fixture.engine.undoStack[0]->undo();
        CHECK_FALSE(fixture.engine.meshOf(4).animatorRef.isValid());
        CHECK(fixture.engine.animatorAt("characters/hero/Ally.vfAnimator") != nullptr);

        // A new file whose state machine has errors is not created, and nothing is assigned.
        CHECK(contains(fixture.failure("animation_set_state_machine", {
                           {"animatorRef", "anims/Broken.vfAnimator"}, {"create", true}, {"assignTo", {5}},
                           {"states", nlohmann::json::array({{{"name", "Idle"}, {"clip", "characters/hero/Hero_Idle.vfAnim"}}})},
                           {"transitions", nlohmann::json::array({{{"from", "Any"}, {"to", "Idle"}}})}}),
                       "was not saved"));
        CHECK(fixture.engine.animatorAt("anims/Broken.vfAnimator") == nullptr);
        CHECK(fixture.engine.meshOf(5).animatorRef == fixture.otherAnimator);
    }

    TEST_CASE("assignTo assigns in the same undo step as the edit")
    {
        ToolFixture fixture;
        const nlohmann::json before = fixture.heroGraph();
        CHECK(contains(fixture.failure("animation_set_state_machine",
                                       {{"entity", 1}, {"defaultState", "Idle"}, {"assignTo", {4, 2}}}), "static"));
        CHECK(fixture.engine.undoStack.empty());
        CHECK_FALSE(fixture.engine.meshOf(4).animatorRef.isValid());
        // Ally's Mesh already names a retarget map: undoing the assignment must keep it.
        const asset::AssetRef retarget = fixture.engine.addFile("rigs/MixamoToHero.vfretarget");
        fixture.engine.meshOf(4).retargetRef = retarget;

        const nlohmann::json result = fixture.callOk("animation_set_state_machine", {
            {"animatorRef", "characters/hero/Hero.vfAnimator"},
            {"states", nlohmann::json::array({{{"name", "Idle"}, {"loop", false}}})},
            {"assignTo", {4, 5, 1}}
        });
        CHECK(result["assigned"] == nlohmann::json::array({4, 5})); // 1 already plays it
        CHECK(result["usedBy"].size() == 3);
        CHECK(fixture.engine.meshOf(4).animatorRef == fixture.heroAnimator);
        CHECK(fixture.engine.meshOf(5).animatorRef == fixture.heroAnimator);

        REQUIRE(fixture.engine.undoStack.size() == 1);
        CHECK(mcpanim::batchSize(fixture.engine.undoStack[0]) == 3); // asset + Ally (added) + Guard (changed)
        fixture.engine.undoStack[0]->undo();
        CHECK_FALSE(fixture.engine.meshOf(4).animatorRef.isValid());
        CHECK(fixture.engine.meshOf(4).retargetRef == retarget);
        CHECK(fixture.engine.meshOf(5).animatorRef == fixture.otherAnimator);
        CHECK(fixture.heroGraph() == before);
        fixture.engine.undoStack[0]->execute();
        CHECK(fixture.engine.meshOf(4).animatorRef == fixture.heroAnimator);
        CHECK(fixture.engine.meshOf(5).animatorRef == fixture.heroAnimator);
        CHECK(fixture.heroGraph() != before);
    }

    TEST_CASE("animation_set_state_machine and animation_preview refuse in Play mode and during a scene load")
    {
        ToolFixture fixture;
        const nlohmann::json edit{{"entity", 1}, {"defaultState", "Idle"}};
        fixture.engine.playMode = true;
        CHECK(contains(fixture.failure("animation_set_state_machine", edit), "Play mode"));
        CHECK(contains(fixture.failure("animation_preview", {{"entity", 1}, {"state", "Idle"}}), "animation_get_runtime_state"));
        fixture.engine.playMode = false;
        fixture.engine.loadPending = true;
        CHECK(contains(fixture.failure("animation_set_state_machine", edit), "scene load"));
        CHECK_FALSE(fixture.engine.logged("SaveAnimator"));
        CHECK_FALSE(fixture.engine.logged("StartPreview"));

        // Reading works whatever the mode.
        fixture.engine.loadPending = false;
        fixture.engine.playMode = true;
        CHECK_FALSE(fixture.call("animation_get_state_machine", {{"entity", 1}}).isError);
    }

    TEST_CASE("animation_get_state_machine reports the graph, the layers, the issues and the users")
    {
        ToolFixture fixture;
        fixture.engine.meshOf(4).animatorRef = fixture.heroAnimator;

        const nlohmann::json result = fixture.callOk("animation_get_state_machine",
                                                     {{"animatorRef", "characters/hero/Hero.vfAnimator"}});
        CHECK(result["name"] == "Hero");
        CHECK(result["layered"] == false);
        REQUIRE(result["layers"].size() == 1);
        CHECK(result["layers"][0]["sourceMode"] == "StateMachine");
        CHECK(result["layer"] == 0);
        CHECK(result["defaultState"] == "Idle");
        CHECK(result["states"][0]["clip"] == "characters/hero/Hero_Idle.vfAnim");
        CHECK(result["states"][0]["position"] == nlohmann::json::array({300.0, 100.0}));
        CHECK(result["usedBy"] == nlohmann::json::array({{{"entity", 1}, {"name", "Hero"}}, {{"entity", 4}, {"name", "Ally"}}}));
        CHECK(result["issues"].is_array());

        CHECK(contains(fixture.failure("animation_get_state_machine", {{"entity", 2}}), "has no Animator"));
        CHECK(contains(fixture.failure("animation_get_state_machine", {{"animatorRef", "anims/Missing.vfAnimator"}}),
                       "does not exist"));
        CHECK(contains(fixture.argError("animation_get_state_machine", {{"animatorRef", "../x.vfAnimator"}}),
                       "outside the allowed root"));
        CHECK(contains(fixture.argError("animation_get_state_machine", {{"entity", 1}, {"layer", 1}}),
                       "single state machine"));
    }

    TEST_CASE("animation_list_clips ranks native coverage, needs the root bone and asks for summaries once")
    {
        ToolFixture fixture;
        const nlohmann::json result = fixture.callOk("animation_list_clips", {{"entity", 1}});
        CHECK(result["via"] == "native");
        CHECK(result["mesh"] == "characters/hero/Hero.vfMesh");
        CHECK(result["skeletonBones"] == 4);
        CHECK(result["rootBone"] == "Hips");
        REQUIRE(result["clips"].size() == 2); // Idle 1.0, Run 0.75; Wave misses the root, Fall covers 0.25
        CHECK(result["clips"][0]["path"] == "characters/hero/Hero_Idle.vfAnim");
        CHECK(result["clips"][0]["coverage"] == 1.0);
        CHECK(result["clips"][0]["channels"] == 4);
        CHECK(result["clips"][1]["path"] == "characters/hero/Hero_Run.vfAnim");
        CHECK(result["clips"][1]["matchedBones"] == 3);
        CHECK(result["clips"][1]["rootMatched"] == true);
        CHECK(result["incompatible"] == 2);
        CHECK(fixture.engine.summaryQueries == 1);

        const nlohmann::json all = fixture.callOk("animation_list_clips",
                                                  {{"entity", 1}, {"includeIncompatible", true}, {"minCoverage", 0.2}});
        REQUIRE(all["clips"].size() == 4);
        CHECK(all["clips"][2]["path"] == "characters/hero/Hero_Wave.vfAnim");
        CHECK(all["clips"][2]["compatible"] == false); // 0.75, but the root bone is not driven
        CHECK(all["clips"][3]["path"] == "other/Fall.vfAnim");
        CHECK(all["clips"][3]["compatible"] == true);

        const nlohmann::json filtered = fixture.callOk("animation_list_clips", {{"mesh", "characters/hero/Hero.vfMesh"},
                                                                                {"directory", "other"}, {"minCoverage", 0}});
        REQUIRE(filtered["clips"].size() == 1);
        CHECK(filtered["clips"][0]["path"] == "other/Fall.vfAnim");
        CHECK(fixture.callOk("animation_list_clips", {{"entity", 1}, {"contains", "RUN"}})["clips"].size() == 1);
        CHECK(fixture.callOk("animation_list_clips", {{"entity", 1}, {"max", 1}})["truncated"] == true);

        CHECK(contains(fixture.failure("animation_list_clips", {{"entity", 2}}), "static mesh"));
        CHECK(contains(fixture.argError("animation_list_clips", {{"entity", 1}, {"retarget", "x.vfretarget"}}),
                       "goes with 'mesh'"));
        CHECK(contains(fixture.argError("animation_list_clips", {}), "exactly one of"));
    }

    TEST_CASE("animation_list_clips matches through the entity's retarget map")
    {
        ToolFixture fixture;
        fixture.engine.addClip("mixamo/Samba.vfAnim", {"mixamorig:Hips", "mixamorig:Spine", "mixamorig:Head"});
        const asset::AssetRef retarget = fixture.engine.addFile("rigs/MixamoToHero.vfretarget");
        services::RetargetBindingInfo binding;
        binding.success = true;
        binding.sourceRig.bindings = {{retargeting::HumanoidBoneRole::Hips, "mixamorig:Hips"},
                                      {retargeting::HumanoidBoneRole::Spine, "mixamorig:Spine"},
                                      {retargeting::HumanoidBoneRole::Head, "mixamorig:Head"}};
        binding.targetRig.bindings = {{retargeting::HumanoidBoneRole::Hips, "Hips"},
                                      {retargeting::HumanoidBoneRole::Spine, "Spine"},
                                      {retargeting::HumanoidBoneRole::Head, "Head"}};
        fixture.engine.retargets[mcpanim::pathKey(retarget.resolve())] = binding;
        fixture.engine.meshOf(1).retargetRef = retarget;

        const nlohmann::json result = fixture.callOk("animation_list_clips", {{"entity", 1}});
        CHECK(result["via"] == "retarget");
        CHECK(result["retarget"] == "rigs/MixamoToHero.vfretarget");
        REQUIRE(result["clips"].size() == 1); // the native clips' bone names are not source-rig bones
        CHECK(result["clips"][0]["path"] == "mixamo/Samba.vfAnim");
        CHECK(result["clips"][0]["matchedBones"] == 3);
        CHECK(result["clips"][0]["via"] == "retarget");
    }

    TEST_CASE("animation_preview forwards the request, reports and stops previews")
    {
        ToolFixture fixture;
        nlohmann::json args = locomotionEdit("characters/hero/Hero_Idle.vfAnim", "characters/hero/Hero_Run.vfAnim");
        args["entity"] = 1;
        fixture.callOk("animation_set_state_machine", args);

        const nlohmann::json clip = fixture.callOk("animation_preview", {
            {"entity", 1}, {"clip", "characters/hero/Hero_Run.vfAnim"}, {"normalizedTime", 0.25}, {"playing", false},
            {"speed", 2}
        });
        CHECK(clip["started"] == 1);
        CHECK(clip["clip"] == "characters/hero/Hero_Run.vfAnim");
        REQUIRE(fixture.engine.startedPreviews.size() == 1);
        const services::AnimationEditPreviewRequest& request = fixture.engine.startedPreviews[0].second;
        CHECK(fixture.engine.startedPreviews[0].first == 1u);
        CHECK(request.source == services::AnimationPreviewSource::Clip);
        CHECK(mcpanim::pathKey(request.clipPath) ==
              mcpanim::pathKey(fixture.engine.file("characters/hero/Hero_Run.vfAnim").string()));
        CHECK(request.normalizedTime == 0.25f);
        CHECK_FALSE(request.playing);
        CHECK(request.speed == 2.0f);
        CHECK(request.loop);

        const nlohmann::json state = fixture.callOk("animation_preview", {{"entity", 1}, {"state", "Run"},
                                                                          {"parameters", {{"speed", 3}}}});
        CHECK(state["state"] == "Run");
        const services::AnimationEditPreviewRequest& stateRequest = fixture.engine.startedPreviews.back().second;
        CHECK(stateRequest.source == services::AnimationPreviewSource::State);
        CHECK(stateRequest.state == "Run");
        REQUIRE(stateRequest.parameters.size() == 1);
        CHECK(stateRequest.parameters[0].first == "speed");
        CHECK(std::get<float>(stateRequest.parameters[0].second) == 3.0f); // typed by the animator: Float

        const nlohmann::json status = fixture.callOk("animation_preview", {{"entity", 1}});
        CHECK(status["previews"][0]["preview"]["source"] == "state");
        CHECK(status["previews"][0]["preview"]["state"] == "Run");

        CHECK(contains(fixture.argError("animation_preview", {{"entity", 1}, {"state", "Fly"}}), "no state \"Fly\""));
        CHECK(contains(fixture.argError("animation_preview", {{"entity", 1}, {"state", "Run"}, {"parameters", {{"fly", 1}}}}),
                       "no parameter \"fly\""));
        CHECK(contains(fixture.argError("animation_preview", {{"entity", 1}, {"state", "Run"}, {"clip", "a.vfAnim"}}),
                       "not both"));
        CHECK(contains(fixture.argError("animation_preview", {{"entity", 1}, {"state", "Run"}, {"loop", false}}),
                       "applies to clip previews"));
        CHECK(contains(fixture.failure("animation_preview", {{"entity", 4}, {"state", "Idle"}}), "has no Animator"));
        CHECK(contains(fixture.argError("animation_preview", {{"stopAll", true}, {"entity", 1}}), "no other arguments"));

        const nlohmann::json stopped = fixture.callOk("animation_preview", {{"entities", {1, 5}}, {"stop", true}});
        CHECK(stopped["stopped"] == nlohmann::json::array({{{"entity", 1}, {"stopped", true}}, {{"entity", 5}, {"stopped", false}}}));
        fixture.callOk("animation_preview", {{"entity", 5}, {"clip", "characters/hero/Hero_Idle.vfAnim"}});
        const nlohmann::json all = fixture.callOk("animation_preview", {{"stopAll", true}});
        CHECK(all["stopped"] == true);
        CHECK(fixture.engine.previews.empty());
        CHECK(fixture.engine.log.back() == "StopPreview:all");

        fixture.engine.previewFails = true;
        CHECK(contains(fixture.failure("animation_preview", {{"entity", 1}, {"state", "Idle"}}), "not initialized"));
        CHECK(fixture.engine.undoStack.size() == 1); // only the state-machine edit; previews are not undoable
    }

    TEST_CASE("animation_get_runtime_state maps the live state ids to the animator's names")
    {
        ToolFixture fixture;
        nlohmann::json args = locomotionEdit("characters/hero/Hero_Idle.vfAnim", "characters/hero/Hero_Run.vfAnim");
        args["entity"] = 1;
        fixture.callOk("animation_set_state_machine", args);
        const animator::AnimatorGraph graph = anim::baseGraph(fixture.engine.animatorAt("characters/hero/Hero.vfAnimator")->data);
        const uint32_t idle = graph.findStateByName("Idle")->id;
        const uint32_t run = graph.findStateByName("Run")->id;
        const uint32_t idleToRun = transitionBetween(graph, "Idle", "Run")->id;

        mcpanim::FakeRuntime live;
        live.debug.isValid = true;
        live.debug.currentStateId = run;
        live.debug.previousStateId = idle;
        live.debug.stateTime = 0.5f;
        live.debug.isBlending = true;
        live.debug.blendProgress = 0.25f;
        live.debug.activeTransitionId = idleToRun;
        live.debug.parameters.push_back({"speed", AnimatorParameterType::Float, 3.0f});
        services::AnimatorConditionEval eval;
        eval.transitionId = idleToRun;
        eval.parameterName = "speed";
        eval.op = ComparisonOperator::Greater;
        eval.threshold = 0.1f;
        eval.currentValue = 3.0f;
        eval.result = true;
        live.debug.conditionResults.push_back(eval);
        live.currentState = "Run";
        live.normalizedTime = 0.5f;
        fixture.engine.runtime[1] = live;
        fixture.engine.playMode = true; // works in Play mode

        const nlohmann::json result = fixture.callOk("animation_get_runtime_state", {{"entity", 1}});
        CHECK(result["hasAnimator"] == true);
        CHECK(result["playMode"] == true);
        CHECK(result["playing"] == true);
        CHECK(result["animator"] == "characters/hero/Hero.vfAnimator");
        CHECK(result["currentState"] == "Run");
        CHECK(result["previousState"] == "Idle");
        CHECK(result["normalizedTime"] == 0.5);
        CHECK(result["blending"] == true);
        CHECK(result["blendProgress"] == 0.25);
        CHECK(result["activeTransition"] == nlohmann::json{{"id", idleToRun}, {"from", "Idle"}, {"to", "Run"}});
        CHECK(result["parameters"] == nlohmann::json::array({{{"name", "speed"}, {"type", "Float"}, {"value", 3.0}}}));
        REQUIRE(result["conditions"].size() == 1);
        CHECK(result["conditions"][0]["from"] == "Idle");
        CHECK(result["conditions"][0]["to"] == "Run");
        CHECK(result["conditions"][0]["op"] == ">");
        CHECK(result["conditions"][0]["threshold"] == 0.1);
        CHECK(result["conditions"][0]["result"] == true);
        CHECK(result["preview"].is_null());

        // A layer the animator does not have is an argument error.
        CHECK(contains(fixture.argError("animation_get_runtime_state", {{"entity", 1}, {"layer", 2}}), "single state machine"));
        CHECK(fixture.engine.lastDebugLayer == 2u);

        const nlohmann::json none = fixture.callOk("animation_get_runtime_state", {{"entity", 4}});
        CHECK(none["hasAnimator"] == false);
        CHECK(contains(none["note"].get<std::string>(), "has no Animator"));
        const nlohmann::json pending = fixture.callOk("animation_get_runtime_state", {{"entity", 5}});
        CHECK(pending["hasAnimator"] == false);
        CHECK(contains(pending["note"].get<std::string>(), "not running yet"));
    }
}
