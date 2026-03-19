#include "AnimatorAsset.hpp"
#include "../asset/AssetRef.hpp"
#include <nlohmann/json.hpp>
#include <format>

namespace animator
{
    using json = nlohmann::json;

    AnimatorParameterValue AnimatorAsset::deserializeParameterValue(const json& j, AnimatorParameterType type)
    {
        try
        {
            switch (type)
            {
            case AnimatorParameterType::Float:
                return j.is_number() ? j.get<float>() : 0.0f;
            case AnimatorParameterType::Int:
                return j.is_number_integer() ? j.get<int32_t>() : 0;
            case AnimatorParameterType::Bool:
            case AnimatorParameterType::Trigger:
                return j.is_boolean() ? j.get<bool>() : false;
            default:
                return 0.0f;
            }
        }
        catch (const json::exception&)
        {
            return 0.0f;
        }
    }

    AnimatorParameter AnimatorAsset::deserializeParameter(const json& j)
    {
        AnimatorParameter param;
        param.name = j.value("name", "");
        param.type = stringToParameterType(j.value("type", "Float"));
        if (j.contains("default"))
        {
            param.defaultValue = deserializeParameterValue(j["default"], param.type);
        }
        return param;
    }

    BlendTreeData AnimatorAsset::deserializeBlendTree(const json& j)
    {
        BlendTreeData blendTree;
        std::string typeStr = j.value("type", "1D");
        blendTree.type = (typeStr == "2D") ? BlendTreeType::BlendTree2D : BlendTreeType::BlendTree1D;
        blendTree.parameterName = j.value("parameterName", "");
        blendTree.parameterNameY = j.value("parameterNameY", "");

        if (j.contains("entries") && j["entries"].is_array())
        {
            for (const auto& entryJson : j["entries"])
            {
                BlendTreeEntry entry;
                entry.animationRef = asset::AssetRef::fromHexString(entryJson.value("animationRef", ""));
                entry.threshold = entryJson.value("threshold", 0.0f);
                if (entryJson.contains("position") && entryJson["position"].is_array()
                    && entryJson["position"].size() >= 2)
                {
                    entry.position.x = entryJson["position"][0].get<float>();
                    entry.position.y = entryJson["position"][1].get<float>();
                }
                blendTree.entries.push_back(std::move(entry));
            }
        }
        return blendTree;
    }

    AnimatorState AnimatorAsset::deserializeState(const json& j)
    {
        AnimatorState state;
        state.id = j.value("id", 0u);
        state.name = j.value("name", "");
        state.animationRef = asset::AssetRef::fromHexString(j.value("animationRef", ""));
        state.playbackSpeed = j.value("playbackSpeed", 1.0f);
        state.loop = j.value("loop", true);

        if (j.contains("position") && j["position"].is_array() && j["position"].size() >= 2)
        {
            state.position.x = j["position"][0].get<float>();
            state.position.y = j["position"][1].get<float>();
        }

        if (j.contains("events") && j["events"].is_array())
        {
            for (const auto& eventJson : j["events"])
            {
                AnimationEvent event;
                event.name = eventJson.value("name", "");
                event.normalizedTime = eventJson.value("normalizedTime", 0.0f);
                event.payload = eventJson.value("payload", "");
                state.events.push_back(std::move(event));
            }
        }

        if (j.contains("blendTree") && j["blendTree"].is_object())
        {
            state.blendTree = deserializeBlendTree(j["blendTree"]);
        }

        return state;
    }

    TransitionCondition AnimatorAsset::deserializeCondition(const json& j)
    {
        TransitionCondition condition;
        condition.parameterName = j.value("parameter", "");
        condition.op = stringToComparisonOperator(j.value("operator", ">"));

        if (j.contains("value"))
        {
            const auto& val = j["value"];
            if (val.is_boolean())
                condition.value = val.get<bool>();
            else if (val.is_number_integer())
                condition.value = val.get<int32_t>();
            else if (val.is_number())
                condition.value = val.get<float>();
        }

        return condition;
    }

    AnimatorTransition AnimatorAsset::deserializeTransition(const json& j)
    {
        AnimatorTransition transition;
        transition.id = j.value("id", 0u);
        transition.sourceStateId = j.value("sourceState", 0u);
        transition.targetStateId = j.value("targetState", 0u);
        transition.blendDuration = j.value("blendDuration", 0.25f);
        transition.hasExitTime = j.value("hasExitTime", false);
        transition.exitTime = j.value("exitTime", 1.0f);
        transition.priority = j.value("priority", 0);

        if (j.contains("conditions") && j["conditions"].is_array())
        {
            for (const auto& condJson : j["conditions"])
            {
                transition.conditions.push_back(deserializeCondition(condJson));
            }
        }

        return transition;
    }

    AnimationLayerData AnimatorAsset::deserializeLayer(const json& j, const WarningLogger& logWarning)
    {
        AnimationLayerData layer;
        layer.name = j.value("name", "Layer");
        layer.weight = j.value("weight", 1.0f);
        layer.blendMode = stringToLayerBlendMode(j.value("blendMode", "Override"));
        layer.sourceMode = stringToLayerSourceMode(j.value("sourceMode", "StateMachine"));
        layer.boneMaskName = j.value("boneMask", "");
        layer.directClipRef = asset::AssetRef::fromHexString(j.value("directClipRef", ""));
        layer.directClipLoop = j.value("directClipLoop", true);
        layer.directClipSpeed = j.value("directClipSpeed", 1.0f);
        layer.additiveRefPose = stringToAdditiveReferencePose(j.value("additiveRefPose", "FirstFrame"));
        layer.additiveRefFrame = j.value("additiveRefFrame", 0.0f);

        if (j.contains("graph") && j["graph"].is_object())
        {
            parseGraph(j["graph"], layer.graph, logWarning);
        }

        return layer;
    }

