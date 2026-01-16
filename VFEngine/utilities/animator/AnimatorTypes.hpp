#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <cstdint>
#include <glm/glm.hpp>

namespace animator
{
    // ============================================================
    // Parameter Types
    // ============================================================

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

    // ============================================================
    // Transition Conditions
    // ============================================================

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

    // ============================================================
    // Animation State
    // ============================================================

    struct AnimatorState
    {
        uint32_t id = 0;
        std::string name;
        std::string animationPath;  // Path to .vfAnim file
        float playbackSpeed = 1.0f;
        bool loop = true;

        // Node graph editor position
        glm::vec2 position{0.0f, 0.0f};
    };

    // ============================================================
    // Transition
    // ============================================================

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

    // ============================================================
    // Animator Graph
    // ============================================================

    struct AnimatorGraph
    {
        std::vector<AnimatorState> states;
        std::vector<AnimatorTransition> transitions;
        std::vector<AnimatorParameter> parameters;
        uint32_t defaultStateId = 0;
        uint32_t nextStateId = 1;
        uint32_t nextTransitionId = 1;

        // "Any State" node position in the node graph editor (special node that can transition to any state)
        glm::vec2 anyStatePosition{-200.0f, 0.0f};

        // "Entry" node position in the node graph editor
        glm::vec2 entryPosition{-400.0f, 0.0f};

        const AnimatorState* findStateById(uint32_t id) const;
        const AnimatorState* findStateByName(const std::string& name) const;
        AnimatorState* findStateById(uint32_t id);
        AnimatorState* findStateByName(const std::string& name);
        const AnimatorParameter* findParameter(const std::string& name) const;
        std::vector<const AnimatorTransition*> getTransitionsFromState(uint32_t stateId) const;
    };

    // ============================================================
    // Animator Data (full asset)
    // ============================================================

    struct AnimatorData
    {
        std::string version = "1.0";
        std::string name;
        AnimatorGraph graph;
    };

    // ============================================================
    // Runtime Parameter State
    // ============================================================

    struct AnimatorRuntimeParameters
    {
        std::unordered_map<std::string, AnimatorParameterValue> values;

        void setFloat(const std::string& name, float value);
        void setInt(const std::string& name, int32_t value);
        void setBool(const std::string& name, bool value);
        void setTrigger(const std::string& name);
        void resetTrigger(const std::string& name);

        float getFloat(const std::string& name, float defaultVal = 0.0f) const;
        int32_t getInt(const std::string& name, int32_t defaultVal = 0) const;
        bool getBool(const std::string& name, bool defaultVal = false) const;
        bool getTrigger(const std::string& name) const;

        void initializeFromGraph(const AnimatorGraph& graph);
    };

    // ============================================================
    // Utility Functions
    // ============================================================

    const char* parameterTypeToString(AnimatorParameterType type);
    AnimatorParameterType stringToParameterType(const std::string& str);
    const char* comparisonOperatorToString(ComparisonOperator op);
    ComparisonOperator stringToComparisonOperator(const std::string& str);

    bool evaluateCondition(const TransitionCondition& condition,
                           const AnimatorRuntimeParameters& params);
    bool evaluateAllConditions(const std::vector<TransitionCondition>& conditions,
                               const AnimatorRuntimeParameters& params);
}
