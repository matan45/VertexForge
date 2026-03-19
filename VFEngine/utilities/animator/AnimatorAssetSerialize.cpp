#include "AnimatorAsset.hpp"
#include "../print/Log.hpp"
#include "../asset/AssetRef.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

namespace animator
{
    using json = nlohmann::json;

    json AnimatorAsset::serializeParameterValue(const AnimatorParameterValue& val, AnimatorParameterType type)
    {
        switch (type)
        {
        case AnimatorParameterType::Float:
            return std::holds_alternative<float>(val) ? std::get<float>(val) : 0.0f;
        case AnimatorParameterType::Int:
            return std::holds_alternative<int32_t>(val) ? std::get<int32_t>(val) : 0;
        case AnimatorParameterType::Bool:
        case AnimatorParameterType::Trigger:
            return std::holds_alternative<bool>(val) ? std::get<bool>(val) : false;
        default:
            return 0.0f;
        }
    }

    json AnimatorAsset::serializeParameter(const AnimatorParameter& param)
    {
        json j;
        j["name"] = param.name;
        j["type"] = parameterTypeToString(param.type);
        j["default"] = serializeParameterValue(param.defaultValue, param.type);
        return j;
    }

    json AnimatorAsset::serializeBlendTree(const BlendTreeData& blendTree)
    {
        json j;
        j["type"] = blendTree.type == BlendTreeType::BlendTree1D ? "1D" : "2D";
        j["parameterName"] = blendTree.parameterName;
        if (blendTree.type == BlendTreeType::BlendTree2D)
        {
            j["parameterNameY"] = blendTree.parameterNameY;
        }

        json entriesJson = json::array();
        for (const auto& entry : blendTree.entries)
        {
            json entryJ;
            entryJ["animationRef"] = entry.animationRef.toHexString();
            if (blendTree.type == BlendTreeType::BlendTree1D)
            {
                entryJ["threshold"] = entry.threshold;
            }
            else
            {
                entryJ["position"] = json::array({entry.position.x, entry.position.y});
            }
            entriesJson.push_back(entryJ);
        }
        j["entries"] = entriesJson;
        return j;
    }

    json AnimatorAsset::serializeState(const AnimatorState& state)
    {
        json j;
        j["id"] = state.id;
        j["name"] = state.name;
        j["animationRef"] = state.animationRef.toHexString();
        j["playbackSpeed"] = state.playbackSpeed;
        j["loop"] = state.loop;
        j["position"] = json::array({state.position.x, state.position.y});

        if (!state.events.empty())
        {
            json eventsJson = json::array();
            for (const auto& event : state.events)
            {
                json eventJ;
                eventJ["name"] = event.name;
                eventJ["normalizedTime"] = event.normalizedTime;
                if (!event.payload.empty())
                {
                    eventJ["payload"] = event.payload;
                }
                eventsJson.push_back(eventJ);
            }
            j["events"] = eventsJson;
        }

        if (state.blendTree.has_value())
        {
            j["blendTree"] = serializeBlendTree(state.blendTree.value());
        }

        return j;
    }

    json AnimatorAsset::serializeCondition(const TransitionCondition& condition)
    {
        json j;
        j["parameter"] = condition.parameterName;
        j["operator"] = comparisonOperatorToString(condition.op);

        std::visit([&j](auto&& arg)
        {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, float>)
                j["value"] = arg;
            else if constexpr (std::is_same_v<T, int32_t>)
                j["value"] = arg;
            else if constexpr (std::is_same_v<T, bool>)
                j["value"] = arg;
        }, condition.value);

