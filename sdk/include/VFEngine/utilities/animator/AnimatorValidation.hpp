#pragma once

#include "AnimatorTypes.hpp"
#include <cmath>
#include <cstdint>
#include <format>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

// Structural checks over an animator::AnimatorData, shared by the Animator Editor (Validate),
// the MCP animation tools (errors block a save) and Tests (VK-1656). Errors describe data the
// runtime cannot play as authored; warnings describe data that plays, probably not as intended.
// The rules mirror the runtime: graph.parameters of every state-machine layer are merged into
// one shared map (AnimationLayerStack::initializeSharedParameters), the 1D blend tree sorts its
// entries itself (BlendTreeEvaluator::compute1DWeights) and reads float parameters, exit time
// compares fmod(time / duration, 1) (AnimatorStateMachine::checkTransitionConditions).
// Header-only.
namespace animator
{
    struct GraphIssue
    {
        enum class Level : uint8_t
        {
            Warning,
            Error
        };

        Level level = Level::Warning;
        std::string message;
        int32_t layer = -1;        // layer index (0 = base layer or the single graph); -1 = whole animator
        uint32_t stateId = 0;      // 0 when not state-specific
        uint32_t transitionId = 0; // 0 when not transition-specific
    };

    namespace validation
    {
        using ParameterTypes = std::unordered_map<std::string, AnimatorParameterType>;

        inline void addIssue(std::vector<GraphIssue>& issues, GraphIssue::Level level, std::string message,
                             int32_t layer, uint32_t stateId = 0, uint32_t transitionId = 0)
        {
            issues.push_back(GraphIssue{level, std::move(message), layer, stateId, transitionId});
        }

        inline std::string quoted(const std::string& name)
        {
            return "\"" + name + "\"";
        }

        inline std::string stateLabel(const AnimatorGraph& graph, uint32_t stateId)
        {
            if (stateId == 0)
                return "Any State";
            const AnimatorState* state = graph.findStateById(stateId);
            return state != nullptr ? quoted(state->name) : "state " + std::to_string(stateId);
        }

        inline std::string transitionLabel(const AnimatorGraph& graph, const AnimatorTransition& transition)
        {
            return "Transition " + stateLabel(graph, transition.sourceStateId) + " -> " +
                   stateLabel(graph, transition.targetStateId);
        }

        // Registers one graph's parameters into the shared map. Duplicates inside the graph are
        // errors; a name declared again by another layer with a different type is a warning
        // (the first declaration wins at runtime).
        inline void collectParameters(const AnimatorGraph& graph, int32_t layer, ParameterTypes& types,
                                      std::vector<GraphIssue>& issues)
        {
            std::unordered_set<std::string> local;
            for (const AnimatorParameter& parameter : graph.parameters)
            {
                if (parameter.name.empty())
                {
                    addIssue(issues, GraphIssue::Level::Error, "A parameter has no name", layer);
                    continue;
                }
                if (!local.insert(parameter.name).second)
                {
                    addIssue(issues, GraphIssue::Level::Error, "Duplicate parameter " + quoted(parameter.name), layer);
                    continue;
                }
                const auto [it, inserted] = types.try_emplace(parameter.name, parameter.type);
                if (!inserted && it->second != parameter.type)
                {
                    addIssue(issues, GraphIssue::Level::Warning,
                             "Parameter " + quoted(parameter.name) + " is " + parameterTypeToString(parameter.type) +
                                 " here but " + parameterTypeToString(it->second) +
                                 " in an earlier layer; the earlier declaration wins",
                             layer);
                }
            }
        }

