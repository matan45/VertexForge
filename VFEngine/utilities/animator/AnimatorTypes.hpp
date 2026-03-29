#pragma once

#include <algorithm>
#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <optional>
#include <cstdint>
#include <glm/glm.hpp>
#include "../asset/AssetRef.hpp"
#include "AnimationEventTypes.hpp"
#include "AnimationLayerTypes.hpp"
#include "BlendTreeTypes.hpp"

namespace animator
{
    enum class AnimatorParameterType : uint8_t
    {
        Float,
        Int,
        Bool,
        Trigger
    };

    using AnimatorParameterValue = std::variant<float, int32_t, bool>;

    struct AnimatorParameter
    {
        std::string name;
        AnimatorParameterType type = AnimatorParameterType::Float;
        AnimatorParameterValue defaultValue = 0.0f;
    };

    enum class ComparisonOperator : uint8_t
    {
        Greater,        // >
        Less,           // <
        GreaterEqual,   // >=
        LessEqual,      // <=
        Equal,          // ==
        NotEqual        // !=
    };

    struct TransitionCondition
    {
        std::string parameterName;
        ComparisonOperator op = ComparisonOperator::Greater;
        AnimatorParameterValue value = 0.0f;
    };

    struct AnimatorState
    {
        uint32_t id = 0;
        std::string name;
        asset::AssetRef animationRef;  // Reference to .vfAnim asset
        float playbackSpeed = 1.0f;
        bool loop = true;

        // Node graph editor position
        glm::vec2 position{0.0f, 0.0f};

        // Animation events triggered at specific normalized times
        std::vector<AnimationEvent> events;

        // Optional blend tree (replaces single animationPath when set)
        std::optional<BlendTreeData> blendTree;
    };

    struct AnimatorTransition
    {
        uint32_t id = 0;
        uint32_t sourceStateId = 0;  // 0 = "Any State"
        uint32_t targetStateId = 0;
        std::vector<TransitionCondition> conditions;  // AND logic
        float blendDuration = 0.25f;  // Seconds
        bool hasExitTime = false;
        float exitTime = 1.0f;  // Normalized time (0-1) when exit time is enabled
        int32_t priority = 0;  // Lower = higher priority
    };

    struct AnimatorGraph
    {
        std::vector<AnimatorState> states;
        std::vector<AnimatorTransition> transitions;
        std::vector<AnimatorParameter> parameters;
        uint32_t defaultStateId = 0;
        uint32_t nextStateId = 1;
        uint32_t nextTransitionId = 1;

        // "Any State" node position in the node graph editor (special node that can transition to any state)
        glm::vec2 anyStatePosition{50.0f, 200.0f};

        // "Entry" node position in the node graph editor
        glm::vec2 entryPosition{50.0f, 50.0f};

        inline const AnimatorState* findStateById(uint32_t id) const {
            auto it = std::find_if(states.begin(), states.end(),
                [id](const AnimatorState& s) { return s.id == id; });
            return it != states.end() ? &(*it) : nullptr;
        }
        inline const AnimatorState* findStateByName(const std::string& name) const {
            auto it = std::find_if(states.begin(), states.end(),
                [&name](const AnimatorState& s) { return s.name == name; });
            return it != states.end() ? &(*it) : nullptr;
        }
        inline AnimatorState* findStateById(uint32_t id) {
            auto it = std::find_if(states.begin(), states.end(),
                [id](AnimatorState& s) { return s.id == id; });
            return it != states.end() ? &(*it) : nullptr;
        }
        inline AnimatorState* findStateByName(const std::string& name) {
            auto it = std::find_if(states.begin(), states.end(),
                [&name](AnimatorState& s) { return s.name == name; });
            return it != states.end() ? &(*it) : nullptr;
        }
        inline const AnimatorParameter* findParameter(const std::string& name) const {
            auto it = std::find_if(parameters.begin(), parameters.end(),
                [&name](const AnimatorParameter& p) { return p.name == name; });
            return it != parameters.end() ? &(*it) : nullptr;
        }
        inline std::vector<const AnimatorTransition*> getTransitionsFromState(uint32_t stateId) const {
            std::vector<const AnimatorTransition*> result;
            for (const auto& transition : transitions) {
                if (transition.sourceStateId == stateId || transition.sourceStateId == 0)
                    result.push_back(&transition);
            }
            std::sort(result.begin(), result.end(),
                [](const AnimatorTransition* a, const AnimatorTransition* b) {
                    return a->priority < b->priority;
                });
            return result;
        }
    };

    struct AnimationLayerData
    {
        std::string name = "Base Layer";
        float weight = 1.0f;
        LayerBlendMode blendMode = LayerBlendMode::Override;
        LayerSourceMode sourceMode = LayerSourceMode::StateMachine;
        std::string boneMaskName;
        asset::AssetRef directClipRef;
        bool directClipLoop = true;
        float directClipSpeed = 1.0f;
        AdditiveReferencePose additiveRefPose = AdditiveReferencePose::FirstFrame;
        float additiveRefFrame = 0.0f;
        AnimatorGraph graph;
    };

    struct AnimatorData
    {
        std::string version = "1.0";
        std::string name;
        AnimatorGraph graph;
        std::vector<AnimationLayerData> layers;
        std::vector<BoneMaskDefinition> boneMasks;
    };

    struct AnimatorRuntimeParameters
    {
        std::unordered_map<std::string, AnimatorParameterValue> values;

