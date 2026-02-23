#include "SceneSerialization.hpp"
#include "JsonConverters.hpp"
#include "../components/Components.hpp"
#include "../print/EditorLogger.hpp"
#include <algorithm>

namespace serialization
{
    namespace
    {
        std::string audioDistanceModelToStr(types::AudioDistanceModel model)
        {
            switch (model)
            {
            case types::AudioDistanceModel::None: return "none";
            case types::AudioDistanceModel::InverseDistance: return "inverseDistance";
            case types::AudioDistanceModel::InverseDistanceClamped: return "inverseDistanceClamped";
            case types::AudioDistanceModel::LinearDistance: return "linearDistance";
            case types::AudioDistanceModel::LinearDistanceClamped: return "linearDistanceClamped";
            case types::AudioDistanceModel::ExponentDistance: return "exponentDistance";
            case types::AudioDistanceModel::ExponentDistanceClamped: return "exponentDistanceClamped";
            default: return "inverseDistanceClamped";
            }
        }

        types::AudioDistanceModel strToAudioDistanceModel(const std::string& str)
        {
            if (str == "none") return types::AudioDistanceModel::None;
            if (str == "inverseDistance") return types::AudioDistanceModel::InverseDistance;
            if (str == "inverseDistanceClamped") return types::AudioDistanceModel::InverseDistanceClamped;
            if (str == "linearDistance") return types::AudioDistanceModel::LinearDistance;
            if (str == "linearDistanceClamped") return types::AudioDistanceModel::LinearDistanceClamped;
            if (str == "exponentDistance") return types::AudioDistanceModel::ExponentDistance;
            if (str == "exponentDistanceClamped") return types::AudioDistanceModel::ExponentDistanceClamped;
            return types::AudioDistanceModel::InverseDistanceClamped;
        }

        // ---- Deserialize physics sub-helpers ----

        void deserializePhysicsSimulation(const json& j, types::PhysicsSettings& settings)
        {
            if (!j.contains("simulation"))
                return;
            const auto& sim = j["simulation"];
            if (sim.contains("fixedTimestep"))
                settings.fixedTimestep = sim["fixedTimestep"].get<double>();
            if (sim.contains("maxAccumulator"))
                settings.maxAccumulator = sim["maxAccumulator"].get<double>();
            if (sim.contains("maxStepsPerFrame"))
                settings.maxStepsPerFrame = sim["maxStepsPerFrame"].get<int>();
        }

        void deserializePhysicsSleepThresholds(const json& j, types::PhysicsSettings& settings)
        {
            if (!j.contains("sleepThresholds"))
                return;
            const auto& sleep = j["sleepThresholds"];
            if (sleep.contains("linearVelocity"))
                settings.linearSleepThreshold = sleep["linearVelocity"].get<float>();
            if (sleep.contains("angularVelocity"))
                settings.angularSleepThreshold = sleep["angularVelocity"].get<float>();
            if (sleep.contains("timeToSleep"))
                settings.timeToSleep = sleep["timeToSleep"].get<float>();
        }

        void deserializePhysicsCollisionLayers(const json& j, types::PhysicsSettings& settings)
        {
            if (!j.contains("collisionLayers") || !j["collisionLayers"].is_array())
                return;
            settings.layers.clear();
            for (const auto& layerJson : j["collisionLayers"])
            {
                types::CollisionLayer layer;
                if (layerJson.contains("index"))
                    layer.index = layerJson["index"].get<uint8_t>();
                if (layerJson.contains("name"))
                    layer.name = layerJson["name"].get<std::string>();
                if (layerJson.contains("builtIn"))
                    layer.isBuiltIn = layerJson["builtIn"].get<bool>();
                settings.layers.push_back(layer);
            }
        }

        void deserializePhysicsCollisionMatrix(const json& j, types::PhysicsSettings& settings)
        {
            if (!j.contains("collisionMatrix") || !j["collisionMatrix"].is_array())
                return;
            for (auto& row : settings.collisionMatrix)
            {
                row.reset();
            }

            const auto& matrix = j["collisionMatrix"];
            for (size_t i = 0; i < matrix.size() && i < types::PhysicsSettings::MAX_LAYERS; ++i)
            {
                if (matrix[i].is_array())
                {
                    for (size_t k = 0; k < matrix[i].size() && k < types::PhysicsSettings::MAX_LAYERS; ++k)
                    {
                        if (matrix[i][k].is_boolean() && matrix[i][k].get<bool>())
                        {
                            settings.collisionMatrix[i].set(k);
                        }
                    }
                }
            }
        }
    } // anonymous namespace