        inline void checkStates(const AnimatorGraph& graph, int32_t layer, std::vector<GraphIssue>& issues)
        {
            if (graph.states.empty())
            {
                addIssue(issues, GraphIssue::Level::Warning, "The graph has no states, so it plays nothing", layer);
                return;
            }

            std::unordered_set<uint32_t> ids;
            std::unordered_set<std::string> names;
            for (const AnimatorState& state : graph.states)
            {
                if (state.id == 0)
                {
                    addIssue(issues, GraphIssue::Level::Error,
                             "State " + quoted(state.name) + " has id 0, which is reserved for Any State", layer);
                }
                else if (!ids.insert(state.id).second)
                {
                    addIssue(issues, GraphIssue::Level::Error, "Duplicate state id " + std::to_string(state.id),
                             layer, state.id);
                }

                if (state.name.empty())
                {
                    addIssue(issues, GraphIssue::Level::Warning,
                             "State " + std::to_string(state.id) + " has no name", layer, state.id);
                }
                else if (!names.insert(state.name).second)
                {
                    addIssue(issues, GraphIssue::Level::Error, "Duplicate state name " + quoted(state.name), layer,
                             state.id);
                }

                if (state.name == "Any")
                {
                    addIssue(issues, GraphIssue::Level::Warning,
                             "State name \"Any\" is reserved by the MCP tools for Any State transitions", layer,
                             state.id);
                }

                if (state.playbackSpeed <= 0.0f)
                {
                    addIssue(issues, GraphIssue::Level::Warning,
                             "State " + quoted(state.name) + " has playbackSpeed <= 0 and does not play forward",
                             layer, state.id);
                }
            }

            if (graph.findStateById(graph.defaultStateId) == nullptr)
            {
                addIssue(issues, GraphIssue::Level::Error, "No default state set", layer);
            }
        }

        inline void checkCondition(const AnimatorGraph& graph, const AnimatorTransition& transition,
                                   const TransitionCondition& condition, AnimatorParameterType type,
                                   const std::string& label, int32_t layer, std::vector<GraphIssue>& issues)
        {
            const bool boolValue = std::holds_alternative<bool>(condition.value);
            const std::string parameter = quoted(condition.parameterName);

            switch (type)
            {
            case AnimatorParameterType::Float:
            case AnimatorParameterType::Int:
                if (boolValue)
                {
                    addIssue(issues, GraphIssue::Level::Error,
                             label + ": " + parameterTypeToString(type) + " parameter " + parameter +
                                 " is compared with true/false; give a number",
                             layer, 0, transition.id);
                }
                else if (type == AnimatorParameterType::Int && std::holds_alternative<float>(condition.value))
                {
                    // evaluateCondition casts a float value to int32_t for an Int parameter.
                    const float value = std::get<float>(condition.value);
                    if (value != std::trunc(value))
                    {
                        addIssue(issues, GraphIssue::Level::Warning,
                                 label + ": Int parameter " + parameter + " is compared with " +
                                     std::format("{}", value) + ", which the runtime truncates to " +
                                     std::format("{}", std::trunc(value)),
                                 layer, 0, transition.id);
                    }
                }
                break;

            case AnimatorParameterType::Bool:
            case AnimatorParameterType::Trigger:
                if (!boolValue)
                {
                    // evaluateCondition only compares a bool parameter with a bool value.
                    addIssue(issues, GraphIssue::Level::Error,
                             label + ": " + parameterTypeToString(type) + " parameter " + parameter +
                                 " is compared with a number and never passes; give true/false",
                             layer, 0, transition.id);
                    break;
                }
                if (condition.op != ComparisonOperator::Equal && condition.op != ComparisonOperator::NotEqual)
                {
                    addIssue(issues, GraphIssue::Level::Warning,
                             label + ": operator " + comparisonOperatorToString(condition.op) + " on " +
                                 parameterTypeToString(type) + " parameter " + parameter + " acts as ==",
                             layer, 0, transition.id);
                }
                if (type == AnimatorParameterType::Trigger)
                {
                    const bool firesWhenSet =
                        (condition.op == ComparisonOperator::Equal && std::get<bool>(condition.value)) ||
                        (condition.op == ComparisonOperator::NotEqual && !std::get<bool>(condition.value));
                    if (!firesWhenSet)
                    {
                        addIssue(issues, GraphIssue::Level::Warning,
                                 label + ": Trigger " + parameter + " should be tested with == true", layer, 0,
                                 transition.id);
                    }
                    // A firing transition only resets triggers its own layer declares.
                    if (graph.findParameter(condition.parameterName) == nullptr)
                    {
                        addIssue(issues, GraphIssue::Level::Warning,
                                 label + ": Trigger " + parameter +
                                     " is declared in another layer, so this layer never resets it",
                                 layer, 0, transition.id);
                    }
                }
                break;
            }
        }

