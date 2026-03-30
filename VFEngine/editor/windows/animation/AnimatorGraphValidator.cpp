#include "AnimatorGraphValidator.hpp"
#include <unordered_set>
#include <queue>

namespace windows::animation
{
    std::vector<GraphWarning> AnimatorGraphValidator::validate(const animator::AnimatorData& data)
    {
        std::vector<GraphWarning> warnings;

        const auto& graph = data.graph;
        checkNoDefaultState(graph, warnings);
        checkDeadEndStates(graph, warnings);
        checkUnreachableStates(graph, warnings);
        checkMissingAnimations(graph, warnings);
        checkUndefinedParameters(graph, warnings);

        return warnings;
    }

    void AnimatorGraphValidator::checkNoDefaultState(const animator::AnimatorGraph& graph,
                                                      std::vector<GraphWarning>& warnings)
    {
        if (graph.states.empty())
            return;

        bool found = false;
        for (const auto& state : graph.states)
        {
            if (state.id == graph.defaultStateId)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            warnings.push_back({GraphWarning::Level::Error, "No default state set"});
        }
    }

    void AnimatorGraphValidator::checkDeadEndStates(const animator::AnimatorGraph& graph,
                                                     std::vector<GraphWarning>& warnings)
    {
        for (const auto& state : graph.states)
        {
            if (state.id == graph.defaultStateId)
                continue;

            bool hasOutgoing = false;
            for (const auto& transition : graph.transitions)
            {
                if (transition.sourceStateId == state.id)
                {
                    hasOutgoing = true;
                    break;
                }
            }

            if (!hasOutgoing)
            {
                warnings.push_back({GraphWarning::Level::Warning,
                    "Dead-end state: \"" + state.name + "\" has no outgoing transitions",
                    state.id});
            }
        }
    }

    void AnimatorGraphValidator::checkUnreachableStates(const animator::AnimatorGraph& graph,
                                                         std::vector<GraphWarning>& warnings)
    {
        if (graph.states.empty())
            return;

        // BFS from default state + any-state targets
        std::unordered_set<uint32_t> reachable;
        std::queue<uint32_t> queue;

        if (graph.defaultStateId != 0)
        {
            reachable.insert(graph.defaultStateId);
            queue.push(graph.defaultStateId);
        }

        // Any State (sourceStateId==0) transitions reach their targets
        for (const auto& transition : graph.transitions)
        {
            if (transition.sourceStateId == 0)
            {
                if (reachable.insert(transition.targetStateId).second)
                    queue.push(transition.targetStateId);
            }
        }

        while (!queue.empty())
        {
            uint32_t current = queue.front();
            queue.pop();

            for (const auto& transition : graph.transitions)
            {
                if (transition.sourceStateId == current)
                {
                    if (reachable.insert(transition.targetStateId).second)
                        queue.push(transition.targetStateId);
                }
            }
        }

        for (const auto& state : graph.states)
        {
            if (reachable.find(state.id) == reachable.end())
            {
                warnings.push_back({GraphWarning::Level::Warning,
                    "Unreachable state: \"" + state.name + "\"",
                    state.id});
            }
        }
    }

    void AnimatorGraphValidator::checkMissingAnimations(const animator::AnimatorGraph& graph,
                                                         std::vector<GraphWarning>& warnings)
    {
        for (const auto& state : graph.states)
        {
            if (!state.blendTree.has_value() && !state.animationRef.isValid())
            {
                warnings.push_back({GraphWarning::Level::Warning,
                    "State \"" + state.name + "\" has no animation clip assigned",
                    state.id});
            }
        }
    }

    void AnimatorGraphValidator::checkUndefinedParameters(const animator::AnimatorGraph& graph,
                                                           std::vector<GraphWarning>& warnings)
    {
        std::unordered_set<std::string> definedParams;
        for (const auto& param : graph.parameters)
        {
            definedParams.insert(param.name);
        }

        for (const auto& transition : graph.transitions)
        {
            for (const auto& condition : transition.conditions)
            {
                if (definedParams.find(condition.parameterName) == definedParams.end())
                {
                    warnings.push_back({GraphWarning::Level::Error,
                        "Transition references undefined parameter: \"" + condition.parameterName + "\"",
                        0, transition.id});
                }
            }
        }
    }
}