    // ---- String/Enum conversion class methods ----

    std::string SceneSerialization::audioDistanceModelToString(types::AudioDistanceModel model)
    {
        return audioDistanceModelToStr(model);
    }

    types::AudioDistanceModel SceneSerialization::stringToAudioDistanceModel(const std::string& str)
    {
        return strToAudioDistanceModel(str);
    }

    std::string SceneSerialization::shadowQualityToString(types::ShadowQuality quality)
    {
        switch (quality)
        {
        case types::ShadowQuality::Off: return "off";
        case types::ShadowQuality::Low: return "low";
        case types::ShadowQuality::Medium: return "medium";
        case types::ShadowQuality::High: return "high";
        case types::ShadowQuality::Ultra: return "ultra";
        default: return "high";
        }
    }

    types::ShadowQuality SceneSerialization::stringToShadowQuality(const std::string& str)
    {
        if (str == "off") return types::ShadowQuality::Off;
        if (str == "low") return types::ShadowQuality::Low;
        if (str == "medium") return types::ShadowQuality::Medium;
        if (str == "high") return types::ShadowQuality::High;
        if (str == "ultra") return types::ShadowQuality::Ultra;
        return types::ShadowQuality::High;
    }

    std::string SceneSerialization::cascadeSplitModeToString(types::CascadeSplitMode mode)
    {
        switch (mode)
        {
        case types::CascadeSplitMode::Linear: return "linear";
        case types::CascadeSplitMode::Logarithmic: return "logarithmic";
        case types::CascadeSplitMode::Practical: return "practical";
        default: return "practical";
        }
    }

    types::CascadeSplitMode SceneSerialization::stringToCascadeSplitMode(const std::string& str)
    {
        if (str == "linear") return types::CascadeSplitMode::Linear;
        if (str == "logarithmic") return types::CascadeSplitMode::Logarithmic;
        if (str == "practical") return types::CascadeSplitMode::Practical;
        return types::CascadeSplitMode::Practical;
    }

    std::string SceneSerialization::toneMappingModeToString(postprocess::ToneMappingMode mode)
    {
        switch (mode)
        {
        case postprocess::ToneMappingMode::ACES: return "aces";
        case postprocess::ToneMappingMode::Reinhard: return "reinhard";
        case postprocess::ToneMappingMode::Uncharted2: return "uncharted2";
        case postprocess::ToneMappingMode::Linear: return "linear";
        default: return "aces";
        }
    }

    postprocess::ToneMappingMode SceneSerialization::stringToToneMappingMode(const std::string& str)
    {
        if (str == "aces") return postprocess::ToneMappingMode::ACES;
        if (str == "reinhard") return postprocess::ToneMappingMode::Reinhard;
        if (str == "uncharted2") return postprocess::ToneMappingMode::Uncharted2;
        if (str == "linear") return postprocess::ToneMappingMode::Linear;
        return postprocess::ToneMappingMode::ACES;
    }

    std::string SceneSerialization::fxaaQualityToString(postprocess::FXAAQuality quality)
    {
        switch (quality)
        {
        case postprocess::FXAAQuality::Low: return "low";
        case postprocess::FXAAQuality::Medium: return "medium";
        case postprocess::FXAAQuality::High: return "high";
        default: return "medium";
        }
    }

    postprocess::FXAAQuality SceneSerialization::stringToFXAAQuality(const std::string& str)
    {
        if (str == "low") return postprocess::FXAAQuality::Low;
        if (str == "medium") return postprocess::FXAAQuality::Medium;
        if (str == "high") return postprocess::FXAAQuality::High;
        return postprocess::FXAAQuality::Medium;
    }

    // ---- Physics Settings ----

