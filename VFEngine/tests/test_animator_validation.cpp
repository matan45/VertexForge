#include <doctest.h>

#include "animator/AnimatorValidation.hpp"
#include "asset/AssetGUID.hpp"
#include "asset/AssetRef.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// VK-1656: animator::validateAnimator, the structural checks shared by the Animator Editor, the MCP
// animation tools (errors block a save) and these tests. One case per rule family; each starts from a
// valid two-state locomotion graph and breaks one thing.
namespace
{
    using animator::AnimatorParameterType;
    using animator::ComparisonOperator;
    using animator::GraphIssue;
    using Level = animator::GraphIssue::Level;

    asset::AssetRef clip(uint64_t id)
    {
        return asset::AssetRef(asset::AssetGUID::fromValue(id));
    }

    animator::AnimatorState state(uint32_t id, std::string name, uint64_t clipId)
    {
        animator::AnimatorState out;
        out.id = id;
        out.name = std::move(name);
        out.animationRef = clipId != 0 ? clip(clipId) : asset::AssetRef::invalid();
        return out;
    }

    animator::TransitionCondition condition(std::string parameter, ComparisonOperator op,
                                            animator::AnimatorParameterValue value)
    {
        return {std::move(parameter), op, value};
    }

    animator::AnimatorTransition transition(uint32_t id, uint32_t from, uint32_t to,
                                            std::vector<animator::TransitionCondition> conditions)
    {
        animator::AnimatorTransition out;
        out.id = id;
        out.sourceStateId = from;
        out.targetStateId = to;
        out.conditions = std::move(conditions);
        return out;
    }

    // Idle (1, default) <-> Run (2) on Float "speed". Valid: no issues at all.
    animator::AnimatorGraph locomotionGraph()
    {
        animator::AnimatorGraph graph;
        graph.parameters.push_back({"speed", AnimatorParameterType::Float, 0.0f});
        graph.states.push_back(state(1, "Idle", 101));
        graph.states.push_back(state(2, "Run", 102));
        graph.transitions.push_back(transition(1, 1, 2, {condition("speed", ComparisonOperator::Greater, 0.1f)}));
        graph.transitions.push_back(transition(2, 2, 1, {condition("speed", ComparisonOperator::LessEqual, 0.1f)}));
        graph.defaultStateId = 1;
        graph.nextStateId = 3;
        graph.nextTransitionId = 3;
        return graph;
    }

    animator::AnimatorData single(animator::AnimatorGraph graph)
    {
        animator::AnimatorData data;
        data.name = "Test";
        data.graph = std::move(graph);
        return data;
    }

    animator::AnimationLayerData layer(std::string name, animator::AnimatorGraph graph)
    {
        animator::AnimationLayerData out;
        out.name = std::move(name);
        out.graph = std::move(graph);
        return out;
    }

    std::optional<GraphIssue> findIssue(const std::vector<GraphIssue>& issues, const std::string& needle)
    {
        for (const GraphIssue& issue : issues)
        {
            if (issue.message.find(needle) != std::string::npos)
            {
                return issue;
            }
        }
        return std::nullopt;
    }

    // The one issue whose message contains `needle`, checked for its level (and layer when given).
    void expectIssue(const std::vector<GraphIssue>& issues, const std::string& needle, Level level,
                     std::optional<int32_t> layerIndex = std::nullopt)
    {
        CAPTURE(needle);
        const std::optional<GraphIssue> issue = findIssue(issues, needle);
        REQUIRE(issue.has_value());
        CHECK(issue->level == level);
        if (layerIndex.has_value())
        {
            CHECK(issue->layer == *layerIndex);
        }
    }

    std::vector<GraphIssue> validate(const animator::AnimatorGraph& graph)
    {
        return animator::validateAnimator(single(graph));
    }
}

