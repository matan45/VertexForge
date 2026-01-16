#pragma once

#include "AnimatorTypes.hpp"
#include <nlohmann/json_fwd.hpp>
#include <string_view>
#include <optional>

namespace animator
{
    inline constexpr const char* ANIMATOR_FORMAT_VERSION = "1.0";

    class AnimatorAsset
    {
    public:
        static std::optional<AnimatorData> load(std::string_view path);
        static bool save(std::string_view path, const AnimatorData& animator);
        static AnimatorData createDefault(const std::string& name = "New Animator");

    private:
        static nlohmann::json serializeParameter(const AnimatorParameter& param);
        static AnimatorParameter deserializeParameter(const nlohmann::json& j);

        static nlohmann::json serializeState(const AnimatorState& state);
        static AnimatorState deserializeState(const nlohmann::json& j);

        static nlohmann::json serializeTransition(const AnimatorTransition& transition);
        static AnimatorTransition deserializeTransition(const nlohmann::json& j);

        static nlohmann::json serializeCondition(const TransitionCondition& condition);
        static TransitionCondition deserializeCondition(const nlohmann::json& j);

        static nlohmann::json serializeParameterValue(const AnimatorParameterValue& val, AnimatorParameterType type);
        static AnimatorParameterValue deserializeParameterValue(const nlohmann::json& j, AnimatorParameterType type);
    };
}