    json SceneSerialization::serializePhysicsSettings(const types::PhysicsSettings& settings)
    {
        json j;

        j["gravity"] = json::array({settings.gravity.x, settings.gravity.y, settings.gravity.z});
        j["gravityScale"] = settings.gravityScale;

        j["simulation"] = {
            {"fixedTimestep", settings.fixedTimestep},
            {"maxAccumulator", settings.maxAccumulator},
            {"maxStepsPerFrame", settings.maxStepsPerFrame}
        };

        j["sleepThresholds"] = {
            {"linearVelocity", settings.linearSleepThreshold},
            {"angularVelocity", settings.angularSleepThreshold},
            {"timeToSleep", settings.timeToSleep}
        };

        j["vfxCollision"] = {
            {"maxSceneColliders", settings.maxVFXSceneColliders}
        };

        j["collisionLayers"] = json::array();
        for (const auto& layer : settings.layers)
        {
            j["collisionLayers"].push_back({
                {"index", layer.index},
                {"name", layer.name},
                {"builtIn", layer.isBuiltIn}
            });
        }

        j["collisionMatrix"] = json::array();
        for (size_t i = 0; i < settings.layers.size(); ++i)
        {
            json row = json::array();
            for (size_t k = 0; k < settings.layers.size(); ++k)
            {
                row.push_back(settings.collisionMatrix[i].test(k));
            }
            j["collisionMatrix"].push_back(row);
        }

        return j;
    }

    void SceneSerialization::deserializePhysicsSettings(const json& j, types::PhysicsSettings& settings)
    {
        if (j.contains("gravity") && j["gravity"].is_array() && j["gravity"].size() == 3)
        {
            settings.gravity.x = j["gravity"][0].get<float>();
            settings.gravity.y = j["gravity"][1].get<float>();
            settings.gravity.z = j["gravity"][2].get<float>();
        }
        if (j.contains("gravityScale"))
        {
            settings.gravityScale = j["gravityScale"].get<float>();
        }

        deserializePhysicsSimulation(j, settings);
        deserializePhysicsSleepThresholds(j, settings);
        deserializePhysicsCollisionLayers(j, settings);
        deserializePhysicsCollisionMatrix(j, settings);

        if (j.contains("vfxCollision") && j["vfxCollision"].is_object())
        {
            const auto& vfx = j["vfxCollision"];
            if (vfx.contains("maxSceneColliders"))
                settings.maxVFXSceneColliders = std::clamp(
                    vfx["maxSceneColliders"].get<uint32_t>(), 1u, 128u);
        }
    }

    // ---- Audio Settings ----

    json SceneSerialization::serializeAudioSettings(const types::AudioSettings& settings)
    {
        json j;

        j["listener"] = {
            {"masterVolume", settings.masterVolume},
            {"dopplerFactor", settings.dopplerFactor},
            {"speedOfSound", settings.speedOfSound}
        };

        j["distanceModel"] = {
            {"model", audioDistanceModelToString(settings.distanceModel)},
            {"defaultRolloffFactor", settings.defaultRolloffFactor}
        };

        return j;
    }

    void SceneSerialization::deserializeAudioSettings(const json& j, types::AudioSettings& settings)
    {
        if (j.contains("listener") && j["listener"].is_object())
        {
            const auto& listener = j["listener"];
            if (listener.contains("masterVolume") && listener["masterVolume"].is_number())
                settings.masterVolume = listener["masterVolume"].get<float>();
            if (listener.contains("dopplerFactor") && listener["dopplerFactor"].is_number())
                settings.dopplerFactor = listener["dopplerFactor"].get<float>();
            if (listener.contains("speedOfSound") && listener["speedOfSound"].is_number())
                settings.speedOfSound = listener["speedOfSound"].get<float>();
        }

        if (j.contains("distanceModel") && j["distanceModel"].is_object())
        {
            const auto& dm = j["distanceModel"];
            if (dm.contains("model") && dm["model"].is_string())
                settings.distanceModel = stringToAudioDistanceModel(dm["model"].get<std::string>());
            if (dm.contains("defaultRolloffFactor") && dm["defaultRolloffFactor"].is_number())
                settings.defaultRolloffFactor = dm["defaultRolloffFactor"].get<float>();
        }
    }
}