    BoneMaskDefinition AnimatorAsset::deserializeBoneMask(const json& j)
    {
        BoneMaskDefinition mask;
        mask.name = j.value("name", "");
        if (j.contains("bones") && j["bones"].is_array())
        {
            for (const auto& boneJson : j["bones"])
            {
                if (boneJson.is_string())
                {
                    mask.includedBoneNames.push_back(boneJson.get<std::string>());
                }
            }
        }
        return mask;
    }

    void AnimatorAsset::parseAnimatorParameters(const json& j, AnimatorGraph& graph, const WarningLogger& logWarning)
    {
        if (!j.contains("parameters") || !j["parameters"].is_array())
        {
            return;
        }

        for (size_t i = 0; i < j["parameters"].size(); ++i)
        {
            const auto& paramJson = j["parameters"][i];
            if (!paramJson.is_object())
            {
                logWarning(std::format("Parameter at index {} is not an object, skipping", i));
                continue;
            }
            graph.parameters.push_back(deserializeParameter(paramJson));
        }
    }

    void AnimatorAsset::parseAnimatorStates(const json& j, AnimatorGraph& graph, const WarningLogger& logWarning)
    {
        if (!j.contains("states") || !j["states"].is_array())
        {
            return;
        }

        for (size_t i = 0; i < j["states"].size(); ++i)
        {
            const auto& stateJson = j["states"][i];
            if (!stateJson.is_object())
            {
                logWarning(std::format("State at index {} is not an object, skipping", i));
                continue;
            }
            AnimatorState state = deserializeState(stateJson);
            graph.nextStateId = std::max(graph.nextStateId, state.id + 1);
            graph.states.push_back(std::move(state));
        }
    }

    void AnimatorAsset::parseAnimatorTransitions(const json& j, AnimatorGraph& graph, const WarningLogger& logWarning)
    {
        if (!j.contains("transitions") || !j["transitions"].is_array())
        {
            return;
        }

        for (size_t i = 0; i < j["transitions"].size(); ++i)
        {
            const auto& transJson = j["transitions"][i];
            if (!transJson.is_object())
            {
                logWarning(std::format("Transition at index {} is not an object, skipping", i));
                continue;
            }
            AnimatorTransition transition = deserializeTransition(transJson);
            graph.nextTransitionId = std::max(graph.nextTransitionId, transition.id + 1);
            graph.transitions.push_back(std::move(transition));
        }
    }

    void AnimatorAsset::parseGraphLayout(const json& j, AnimatorGraph& graph, const WarningLogger& logWarning)
    {
        graph.defaultStateId = j.value("defaultState", 1u);

        if (j.contains("anyStatePosition") && j["anyStatePosition"].is_array() && j["anyStatePosition"].size() >= 2)
        {
            graph.anyStatePosition.x = j["anyStatePosition"][0].get<float>();
            graph.anyStatePosition.y = j["anyStatePosition"][1].get<float>();
        }

        if (j.contains("entryPosition") && j["entryPosition"].is_array() && j["entryPosition"].size() >= 2)
        {
            graph.entryPosition.x = j["entryPosition"][0].get<float>();
            graph.entryPosition.y = j["entryPosition"][1].get<float>();
        }

        if (!graph.states.empty())
        {
            bool defaultExists = graph.findStateById(graph.defaultStateId) != nullptr;
            if (!defaultExists)
            {
                logWarning(std::format("Default state {} not found, using first state",
                                        graph.defaultStateId));
                graph.defaultStateId = graph.states[0].id;
            }
        }
    }

    void AnimatorAsset::parseGraph(const json& j, AnimatorGraph& graph, const WarningLogger& logWarning)
    {
        parseAnimatorParameters(j, graph, logWarning);
        parseAnimatorStates(j, graph, logWarning);
        parseAnimatorTransitions(j, graph, logWarning);
        parseGraphLayout(j, graph, logWarning);
    }

    AnimatorData AnimatorAsset::parseAnimatorData(const json& j, const WarningLogger& logWarning)
    {
        AnimatorData animator;
        animator.version = j.value("version", ANIMATOR_FORMAT_VERSION);
        animator.name = j.value("name", "Unnamed Animator");
        if (animator.name.empty())
        {
            animator.name = "Unnamed Animator";
            logWarning("Animator has empty name, using default");
        }

        if (j.contains("boneMasks") && j["boneMasks"].is_array())
        {
            for (const auto& maskJson : j["boneMasks"])
                if (maskJson.is_object())
                    animator.boneMasks.push_back(deserializeBoneMask(maskJson));
        }

        if (j.contains("layers") && j["layers"].is_array() && !j["layers"].empty())
        {
            for (size_t i = 0; i < j["layers"].size(); ++i)
            {
                const auto& layerJson = j["layers"][i];
                if (!layerJson.is_object())
                {
                    logWarning(std::format("Layer at index {} is not an object, skipping", i));
                    continue;
                }
                animator.layers.push_back(deserializeLayer(layerJson, logWarning));
            }
            if (!animator.layers.empty())
                animator.graph = animator.layers[0].graph;
        }
        else
        {
            parseGraph(j, animator.graph, logWarning);
        }

        return animator;
    }
}