        return j;
    }

    json AnimatorAsset::serializeTransition(const AnimatorTransition& transition)
    {
        json j;
        j["id"] = transition.id;
        j["sourceState"] = transition.sourceStateId;
        j["targetState"] = transition.targetStateId;
        j["blendDuration"] = transition.blendDuration;
        j["hasExitTime"] = transition.hasExitTime;
        j["exitTime"] = transition.exitTime;
        j["priority"] = transition.priority;

        json conditionsJson = json::array();
        for (const auto& condition : transition.conditions)
        {
            conditionsJson.push_back(serializeCondition(condition));
        }
        j["conditions"] = conditionsJson;

        return j;
    }

    json AnimatorAsset::serializeGraph(const AnimatorGraph& graph)
    {
        json j;
        j["defaultState"] = graph.defaultStateId;
        j["anyStatePosition"] = json::array({graph.anyStatePosition.x, graph.anyStatePosition.y});
        j["entryPosition"] = json::array({graph.entryPosition.x, graph.entryPosition.y});

        json parametersJson = json::array();
        for (const auto& param : graph.parameters)
        {
            parametersJson.push_back(serializeParameter(param));
        }
        j["parameters"] = parametersJson;

        json statesJson = json::array();
        for (const auto& state : graph.states)
        {
            statesJson.push_back(serializeState(state));
        }
        j["states"] = statesJson;

        json transitionsJson = json::array();
        for (const auto& transition : graph.transitions)
        {
            transitionsJson.push_back(serializeTransition(transition));
        }
        j["transitions"] = transitionsJson;

        return j;
    }

    json AnimatorAsset::serializeLayer(const AnimationLayerData& layer)
    {
        json j;
        j["name"] = layer.name;
        j["weight"] = layer.weight;
        j["blendMode"] = layerBlendModeToString(layer.blendMode);
        j["sourceMode"] = layerSourceModeToString(layer.sourceMode);

        if (!layer.boneMaskName.empty())
        {
            j["boneMask"] = layer.boneMaskName;
        }

        if (layer.sourceMode == LayerSourceMode::DirectClip)
        {
            j["directClipRef"] = layer.directClipRef.toHexString();
            j["directClipLoop"] = layer.directClipLoop;
            j["directClipSpeed"] = layer.directClipSpeed;
        }

        if (layer.blendMode == LayerBlendMode::Additive)
        {
            j["additiveRefPose"] = additiveReferencePoseToString(layer.additiveRefPose);
            j["additiveRefFrame"] = layer.additiveRefFrame;
        }

        if (layer.sourceMode == LayerSourceMode::StateMachine)
        {
            j["graph"] = serializeGraph(layer.graph);
        }

        return j;
    }

    json AnimatorAsset::serializeBoneMask(const BoneMaskDefinition& mask)
    {
        json j;
        j["name"] = mask.name;
        j["bones"] = json::array();
        for (const auto& boneName : mask.includedBoneNames)
        {
            j["bones"].push_back(boneName);
        }
        return j;
    }

    json AnimatorAsset::buildAnimatorJson(const AnimatorData& animator)
    {
        json j;
        j["version"] = ANIMATOR_FORMAT_VERSION;
        j["name"] = animator.name;

        if (!animator.boneMasks.empty())
        {
            json masksJson = json::array();
            for (const auto& mask : animator.boneMasks)
                masksJson.push_back(serializeBoneMask(mask));
            j["boneMasks"] = masksJson;
        }

        if (!animator.layers.empty())
        {
            json layersJson = json::array();
            for (const auto& layer : animator.layers)
                layersJson.push_back(serializeLayer(layer));
            j["layers"] = layersJson;
        }
        else
        {
            // Single-graph format (v1.0 backward compat) - merge graph keys into root
            json graphJson = serializeGraph(animator.graph);
            for (auto& [key, value] : graphJson.items())
                j[key] = value;
        }

        return j;
    }

    bool AnimatorAsset::save(std::string_view path, const AnimatorData& animator)
    {
        json j = buildAnimatorJson(animator);

        try
        {
            std::filesystem::path filePath(path);
            std::filesystem::create_directories(filePath.parent_path());

            std::ofstream file(filePath);
            if (!file.is_open())
            {
                vfLogError("Failed to create animator file: {}", path);
                return false;
            }

            file << j.dump(4);
            vfLogInfo("Saved animator: {} to {}", animator.name, path);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to save animator file {}: {}", path, e.what());
            return false;
        }
    }
}
