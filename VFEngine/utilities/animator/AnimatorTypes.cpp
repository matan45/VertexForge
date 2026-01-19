#include "AnimatorTypes.hpp"
#include <algorithm>

namespace animator
{
    const AnimatorState* AnimatorGraph::findStateById(uint32_t id) const
    {
        auto it = std::find_if(states.begin(), states.end(),
            [id](const AnimatorState& s) { return s.id == id; });
        return it != states.end() ? &(*it) : nullptr;
    }

    const AnimatorState* AnimatorGraph::findStateByName(const std::string& name) const
    {
        auto it = std::find_if(states.begin(), states.end(),
            [&name](const AnimatorState& s) { return s.name == name; });
        return it != states.end() ? &(*it) : nullptr;
    }

    AnimatorState* AnimatorGraph::findStateById(uint32_t id)
    {
        auto it = std::find_if(states.begin(), states.end(),
            [id](AnimatorState& s) { return s.id == id; });
        return it != states.end() ? &(*it) : nullptr;
    }

    AnimatorState* AnimatorGraph::findStateByName(const std::string& name)
    {
        auto it = std::find_if(states.begin(), states.end(),
            [&name](AnimatorState& s) { return s.name == name; });
        return it != states.end() ? &(*it) : nullptr;
    }

    const AnimatorParameter* AnimatorGraph::findParameter(const std::string& name) const
    {
        auto it = std::find_if(parameters.begin(), parameters.end(),
            [&name](const AnimatorParameter& p) { return p.name == name; });
        return it != parameters.end() ? &(*it) : nullptr;
    }

    std::vector<const AnimatorTransition*> AnimatorGraph::getTransitionsFromState(uint32_t stateId) const
    {
        std::vector<const AnimatorTransition*> result;
        for (const auto& transition : transitions)
        {
            // Include transitions from this state OR "Any State" transitions (sourceStateId == 0)
            if (transition.sourceStateId == stateId || transition.sourceStateId == 0)
            {
                result.push_back(&transition);
            }
        }
        // Sort by priority (lower = higher priority)
        std::sort(result.begin(), result.end(),
            [](const AnimatorTransition* a, const AnimatorTransition* b)
            {
                return a->priority < b->priority;
            });
        return result;
    }

    void AnimatorRuntimeParameters::setFloat(const std::string& name, float value)
    {
        values[name] = value;
    }

    void AnimatorRuntimeParameters::setInt(const std::string& name, int32_t value)
    {
        values[name] = value;
    }

    void AnimatorRuntimeParameters::setBool(const std::string& name, bool value)
    {
        values[name] = value;
    }

    void AnimatorRuntimeParameters::setTrigger(const std::string& name)
    {
        values[name] = true;
    }

    void AnimatorRuntimeParameters::resetTrigger(const std::string& name)
    {
        auto it = values.find(name);
        if (it != values.end() && std::holds_alternative<bool>(it->second))
        {
            it->second = false;
        }
    }

    float AnimatorRuntimeParameters::getFloat(const std::string& name, float defaultVal) const
    {
        auto it = values.find(name);
        if (it != values.end() && std::holds_alternative<float>(it->second))
        {
            return std::get<float>(it->second);
        }
        return defaultVal;
    }

    int32_t AnimatorRuntimeParameters::getInt(const std::string& name, int32_t defaultVal) const
    {
        auto it = values.find(name);
        if (it != values.end() && std::holds_alternative<int32_t>(it->second))
        {
            return std::get<int32_t>(it->second);
        }
        return defaultVal;
    }

    bool AnimatorRuntimeParameters::getBool(const std::string& name, bool defaultVal) const
    {
        auto it = values.find(name);
        if (it != values.end() && std::holds_alternative<bool>(it->second))
        {
            return std::get<bool>(it->second);
        }
        return defaultVal;
    }

    bool AnimatorRuntimeParameters::getTrigger(const std::string& name) const
    {
        return getBool(name, false);
    }

    void AnimatorRuntimeParameters::initializeFromGraph(const AnimatorGraph& graph)
    {
        values.clear();
        for (const auto& param : graph.parameters)
        {
            values[param.name] = param.defaultValue;
        }
    }

    const char* parameterTypeToString(AnimatorParameterType type)
    {
        switch (type)
        {
            case AnimatorParameterType::Float:   return "Float";
            case AnimatorParameterType::Int:     return "Int";
            case AnimatorParameterType::Bool:    return "Bool";
            case AnimatorParameterType::Trigger: return "Trigger";
            default:                             return "Float";
        }
    }