        inline void checkTransitions(const AnimatorGraph& graph, int32_t layer, const ParameterTypes& parameters,
                                     std::vector<GraphIssue>& issues)
        {
            std::unordered_set<uint32_t> ids;
            for (const AnimatorTransition& transition : graph.transitions)
            {
                const std::string label = transitionLabel(graph, transition);

                if (transition.id == 0 || !ids.insert(transition.id).second)
                {
                    addIssue(issues, GraphIssue::Level::Error,
                             label + " has a missing or duplicate id " + std::to_string(transition.id), layer, 0,
                             transition.id);
                }
                if (transition.sourceStateId != 0 && graph.findStateById(transition.sourceStateId) == nullptr)
                {
                    addIssue(issues, GraphIssue::Level::Error, label + " starts at a state that does not exist",
                             layer, 0, transition.id);
                }
                if (transition.targetStateId == 0)
                {
                    addIssue(issues, GraphIssue::Level::Error, label + " targets Any State, which is not a state",
                             layer, 0, transition.id);
                }
                else if (graph.findStateById(transition.targetStateId) == nullptr)
                {
                    addIssue(issues, GraphIssue::Level::Error, label + " targets a state that does not exist",
                             layer, 0, transition.id);
                }
                else if (transition.sourceStateId == transition.targetStateId)
                {
                    // checkTransitionConditions skips a transition into the playing state unless it starts at
                    // Any State.
                    addIssue(issues, GraphIssue::Level::Warning,
                             label + " leads back to its own state, which the runtime never takes; restart a state "
                                     "with an Any State transition on a Trigger",
                             layer, 0, transition.id);
                }
                if (transition.blendDuration < 0.0f)
                {
                    addIssue(issues, GraphIssue::Level::Error, label + " has a negative blendDuration", layer, 0,
                             transition.id);
                }

                if (transition.hasExitTime)
                {
                    if (transition.exitTime < 0.0f)
                    {
                        addIssue(issues, GraphIssue::Level::Warning, label + " has a negative exitTime", layer, 0,
                                 transition.id);
                    }
                    const AnimatorState* source = graph.findStateById(transition.sourceStateId);
                    if (source != nullptr && !source->loop && transition.exitTime >= 1.0f)
                    {
                        addIssue(issues, GraphIssue::Level::Warning,
                                 label + " never fires: exitTime >= 1 on a non-looping state (normalized time "
                                         "stays below 1); use e.g. 0.95",
                                 layer, 0, transition.id);
                    }
                }

                if (transition.conditions.empty() && !transition.hasExitTime)
                {
                    if (transition.sourceStateId == 0)
                    {
                        addIssue(issues, GraphIssue::Level::Error,
                                 label + " has no conditions and no exit time, so it fires after every blend",
                                 layer, 0, transition.id);
                    }
                    else
                    {
                        addIssue(issues, GraphIssue::Level::Warning,
                                 label + " has no conditions and no exit time, so it fires immediately", layer, 0,
                                 transition.id);
                    }
                }

                bool usesTrigger = false;
                for (const TransitionCondition& condition : transition.conditions)
                {
                    const auto it = parameters.find(condition.parameterName);
                    if (it == parameters.end())
                    {
                        addIssue(issues, GraphIssue::Level::Error,
                                 label + " references undefined parameter " + quoted(condition.parameterName),
                                 layer, 0, transition.id);
                        continue;
                    }
                    usesTrigger = usesTrigger || it->second == AnimatorParameterType::Trigger;
                    checkCondition(graph, transition, condition, it->second, label, layer, issues);
                }

                // Any State may transition into the state that is already playing, so a held
                // Bool/number condition restarts the target after every blend.
                if (transition.sourceStateId == 0 && !transition.conditions.empty() && !usesTrigger)
                {
                    addIssue(issues, GraphIssue::Level::Warning,
                             label + " re-fires after every blend while its conditions hold; use a Trigger "
                                     "parameter",
                             layer, 0, transition.id);
                }
            }
        }

