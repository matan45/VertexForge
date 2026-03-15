#pragma once

#include "AnimatorTypes.hpp"
#include <nlohmann/json_fwd.hpp>
#include <string_view>
#include <optional>
#include <functional>

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
        using WarningLogger = std::function<void(const std::string&)>;

        // Serialization helpers
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

        static nlohmann::json serializeBlendTree(const BlendTreeData& blendTree);
        static BlendTreeData deserializeBlendTree(const nlohmann::json& j);

        static nlohmann::json serializeLayer(const AnimationLayerData& layer);
        static AnimationLayerData deserializeLayer(const nlohmann::json& j, const WarningLogger& logWarning);

        static nlohmann::json serializeBoneMask(const BoneMaskDefinition& mask);
        static BoneMaskDefinition deserializeBoneMask(const nlohmann::json& j);

        static nlohmann::json serializeGraph(const AnimatorGraph& graph);
        static void parseGraph(const nlohmann::json& j, AnimatorGraph& graph, const WarningLogger& logWarning);

        // Load helpers
        static std::optional<nlohmann::json> readJsonFromFile(std::string_view path);
        static AnimatorData parseAnimatorData(const nlohmann::json& j, const WarningLogger& logWarning);
        static void parseAnimatorParameters(const nlohmann::json& j, AnimatorGraph& graph, const WarningLogger& logWarning);
        static void parseAnimatorStates(const nlohmann::json& j, AnimatorGraph& graph, const WarningLogger& logWarning);
        static void parseAnimatorTransitions(const nlohmann::json& j, AnimatorGraph& graph, const WarningLogger& logWarning);
        static void parseGraphLayout(const nlohmann::json& j, AnimatorGraph& graph, const WarningLogger& logWarning);

        // Save helpers
        static nlohmann::json buildAnimatorJson(const AnimatorData& animator);
    };
}