    AnimatorParameterType stringToParameterType(const std::string& str)
    {
        if (str == "Int")     return AnimatorParameterType::Int;
        if (str == "Bool")    return AnimatorParameterType::Bool;
        if (str == "Trigger") return AnimatorParameterType::Trigger;
        return AnimatorParameterType::Float;
    }

    const char* comparisonOperatorToString(ComparisonOperator op)
    {
        switch (op)
        {
            case ComparisonOperator::Greater:      return ">";
            case ComparisonOperator::Less:         return "<";
            case ComparisonOperator::GreaterEqual: return ">=";
            case ComparisonOperator::LessEqual:    return "<=";
            case ComparisonOperator::Equal:        return "==";
            case ComparisonOperator::NotEqual:     return "!=";
            default:                               return ">";
        }
    }

    ComparisonOperator stringToComparisonOperator(const std::string& str)
    {
        if (str == "<")  return ComparisonOperator::Less;
        if (str == ">=") return ComparisonOperator::GreaterEqual;
        if (str == "<=") return ComparisonOperator::LessEqual;
        if (str == "==") return ComparisonOperator::Equal;
        if (str == "!=") return ComparisonOperator::NotEqual;
        return ComparisonOperator::Greater;
    }

    bool evaluateCondition(const TransitionCondition& condition,
                           const AnimatorRuntimeParameters& params)
    {
        auto it = params.values.find(condition.parameterName);
        if (it == params.values.end())
        {
            return false;
        }

        const auto& paramValue = it->second;
        const auto& conditionValue = condition.value;

        // Handle trigger type - just check if it's set
        if (std::holds_alternative<bool>(paramValue) && std::holds_alternative<bool>(conditionValue))
        {
            bool pVal = std::get<bool>(paramValue);
            bool cVal = std::get<bool>(conditionValue);
            switch (condition.op)
            {
                case ComparisonOperator::Equal:    return pVal == cVal;
                case ComparisonOperator::NotEqual: return pVal != cVal;
                default:                           return pVal == cVal;
            }
        }

        // Handle float comparison
        if (std::holds_alternative<float>(paramValue))
        {
            float pVal = std::get<float>(paramValue);
            float cVal = 0.0f;
            if (std::holds_alternative<float>(conditionValue))
                cVal = std::get<float>(conditionValue);
            else if (std::holds_alternative<int32_t>(conditionValue))
                cVal = static_cast<float>(std::get<int32_t>(conditionValue));

            switch (condition.op)
            {
                case ComparisonOperator::Greater:      return pVal > cVal;
                case ComparisonOperator::Less:         return pVal < cVal;
                case ComparisonOperator::GreaterEqual: return pVal >= cVal;
                case ComparisonOperator::LessEqual:    return pVal <= cVal;
                case ComparisonOperator::Equal:        return pVal == cVal;
                case ComparisonOperator::NotEqual:     return pVal != cVal;
                default:                               return false;
            }
        }

        // Handle int comparison
        if (std::holds_alternative<int32_t>(paramValue))
        {
            int32_t pVal = std::get<int32_t>(paramValue);
            int32_t cVal = 0;
            if (std::holds_alternative<int32_t>(conditionValue))
                cVal = std::get<int32_t>(conditionValue);
            else if (std::holds_alternative<float>(conditionValue))
                cVal = static_cast<int32_t>(std::get<float>(conditionValue));

            switch (condition.op)
            {
                case ComparisonOperator::Greater:      return pVal > cVal;
                case ComparisonOperator::Less:         return pVal < cVal;
                case ComparisonOperator::GreaterEqual: return pVal >= cVal;
                case ComparisonOperator::LessEqual:    return pVal <= cVal;
                case ComparisonOperator::Equal:        return pVal == cVal;
                case ComparisonOperator::NotEqual:     return pVal != cVal;
                default:                               return false;
            }
        }

        return false;
    }

    bool evaluateAllConditions(const std::vector<TransitionCondition>& conditions,
                               const AnimatorRuntimeParameters& params)
    {
        if (conditions.empty())
        {
            return true;  // No conditions means always valid
        }

        for (const auto& condition : conditions)
        {
            if (!evaluateCondition(condition, params))
            {
                return false;  // AND logic - all must pass
            }
        }
        return true;
    }
}