        inline void checkDeadEndStates(const AnimatorGraph& graph, int32_t layer, std::vector<GraphIssue>& issues)
        {
            for (const AnimatorState& state : graph.states)
            {
                if (state.id == graph.defaultStateId)
                    continue;

                // A transition back into the same state never fires, so it is no way out.
                bool hasOutgoing = false;
                for (const AnimatorTransition& transition : graph.transitions)
                {
                    if (transition.sourceStateId == state.id && transition.targetStateId != state.id)
                    {
                        hasOutgoing = true;
                        break;
                    }
                }
                if (!hasOutgoing)
                {
                    addIssue(issues, GraphIssue::Level::Warning,
                             "Dead-end state: " + quoted(state.name) + " has no outgoing transitions", layer,
                             state.id);
                }
            }
        }

        inline void checkUnreachableStates(const AnimatorGraph& graph, int32_t layer, std::vector<GraphIssue>& issues)
        {
            if (graph.states.empty())
                return;

            // Breadth-first from the default state plus every Any State target.
            std::unordered_set<uint32_t> reachable;
            std::queue<uint32_t> pending;
            if (graph.defaultStateId != 0)
            {
                reachable.insert(graph.defaultStateId);
                pending.push(graph.defaultStateId);
            }
            for (const AnimatorTransition& transition : graph.transitions)
            {
                if (transition.sourceStateId == 0 && reachable.insert(transition.targetStateId).second)
                    pending.push(transition.targetStateId);
            }
            while (!pending.empty())
            {
                const uint32_t current = pending.front();
                pending.pop();
                for (const AnimatorTransition& transition : graph.transitions)
                {
                    if (transition.sourceStateId == current && reachable.insert(transition.targetStateId).second)
                        pending.push(transition.targetStateId);
                }
            }

            for (const AnimatorState& state : graph.states)
            {
                if (reachable.find(state.id) == reachable.end())
                {
                    addIssue(issues, GraphIssue::Level::Warning, "Unreachable state: " + quoted(state.name), layer,
                             state.id);
                }
            }
        }

        inline void checkBlendTree(const AnimatorState& state, int32_t layer, const ParameterTypes& parameters,
                                   std::vector<GraphIssue>& issues)
        {
            const BlendTreeData& tree = *state.blendTree;
            const std::string owner = "Blend tree of state " + quoted(state.name);

            auto checkAxis = [&](const std::string& name, const char* axis)
            {
                if (name.empty())
                {
                    addIssue(issues, GraphIssue::Level::Error, owner + " has no " + axis + "parameter", layer,
                             state.id);
                    return;
                }
                const auto it = parameters.find(name);
                if (it == parameters.end())
                {
                    addIssue(issues, GraphIssue::Level::Error,
                             owner + " uses undefined parameter " + quoted(name), layer, state.id);
                }
                else if (it->second != AnimatorParameterType::Float)
                {
                    addIssue(issues, GraphIssue::Level::Error,
                             owner + ": parameter " + quoted(name) + " must be Float (blend trees read floats)",
                             layer, state.id);
                }
            };

            const bool twoD = tree.type == BlendTreeType::BlendTree2D;
            checkAxis(tree.parameterName, twoD ? "X " : "");
            if (twoD)
                checkAxis(tree.parameterNameY, "Y ");

            if (tree.entries.empty())
            {
                addIssue(issues, GraphIssue::Level::Warning, owner + " has no entries", layer, state.id);
                return;
            }

            for (size_t i = 0; i < tree.entries.size(); ++i)
            {
                if (!tree.entries[i].animationRef.isValid())
                {
                    addIssue(issues, GraphIssue::Level::Warning,
                             owner + ": entry " + std::to_string(i) + " has no clip", layer, state.id);
                }
                if (!twoD)
                {
                    for (size_t j = 0; j < i; ++j)
                    {
                        if (tree.entries[j].threshold == tree.entries[i].threshold)
                        {
                            addIssue(issues, GraphIssue::Level::Warning,
                                     owner + ": entries " + std::to_string(j) + " and " + std::to_string(i) +
                                         " share threshold " + std::to_string(tree.entries[i].threshold),
                                     layer, state.id);
                            break;
                        }
                    }
                }
            }
        }