TEST_SUITE("AnimatorValidation")
{
    TEST_CASE("a well-formed graph has no issues")
    {
        const std::vector<GraphIssue> issues = validate(locomotionGraph());
        std::string messages;
        for (const GraphIssue& issue : issues)
        {
            messages += issue.message + "\n";
        }
        INFO(messages);
        CHECK(issues.empty());
        CHECK_FALSE(animator::hasErrors(issues));
    }

    TEST_CASE("an empty graph is only a warning")
    {
        const std::vector<GraphIssue> issues = validate(animator::AnimatorGraph{});
        REQUIRE(issues.size() == 1);
        expectIssue(issues, "has no states", Level::Warning, 0);
        CHECK_FALSE(animator::hasErrors(issues));
    }

    TEST_CASE("parameters: unnamed and duplicate names are errors")
    {
        animator::AnimatorGraph graph = locomotionGraph();
        graph.parameters.push_back({"", AnimatorParameterType::Bool, false});
        graph.parameters.push_back({"speed", AnimatorParameterType::Float, 1.0f});
        const std::vector<GraphIssue> issues = validate(graph);
        expectIssue(issues, "A parameter has no name", Level::Error);
        expectIssue(issues, "Duplicate parameter \"speed\"", Level::Error);
        CHECK(animator::hasErrors(issues));
    }

    TEST_CASE("states: ids, names, the reserved name, speed and the default state")
    {
        SUBCASE("id 0")
        {
            animator::AnimatorGraph graph = locomotionGraph();
            graph.states.push_back(state(0, "Zero", 103));
            expectIssue(validate(graph), "has id 0", Level::Error);
        }
        SUBCASE("duplicate id")
        {
            animator::AnimatorGraph graph = locomotionGraph();
            graph.states.push_back(state(2, "Walk", 103));
            const std::vector<GraphIssue> issues = validate(graph);
            expectIssue(issues, "Duplicate state id 2", Level::Error);
            CHECK(findIssue(issues, "Duplicate state id 2")->stateId == 2);
        }
        SUBCASE("unnamed")
        {
            animator::AnimatorGraph graph = locomotionGraph();
            graph.states.push_back(state(3, "", 103));
            graph.transitions.push_back(transition(3, 1, 3, {condition("speed", ComparisonOperator::Greater, 5.0f)}));
            graph.transitions.push_back(transition(4, 3, 1, {condition("speed", ComparisonOperator::Less, 5.0f)}));
            expectIssue(validate(graph), "State 3 has no name", Level::Warning);
        }
        SUBCASE("duplicate name")
        {
            animator::AnimatorGraph graph = locomotionGraph();
            graph.states.push_back(state(3, "Run", 103));
            expectIssue(validate(graph), "Duplicate state name \"Run\"", Level::Error);
        }
        SUBCASE("the reserved name Any")
        {
            animator::AnimatorGraph graph = locomotionGraph();
            graph.states[1].name = "Any";
            expectIssue(validate(graph), "reserved by the MCP tools", Level::Warning);
        }
        SUBCASE("playbackSpeed <= 0")
        {
            animator::AnimatorGraph graph = locomotionGraph();
            graph.states[1].playbackSpeed = 0.0f;
            expectIssue(validate(graph), "playbackSpeed <= 0", Level::Warning);
        }
        SUBCASE("no default state")
        {
            animator::AnimatorGraph graph = locomotionGraph();
            graph.defaultStateId = 9;
            expectIssue(validate(graph), "No default state set", Level::Error);
        }
    }

    TEST_CASE("transitions: ids, endpoints, blend and exit times")
    {
        SUBCASE("missing or duplicate id")
        {
            animator::AnimatorGraph graph = locomotionGraph();
            graph.transitions[1].id = 1;
            const std::vector<GraphIssue> issues = validate(graph);
            expectIssue(issues, "missing or duplicate id 1", Level::Error);
            CHECK(findIssue(issues, "missing or duplicate id")->transitionId == 1);
            graph.transitions[1].id = 0;
            expectIssue(validate(graph), "missing or duplicate id 0", Level::Error);
        }
        SUBCASE("a source that does not exist")
        {
            animator::AnimatorGraph graph = locomotionGraph();
            graph.transitions[0].sourceStateId = 7;
            expectIssue(validate(graph), "starts at a state that does not exist", Level::Error);
        }
        SUBCASE("targeting Any State")
        {
            animator::AnimatorGraph graph = locomotionGraph();
            graph.transitions[0].targetStateId = 0;
            expectIssue(validate(graph), "targets Any State", Level::Error);
        }
        SUBCASE("a target that does not exist")
        {
            animator::AnimatorGraph graph = locomotionGraph();
            graph.transitions[0].targetStateId = 7;
            expectIssue(validate(graph), "targets a state that does not exist", Level::Error);
        }
        SUBCASE("negative blend duration")
        {
            animator::AnimatorGraph graph = locomotionGraph();
            graph.transitions[0].blendDuration = -0.1f;
            expectIssue(validate(graph), "negative blendDuration", Level::Error);
        }
        SUBCASE("negative exit time")
        {
            animator::AnimatorGraph graph = locomotionGraph();
            graph.transitions[0].hasExitTime = true;
            graph.transitions[0].exitTime = -0.5f;
            expectIssue(validate(graph), "negative exitTime", Level::Warning);
        }
        SUBCASE("exit time >= 1 on a non-looping state never fires")
        {
            animator::AnimatorGraph graph = locomotionGraph();
            graph.states[0].loop = false;
            graph.transitions[0].hasExitTime = true;
            graph.transitions[0].exitTime = 1.0f;
            expectIssue(validate(graph), "never fires", Level::Warning);

            // A looping source wraps its normalized time, so 1.0 is fine there.
            graph.states[0].loop = true;
            CHECK_FALSE(findIssue(validate(graph), "never fires").has_value());
        }
        SUBCASE("no conditions and no exit time")
        {
            animator::AnimatorGraph graph = locomotionGraph();
            graph.transitions[0].conditions.clear();
            expectIssue(validate(graph), "fires immediately", Level::Warning);

            graph.transitions.push_back(transition(3, 0, 2, {}));
            expectIssue(validate(graph), "fires after every blend", Level::Error);

            graph.transitions.back().hasExitTime = true;
            CHECK_FALSE(findIssue(validate(graph), "fires after every blend").has_value());
        }
        SUBCASE("an undefined parameter")
        {
            animator::AnimatorGraph graph = locomotionGraph();
            graph.transitions[0].conditions.push_back(condition("jump", ComparisonOperator::Equal, true));
            expectIssue(validate(graph), "references undefined parameter \"jump\"", Level::Error);
        }
        SUBCASE("Any State with held conditions re-fires; a trigger does not")
        {
            animator::AnimatorGraph graph = locomotionGraph();
            graph.transitions.push_back(transition(3, 0, 2, {condition("speed", ComparisonOperator::Greater, 5.0f)}));
            expectIssue(validate(graph), "re-fires after every blend", Level::Warning);

            graph.parameters.push_back({"hit", AnimatorParameterType::Trigger, false});
            graph.transitions.back().conditions = {condition("hit", ComparisonOperator::Equal, true)};
            CHECK_FALSE(findIssue(validate(graph), "re-fires").has_value());
        }
        SUBCASE("a transition back into its own state is never taken")
        {
            animator::AnimatorGraph graph = locomotionGraph();
            graph.transitions.push_back(transition(3, 2, 2, {condition("speed", ComparisonOperator::Greater, 5.0f)}));
            const std::vector<GraphIssue> issues = validate(graph);
            expectIssue(issues, "leads back to its own state", Level::Warning);
            CHECK(findIssue(issues, "leads back to its own state")->transitionId == 3);

            // From Any State the playing state may restart.
            graph.parameters.push_back({"hit", AnimatorParameterType::Trigger, false});
            graph.transitions.back() = transition(3, 0, 2, {condition("hit", ComparisonOperator::Equal, true)});
            CHECK_FALSE(findIssue(validate(graph), "leads back").has_value());
        }
    }

    TEST_CASE("conditions are checked against their parameter's type")
    {
        animator::AnimatorGraph base = locomotionGraph();
        base.parameters.push_back({"lane", AnimatorParameterType::Int, 0});
        base.parameters.push_back({"grounded", AnimatorParameterType::Bool, true});
        base.parameters.push_back({"jump", AnimatorParameterType::Trigger, false});
        const auto withCondition = [&base](animator::TransitionCondition added)
        {
            animator::AnimatorGraph graph = base;
            graph.transitions[0].conditions.push_back(std::move(added));
            return validate(graph);
        };

        CHECK(withCondition(condition("lane", ComparisonOperator::Greater, 1)).empty());
        CHECK(withCondition(condition("grounded", ComparisonOperator::NotEqual, false)).empty());
        CHECK(withCondition(condition("jump", ComparisonOperator::Equal, true)).empty());

        expectIssue(withCondition(condition("speed", ComparisonOperator::Equal, true)), "is compared with true/false",
                    Level::Error);
        expectIssue(withCondition(condition("lane", ComparisonOperator::Equal, false)), "is compared with true/false",
                    Level::Error);
        expectIssue(withCondition(condition("grounded", ComparisonOperator::Equal, 1.0f)),
                    "compared with a number and never passes", Level::Error);
        expectIssue(withCondition(condition("grounded", ComparisonOperator::Greater, true)), "acts as ==",
                    Level::Warning);
        expectIssue(withCondition(condition("jump", ComparisonOperator::Equal, false)), "should be tested with == true",
                    Level::Warning);
        // != false fires when the trigger is set, like == true.
        CHECK_FALSE(findIssue(withCondition(condition("jump", ComparisonOperator::NotEqual, false)),
                              "should be tested").has_value());

        // An Int parameter compares with the float value cast to int32_t: a fraction is dropped.
        expectIssue(withCondition(condition("lane", ComparisonOperator::Greater, 1.5f)),
                    "is compared with 1.5, which the runtime truncates to 1", Level::Warning);
        CHECK(withCondition(condition("lane", ComparisonOperator::Greater, 2.0f)).empty());
    }

    TEST_CASE("dead ends and unreachable states are warnings")
    {
        animator::AnimatorGraph graph = locomotionGraph();
        graph.states.push_back(state(3, "Fall", 103));
        graph.transitions.push_back(transition(3, 2, 3, {condition("speed", ComparisonOperator::Greater, 9.0f)}));
        expectIssue(validate(graph), "Dead-end state: \"Fall\"", Level::Warning);

        graph.states.push_back(state(4, "Orphan", 104));
        graph.transitions.push_back(transition(4, 4, 1, {condition("speed", ComparisonOperator::Less, 0.0f)}));
        const std::vector<GraphIssue> issues = validate(graph);
        expectIssue(issues, "Unreachable state: \"Orphan\"", Level::Warning);
        CHECK(findIssue(issues, "Unreachable state: \"Orphan\"")->stateId == 4);
        CHECK_FALSE(findIssue(issues, "Unreachable state: \"Fall\"").has_value());

        // An Any State transition makes its target reachable.
        graph.parameters.push_back({"spawn", AnimatorParameterType::Trigger, false});
        graph.transitions.push_back(transition(5, 0, 4, {condition("spawn", ComparisonOperator::Equal, true)}));
        CHECK_FALSE(findIssue(validate(graph), "Unreachable state: \"Orphan\"").has_value());

        // A transition back into the same state is no way out.
        graph.transitions.push_back(transition(6, 3, 3, {condition("speed", ComparisonOperator::Less, 1.0f)}));
        expectIssue(validate(graph), "Dead-end state: \"Fall\"", Level::Warning);
    }

    TEST_CASE("clips and blend trees")
    {
        SUBCASE("a state without a clip")
        {
            animator::AnimatorGraph graph = locomotionGraph();
            graph.states[1].animationRef = asset::AssetRef::invalid();
            expectIssue(validate(graph), "has no animation clip assigned", Level::Warning);
        }

        animator::AnimatorGraph graph = locomotionGraph();
        animator::BlendTreeData tree;
        tree.parameterName = "speed";
        tree.entries = {{clip(201), 0.0f, glm::vec2(0.0f)}, {clip(202), 3.0f, glm::vec2(0.0f)}};
        graph.states[1].blendTree = tree;
        graph.states[1].animationRef = asset::AssetRef::invalid(); // a blend tree needs no clip of its own
        CHECK(validate(graph).empty());

        SUBCASE("axis parameter missing, undefined or not Float")
        {
            graph.states[1].blendTree->parameterName.clear();
            expectIssue(validate(graph), "has no parameter", Level::Error);
            graph.states[1].blendTree->parameterName = "blend";
            expectIssue(validate(graph), "uses undefined parameter \"blend\"", Level::Error);
            graph.parameters.push_back({"blend", AnimatorParameterType::Int, 0});
            expectIssue(validate(graph), "must be Float", Level::Error);
        }
        SUBCASE("2D needs a Y parameter")
        {
            graph.states[1].blendTree->type = animator::BlendTreeType::BlendTree2D;
            expectIssue(validate(graph), "has no Y parameter", Level::Error);
            graph.states[1].blendTree->parameterNameY = "speed";
            CHECK_FALSE(animator::hasErrors(validate(graph)));
        }
        SUBCASE("entries: none, without a clip, sharing a 1D threshold")
        {
            graph.states[1].blendTree->entries.clear();
            expectIssue(validate(graph), "has no entries", Level::Warning);

            graph.states[1].blendTree->entries = {{clip(201), 1.0f, glm::vec2(0.0f)},
                                                  {asset::AssetRef::invalid(), 2.0f, glm::vec2(0.0f)},
                                                  {clip(203), 1.0f, glm::vec2(0.0f)}};
            const std::vector<GraphIssue> issues = validate(graph);
            expectIssue(issues, "entry 1 has no clip", Level::Warning);
            expectIssue(issues, "entries 0 and 2 share threshold", Level::Warning);

            // 2D entries are not compared by threshold.
            graph.states[1].blendTree->type = animator::BlendTreeType::BlendTree2D;
            graph.states[1].blendTree->parameterNameY = "speed";
            CHECK_FALSE(findIssue(validate(graph), "share threshold").has_value());
        }
    }

    TEST_CASE("layered files: every layer is checked, the top-level graph is ignored")
    {
        animator::AnimatorGraph upper;
        upper.parameters.push_back({"wave", AnimatorParameterType::Bool, false});
        upper.states.push_back(state(1, "Rest", 301));
        upper.states.push_back(state(2, "Wave", 302));
        // Layer 1 tests layer 0's "speed": the runtime merges every layer's parameters.
        upper.transitions.push_back(transition(1, 1, 2, {condition("wave", ComparisonOperator::Equal, true),
                                                         condition("speed", ComparisonOperator::Less, 1.0f)}));
        upper.transitions.push_back(transition(2, 2, 1, {condition("wave", ComparisonOperator::Equal, false)}));
        upper.defaultStateId = 1;

        animator::AnimatorData data;
        data.layers = {layer("Base", locomotionGraph()), layer("Upper", upper)};
        data.graph = animator::AnimatorGraph{}; // never read for a layered file
        data.graph.defaultStateId = 42;
        CHECK(animator::validateAnimator(data).empty());

        SUBCASE("issues carry their layer index")
        {
            data.layers[1].graph.defaultStateId = 0;
            expectIssue(animator::validateAnimator(data), "No default state set", Level::Error, 1);
        }
        SUBCASE("the same parameter with another type in a later layer")
        {
            data.layers[1].graph.parameters.push_back({"speed", AnimatorParameterType::Int, 0});
            expectIssue(animator::validateAnimator(data), "the earlier declaration wins", Level::Warning, 1);
        }
        SUBCASE("a trigger declared in another layer is never reset by this one")
        {
            data.layers[0].graph.parameters.push_back({"hit", AnimatorParameterType::Trigger, false});
            data.layers[1].graph.transitions[0].conditions.push_back(condition("hit", ComparisonOperator::Equal, true));
            expectIssue(animator::validateAnimator(data), "declared in another layer", Level::Warning, 1);
        }
        SUBCASE("bone masks and DirectClip layers")
        {
            data.layers[1].boneMaskName = "UpperBody";
            expectIssue(animator::validateAnimator(data), "unknown bone mask \"UpperBody\"", Level::Warning, 1);
            animator::BoneMaskDefinition mask;
            mask.name = "UpperBody";
            data.boneMasks.push_back(mask);
            CHECK_FALSE(findIssue(animator::validateAnimator(data), "unknown bone mask").has_value());

            data.layers[1].sourceMode = animator::LayerSourceMode::DirectClip;
            expectIssue(animator::validateAnimator(data), "DirectClip layer \"Upper\" has no clip", Level::Warning, 1);
            data.layers[1].directClipRef = clip(401);
            CHECK(animator::validateAnimator(data).empty()); // its graph is not checked

            data.layers[0].sourceMode = animator::LayerSourceMode::DirectClip;
            const std::vector<GraphIssue> issues = animator::validateAnimator(data);
            expectIssue(issues, "base layer cannot be a DirectClip layer", Level::Error, 0);
            CHECK(animator::hasErrors(issues));
        }
    }
}