        inline void setFloat(const std::string& name, float value) { values[name] = value; }
        inline void setInt(const std::string& name, int32_t value) { values[name] = value; }
        inline void setBool(const std::string& name, bool value) { values[name] = value; }
        inline void setTrigger(const std::string& name) { values[name] = true; }
        inline void resetTrigger(const std::string& name) {
            auto it = values.find(name);
            if (it != values.end() && std::holds_alternative<bool>(it->second))
                it->second = false;
        }

        inline float getFloat(const std::string& name, float defaultVal = 0.0f) const {
            auto it = values.find(name);
            return (it != values.end() && std::holds_alternative<float>(it->second))
                ? std::get<float>(it->second) : defaultVal;
        }
        inline int32_t getInt(const std::string& name, int32_t defaultVal = 0) const {
            auto it = values.find(name);
            return (it != values.end() && std::holds_alternative<int32_t>(it->second))
                ? std::get<int32_t>(it->second) : defaultVal;
        }
        inline bool getBool(const std::string& name, bool defaultVal = false) const {
            auto it = values.find(name);
            return (it != values.end() && std::holds_alternative<bool>(it->second))
                ? std::get<bool>(it->second) : defaultVal;
        }
        inline bool getTrigger(const std::string& name) const { return getBool(name, false); }

        inline void initializeFromGraph(const AnimatorGraph& graph) {
            values.clear();
            for (const auto& param : graph.parameters)
                values[param.name] = param.defaultValue;
        }
    };

    inline const char* parameterTypeToString(AnimatorParameterType type) {
        switch (type) {
            case AnimatorParameterType::Float:   return "Float";
            case AnimatorParameterType::Int:     return "Int";
            case AnimatorParameterType::Bool:    return "Bool";
            case AnimatorParameterType::Trigger: return "Trigger";
            default:                             return "Float";
        }
    }
    inline AnimatorParameterType stringToParameterType(const std::string& str) {
        if (str == "Int")     return AnimatorParameterType::Int;
        if (str == "Bool")    return AnimatorParameterType::Bool;
        if (str == "Trigger") return AnimatorParameterType::Trigger;
        return AnimatorParameterType::Float;
    }
    inline const char* comparisonOperatorToString(ComparisonOperator op) {
        switch (op) {
            case ComparisonOperator::Greater:      return ">";
            case ComparisonOperator::Less:         return "<";
            case ComparisonOperator::GreaterEqual: return ">=";
            case ComparisonOperator::LessEqual:    return "<=";
            case ComparisonOperator::Equal:        return "==";
            case ComparisonOperator::NotEqual:     return "!=";
            default:                               return ">";
        }
    }
    inline ComparisonOperator stringToComparisonOperator(const std::string& str) {
        if (str == "<")  return ComparisonOperator::Less;
        if (str == ">=") return ComparisonOperator::GreaterEqual;
        if (str == "<=") return ComparisonOperator::LessEqual;
        if (str == "==") return ComparisonOperator::Equal;
        if (str == "!=") return ComparisonOperator::NotEqual;
        return ComparisonOperator::Greater;
    }

    inline bool evaluateCondition(const TransitionCondition& condition,
                                  const AnimatorRuntimeParameters& params) {
        auto it = params.values.find(condition.parameterName);
        if (it == params.values.end()) return false;
        const auto& paramValue = it->second;
        const auto& conditionValue = condition.value;
        if (std::holds_alternative<bool>(paramValue) && std::holds_alternative<bool>(conditionValue)) {
            bool pVal = std::get<bool>(paramValue);
            bool cVal = std::get<bool>(conditionValue);
            switch (condition.op) {
                case ComparisonOperator::Equal:    return pVal == cVal;
                case ComparisonOperator::NotEqual: return pVal != cVal;
                default:                           return pVal == cVal;
            }
        }
        if (std::holds_alternative<float>(paramValue)) {
            float pVal = std::get<float>(paramValue);
            float cVal = std::holds_alternative<float>(conditionValue) ? std::get<float>(conditionValue)
                       : std::holds_alternative<int32_t>(conditionValue) ? static_cast<float>(std::get<int32_t>(conditionValue)) : 0.0f;
            switch (condition.op) {
                case ComparisonOperator::Greater:      return pVal > cVal;
                case ComparisonOperator::Less:         return pVal < cVal;
                case ComparisonOperator::GreaterEqual: return pVal >= cVal;
                case ComparisonOperator::LessEqual:    return pVal <= cVal;
                case ComparisonOperator::Equal:        return pVal == cVal;
                case ComparisonOperator::NotEqual:     return pVal != cVal;
                default:                               return false;
            }
        }
        if (std::holds_alternative<int32_t>(paramValue)) {
            int32_t pVal = std::get<int32_t>(paramValue);
            int32_t cVal = std::holds_alternative<int32_t>(conditionValue) ? std::get<int32_t>(conditionValue)
                         : std::holds_alternative<float>(conditionValue) ? static_cast<int32_t>(std::get<float>(conditionValue)) : 0;
            switch (condition.op) {
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

    inline bool evaluateAllConditions(const std::vector<TransitionCondition>& conditions,
                                      const AnimatorRuntimeParameters& params) {
        if (conditions.empty()) return true;
        for (const auto& condition : conditions)
            if (!evaluateCondition(condition, params)) return false;
        return true;
    }
}