        inline void checkClips(const AnimatorGraph& graph, int32_t layer, const ParameterTypes& parameters,
                               std::vector<GraphIssue>& issues)
        {
            for (const AnimatorState& state : graph.states)
            {
                if (state.blendTree.has_value())
                {
                    checkBlendTree(state, layer, parameters, issues);
                }
                else if (!state.animationRef.isValid())
                {
                    addIssue(issues, GraphIssue::Level::Warning,
                             "State " + quoted(state.name) + " has no animation clip assigned", layer, state.id);
                }
            }
        }

        inline void checkGraph(const AnimatorGraph& graph, int32_t layer, const ParameterTypes& parameters,
                               std::vector<GraphIssue>& issues)
        {
            checkStates(graph, layer, issues);
            checkTransitions(graph, layer, parameters, issues);
            checkDeadEndStates(graph, layer, issues);
            checkUnreachableStates(graph, layer, issues);
            checkClips(graph, layer, parameters, issues);
        }
    }

    // Every issue of the animator. A layered file (layers non-empty) is checked layer by layer
    // and its top-level `graph` is ignored, exactly like the runtime and the serializer do.
    inline std::vector<GraphIssue> validateAnimator(const AnimatorData& data)
    {
        std::vector<GraphIssue> issues;
        validation::ParameterTypes parameters;

        if (data.layers.empty())
        {
            validation::collectParameters(data.graph, 0, parameters, issues);
            validation::checkGraph(data.graph, 0, parameters, issues);
            return issues;
        }

        for (size_t i = 0; i < data.layers.size(); ++i)
        {
            if (data.layers[i].sourceMode == LayerSourceMode::StateMachine)
                validation::collectParameters(data.layers[i].graph, static_cast<int32_t>(i), parameters, issues);
        }

        for (size_t i = 0; i < data.layers.size(); ++i)
        {
            const AnimationLayerData& layer = data.layers[i];
            const auto index = static_cast<int32_t>(i);

            if (!layer.boneMaskName.empty())
            {
                bool maskFound = false;
                for (const BoneMaskDefinition& mask : data.boneMasks)
                {
                    if (mask.name == layer.boneMaskName)
                    {
                        maskFound = true;
                        break;
                    }
                }
                if (!maskFound)
                {
                    validation::addIssue(issues, GraphIssue::Level::Warning,
                                         "Layer " + validation::quoted(layer.name) + " uses unknown bone mask " +
                                             validation::quoted(layer.boneMaskName),
                                         index);
                }
            }

            if (layer.sourceMode == LayerSourceMode::DirectClip)
            {
                if (i == 0)
                {
                    // The base layer drives the stack through its state machine; a DirectClip
                    // base layer never updates and the mesh stays in bind pose.
                    validation::addIssue(issues, GraphIssue::Level::Error,
                                         "The base layer cannot be a DirectClip layer (the mesh stays in bind pose)",
                                         index);
                }
                else if (!layer.directClipRef.isValid())
                {
                    validation::addIssue(issues, GraphIssue::Level::Warning,
                                         "DirectClip layer " + validation::quoted(layer.name) + " has no clip",
                                         index);
                }
                continue;
            }

            validation::checkGraph(layer.graph, index, parameters, issues);
        }

        return issues;
    }

    inline bool hasErrors(const std::vector<GraphIssue>& issues)
    {
        for (const GraphIssue& issue : issues)
        {
            if (issue.level == GraphIssue::Level::Error)
                return true;
        }
        return false;
    }
}
