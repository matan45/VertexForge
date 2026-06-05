#pragma once
#include <animator/AnimatorTypes.hpp>
#include <string>
#include <vector>
#include <cstdint>

namespace services
{
    struct AnimatorParameterDebugInfo
    {
        std::string name;
        animator::AnimatorParameterType type = animator::AnimatorParameterType::Float;
        animator::AnimatorParameterValue currentValue = 0.0f;
    };

    struct AnimatorConditionEval
    {
        uint32_t transitionId = 0;
        std::string parameterName;
        animator::ComparisonOperator op = animator::ComparisonOperator::Greater;
        animator::AnimatorParameterValue threshold = 0.0f;
        animator::AnimatorParameterValue currentValue = 0.0f;
        bool result = false;
    };

    struct AnimatorRuntimeDebugData
    {
        bool isValid = false;

        // Current state machine state
        uint32_t currentStateId = 0;
        uint32_t previousStateId = 0;
        float stateTime = 0.0f;
        bool isBlending = false;
        float blendProgress = 0.0f;
        uint32_t activeTransitionId = 0;

        // Current parameter values
        std::vector<AnimatorParameterDebugInfo> parameters;

        // Condition evaluation results for all outgoing transitions from current state
        std::vector<AnimatorConditionEval> conditionResults;
    };
}
