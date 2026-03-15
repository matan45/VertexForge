#include "AnimatorAsset.hpp"
#include "../print/Log.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <format>

namespace animator
{
    using json = nlohmann::json;
    namespace fs = std::filesystem;

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

    json AnimatorAsset::serializeParameter(const AnimatorParameter& param)
    {
        json j;
        j["name"] = param.name;
        j["type"] = parameterTypeToString(param.type);
        j["default"] = serializeParameterValue(param.defaultValue, param.type);
        return j;
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
            entryJ["animationPath"] = entry.animationPath;
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
                entry.animationPath = entryJson.value("animationPath", "");
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

    json AnimatorAsset::serializeState(const AnimatorState& state)
    {
        json j;
        j["id"] = state.id;
        j["name"] = state.name;
        j["animationPath"] = state.animationPath;
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

    AnimatorState AnimatorAsset::deserializeState(const json& j)
    {
        AnimatorState state;
        state.id = j.value("id", 0u);
        state.name = j.value("name", "");
        state.animationPath = j.value("animationPath", "");
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

    std::optional<json> AnimatorAsset::readJsonFromFile(std::string_view path)
    {
        fs::path filePath(path);
        if (!fs::exists(filePath))
        {
            vfLogError("Animator file not found: {}", path);
            return std::nullopt;
        }

        std::error_code ec;
        auto fileSize = fs::file_size(filePath, ec);
        if (ec)
        {
            vfLogError("Cannot read animator file size '{}': {}", path, ec.message());
            return std::nullopt;
        }

        constexpr size_t MAX_ANIMATOR_FILE_SIZE = 10 * 1024 * 1024;
        if (fileSize > MAX_ANIMATOR_FILE_SIZE)
        {
            vfLogError("Animator file '{}' is too large ({} bytes, max {} bytes)",
                       path, fileSize, MAX_ANIMATOR_FILE_SIZE);
            return std::nullopt;
        }

        std::ifstream file(filePath);
        if (!file.is_open())
        {
            vfLogError("Failed to open animator file: {}", path);
            return std::nullopt;
        }

        json j;
        try { file >> j; }
        catch (const json::parse_error& e)
        {
            vfLogError("Animator file '{}' contains invalid JSON at byte {}: {}",
                       path, e.byte, e.what());
            return std::nullopt;
        }

        if (!j.is_object())
        {
            vfLogError("Animator file '{}' must contain a JSON object at root level", path);
            return std::nullopt;
        }
        return j;
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

    void AnimatorAsset::parseGraph(const json& j, AnimatorGraph& graph, const WarningLogger& logWarning)
    {
        parseAnimatorParameters(j, graph, logWarning);
        parseAnimatorStates(j, graph, logWarning);
        parseAnimatorTransitions(j, graph, logWarning);
        parseGraphLayout(j, graph, logWarning);
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
            j["directClipPath"] = layer.directClipPath;
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

    AnimationLayerData AnimatorAsset::deserializeLayer(const json& j, const WarningLogger& logWarning)
    {
        AnimationLayerData layer;
        layer.name = j.value("name", "Layer");
        layer.weight = j.value("weight", 1.0f);
        layer.blendMode = stringToLayerBlendMode(j.value("blendMode", "Override"));
        layer.sourceMode = stringToLayerSourceMode(j.value("sourceMode", "StateMachine"));
        layer.boneMaskName = j.value("boneMask", "");
        layer.directClipPath = j.value("directClipPath", "");
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

    AnimatorData AnimatorAsset::parseAnimatorData(const json& j, const WarningLogger& logWarning)
    {
        AnimatorData animator;
        animator.version = j.value("version", "1.0");
        animator.name = j.value("name", "Unnamed Animator");

        if (animator.name.empty())
        {
            animator.name = "Unnamed Animator";
            logWarning("Animator has empty name, using default");
        }

        // Parse bone masks
        if (j.contains("boneMasks") && j["boneMasks"].is_array())
        {
            for (const auto& maskJson : j["boneMasks"])
            {
                if (maskJson.is_object())
                {
                    animator.boneMasks.push_back(deserializeBoneMask(maskJson));
                }
            }
        }

        // Parse layers if present (v1.1+), otherwise use single graph (v1.0 backward compat)
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

            // For backward compat, populate the root graph from the base layer
            if (!animator.layers.empty())
            {
                animator.graph = animator.layers[0].graph;
            }
        }
        else
        {
            // v1.0 format: single graph at root level
            parseAnimatorParameters(j, animator.graph, logWarning);
            parseAnimatorStates(j, animator.graph, logWarning);
            parseAnimatorTransitions(j, animator.graph, logWarning);
            parseGraphLayout(j, animator.graph, logWarning);
        }

        return animator;
    }

    std::optional<AnimatorData> AnimatorAsset::load(std::string_view path)
    {
        auto jsonOpt = readJsonFromFile(path);
        if (!jsonOpt.has_value())
        {
            return std::nullopt;
        }

        const json& j = jsonOpt.value();
        int warningCount = 0;
        constexpr int MAX_WARNINGS = 20;

        auto logWarningLimited = [&](const std::string& msg)
        {
            if (warningCount < MAX_WARNINGS)
            {
                vfLogWarning("{}", msg);
                warningCount++;
                if (warningCount == MAX_WARNINGS)
                {
                    vfLogWarning("(suppressing further warnings for this file)");
                }
            }
        };

        try
        {
            AnimatorData animator = parseAnimatorData(j, logWarningLimited);

            if (warningCount > 0)
            {
                vfLogWarning("Loaded animator '{}' with {} warning(s)", animator.name, warningCount);
            }

            vfLogInfo("Loaded animator '{}': {} states, {} transitions, {} parameters, {} layers",
                      animator.name, animator.graph.states.size(),
                      animator.graph.transitions.size(), animator.graph.parameters.size(),
                      animator.layers.size());

            return animator;
        }
        catch (const json::exception& e)
        {
            vfLogError("Failed to parse animator file '{}': {}", path, e.what());
            return std::nullopt;
        }
        catch (const std::exception& e)
        {
            vfLogError("Unexpected error loading animator '{}': {}", path, e.what());
            return std::nullopt;
        }
    }

    json AnimatorAsset::buildAnimatorJson(const AnimatorData& animator)
    {
        json j;

        j["version"] = ANIMATOR_FORMAT_VERSION;
        j["name"] = animator.name;

        // Serialize bone masks
        if (!animator.boneMasks.empty())
        {
            json masksJson = json::array();
            for (const auto& mask : animator.boneMasks)
            {
                masksJson.push_back(serializeBoneMask(mask));
            }
            j["boneMasks"] = masksJson;
        }

        if (!animator.layers.empty())
        {
            // Multi-layer format (v1.1)
            json layersJson = json::array();
            for (const auto& layer : animator.layers)
            {
                layersJson.push_back(serializeLayer(layer));
            }
            j["layers"] = layersJson;
        }
        else
        {
            // Single-graph format (v1.0 backward compat)
            j["defaultState"] = animator.graph.defaultStateId;
            j["anyStatePosition"] = json::array({animator.graph.anyStatePosition.x, animator.graph.anyStatePosition.y});
            j["entryPosition"] = json::array({animator.graph.entryPosition.x, animator.graph.entryPosition.y});

            json parametersJson = json::array();
            for (const auto& param : animator.graph.parameters)
            {
                parametersJson.push_back(serializeParameter(param));
            }
            j["parameters"] = parametersJson;

            json statesJson = json::array();
            for (const auto& state : animator.graph.states)
            {
                statesJson.push_back(serializeState(state));
            }
            j["states"] = statesJson;

            json transitionsJson = json::array();
            for (const auto& transition : animator.graph.transitions)
            {
                transitionsJson.push_back(serializeTransition(transition));
            }
            j["transitions"] = transitionsJson;
        }

        return j;
    }

    bool AnimatorAsset::save(std::string_view path, const AnimatorData& animator)
    {
        json j = buildAnimatorJson(animator);

        try
        {
            fs::path filePath(path);
            fs::create_directories(filePath.parent_path());

            std::ofstream file(filePath);
            if (!file.is_open())
            {
                vfLogError("Failed to create animator file: {}", path);
                return false;
            }

            file << j.dump(4); // Pretty print with 4-space indent
            vfLogInfo("Saved animator: {} to {}", animator.name, path);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to save animator file {}: {}", path, e.what());
            return false;
        }
    }

    AnimatorData AnimatorAsset::createDefault(const std::string& name)
    {
        AnimatorData animator;
        animator.name = name;
        animator.version = ANIMATOR_FORMAT_VERSION;

        animator.graph.entryPosition = glm::vec2(50.0f, 100.0f);
        animator.graph.anyStatePosition = glm::vec2(50.0f, 250.0f);

        AnimatorState idleState;
        idleState.id = animator.graph.nextStateId++;
        idleState.name = "Idle";
        idleState.loop = true;
        idleState.position = glm::vec2(250.0f, 100.0f);
        animator.graph.states.push_back(std::move(idleState));

        animator.graph.defaultStateId = 1;

        return animator;
    }
}
