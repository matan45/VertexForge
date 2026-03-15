#include "SceneSerialization.hpp"
#include "JsonConverters.hpp"
#include "../components/Components.hpp"
#include "../types/AudioEffectTypes.hpp"
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
        json serializeEffectParams(const types::BusEffectConfig& effect)
        {
            json j;
            j["id"] = effect.id;
            j["type"] = types::audioEffectTypeToString(effect.type);
            j["enabled"] = effect.enabled;
            j["wetDryMix"] = effect.wetDryMix;

            std::visit([&j](const auto& params)
            {
                using T = std::decay_t<decltype(params)>;
                if constexpr (std::is_same_v<T, types::ReverbParams>)
                {
                    json p;
                    p["density"] = params.density;
                    p["diffusion"] = params.diffusion;
                    p["gain"] = params.gain;
                    p["gainHF"] = params.gainHF;
                    p["gainLF"] = params.gainLF;
                    p["decayTime"] = params.decayTime;
                    p["decayHFRatio"] = params.decayHFRatio;
                    p["decayLFRatio"] = params.decayLFRatio;
                    p["reflectionsGain"] = params.reflectionsGain;
                    p["reflectionsDelay"] = params.reflectionsDelay;
                    p["lateReverbGain"] = params.lateReverbGain;
                    p["lateReverbDelay"] = params.lateReverbDelay;
                    p["echoTime"] = params.echoTime;
                    p["echoDepth"] = params.echoDepth;
                    p["modulationTime"] = params.modulationTime;
                    p["modulationDepth"] = params.modulationDepth;
                    p["airAbsorptionGainHF"] = params.airAbsorptionGainHF;
                    p["hfReference"] = params.hfReference;
                    p["lfReference"] = params.lfReference;
                    p["roomRolloffFactor"] = params.roomRolloffFactor;
                    p["decayHFLimit"] = params.decayHFLimit;
                    if (!params.presetName.empty())
                        p["presetName"] = params.presetName;
                    j["params"] = p;
                }
                else if constexpr (std::is_same_v<T, types::EQParams>)
                {
                    json p;
                    p["lowGain"] = params.lowGain;
                    p["lowCutoff"] = params.lowCutoff;
                    p["mid1Gain"] = params.mid1Gain;
                    p["mid1Center"] = params.mid1Center;
                    p["mid1Width"] = params.mid1Width;
                    p["mid2Gain"] = params.mid2Gain;
                    p["mid2Center"] = params.mid2Center;
                    p["mid2Width"] = params.mid2Width;
                    p["highGain"] = params.highGain;
                    p["highCutoff"] = params.highCutoff;
                    j["params"] = p;
                }
                else if constexpr (std::is_same_v<T, types::CompressorParams>)
                {
                    json p;
                    p["onOff"] = params.onOff;
                    j["params"] = p;
                }
                else if constexpr (std::is_same_v<T, types::EchoParams>)
                {
                    json p;
                    p["delay"] = params.delay;
                    p["lrDelay"] = params.lrDelay;
                    p["damping"] = params.damping;
                    p["feedback"] = params.feedback;
                    p["spread"] = params.spread;
                    j["params"] = p;
                }
                else if constexpr (std::is_same_v<T, types::ChorusParams>)
                {
                    json p;
                    p["waveform"] = params.waveform;
                    p["phase"] = params.phase;
                    p["rate"] = params.rate;
                    p["depth"] = params.depth;
                    p["feedback"] = params.feedback;
                    p["delay"] = params.delay;
                    j["params"] = p;
                }
            }, effect.params);

            return j;
        }

        types::BusEffectConfig deserializeEffectParams(const json& j)
        {
            types::BusEffectConfig config;
            if (j.contains("id")) config.id = j["id"].get<uint32_t>();
            if (j.contains("enabled")) config.enabled = j["enabled"].get<bool>();
            if (j.contains("wetDryMix")) config.wetDryMix = j["wetDryMix"].get<float>();

            std::string typeStr = j.value("type", "Reverb");
            config.type = types::stringToAudioEffectType(typeStr);

            if (j.contains("params"))
            {
                const auto& p = j["params"];
                switch (config.type)
                {
                case types::AudioEffectType::Reverb:
                {
                    types::ReverbParams params;
                    if (p.contains("density")) params.density = p["density"].get<float>();
                    if (p.contains("diffusion")) params.diffusion = p["diffusion"].get<float>();
                    if (p.contains("gain")) params.gain = p["gain"].get<float>();
                    if (p.contains("gainHF")) params.gainHF = p["gainHF"].get<float>();
                    if (p.contains("gainLF")) params.gainLF = p["gainLF"].get<float>();
                    if (p.contains("decayTime")) params.decayTime = p["decayTime"].get<float>();
                    if (p.contains("decayHFRatio")) params.decayHFRatio = p["decayHFRatio"].get<float>();
                    if (p.contains("decayLFRatio")) params.decayLFRatio = p["decayLFRatio"].get<float>();
                    if (p.contains("reflectionsGain")) params.reflectionsGain = p["reflectionsGain"].get<float>();
                    if (p.contains("reflectionsDelay")) params.reflectionsDelay = p["reflectionsDelay"].get<float>();
                    if (p.contains("lateReverbGain")) params.lateReverbGain = p["lateReverbGain"].get<float>();
                    if (p.contains("lateReverbDelay")) params.lateReverbDelay = p["lateReverbDelay"].get<float>();
                    if (p.contains("echoTime")) params.echoTime = p["echoTime"].get<float>();
                    if (p.contains("echoDepth")) params.echoDepth = p["echoDepth"].get<float>();
                    if (p.contains("modulationTime")) params.modulationTime = p["modulationTime"].get<float>();
                    if (p.contains("modulationDepth")) params.modulationDepth = p["modulationDepth"].get<float>();
                    if (p.contains("airAbsorptionGainHF")) params.airAbsorptionGainHF = p["airAbsorptionGainHF"].get<float>();
                    if (p.contains("hfReference")) params.hfReference = p["hfReference"].get<float>();
                    if (p.contains("lfReference")) params.lfReference = p["lfReference"].get<float>();
                    if (p.contains("roomRolloffFactor")) params.roomRolloffFactor = p["roomRolloffFactor"].get<float>();
                    if (p.contains("decayHFLimit")) params.decayHFLimit = p["decayHFLimit"].get<int>();
                    if (p.contains("presetName")) params.presetName = p["presetName"].get<std::string>();
                    config.params = params;
                    break;
                }
                case types::AudioEffectType::EQ:
                {
                    types::EQParams params;
                    if (p.contains("lowGain")) params.lowGain = p["lowGain"].get<float>();
                    if (p.contains("lowCutoff")) params.lowCutoff = p["lowCutoff"].get<float>();
                    if (p.contains("mid1Gain")) params.mid1Gain = p["mid1Gain"].get<float>();
                    if (p.contains("mid1Center")) params.mid1Center = p["mid1Center"].get<float>();
                    if (p.contains("mid1Width")) params.mid1Width = p["mid1Width"].get<float>();
                    if (p.contains("mid2Gain")) params.mid2Gain = p["mid2Gain"].get<float>();
                    if (p.contains("mid2Center")) params.mid2Center = p["mid2Center"].get<float>();
                    if (p.contains("mid2Width")) params.mid2Width = p["mid2Width"].get<float>();
                    if (p.contains("highGain")) params.highGain = p["highGain"].get<float>();
                    if (p.contains("highCutoff")) params.highCutoff = p["highCutoff"].get<float>();
                    config.params = params;
                    break;
                }
                case types::AudioEffectType::Compressor:
                {
                    types::CompressorParams params;
                    if (p.contains("onOff")) params.onOff = p["onOff"].get<bool>();
                    config.params = params;
                    break;
                }
                case types::AudioEffectType::Echo:
                {
                    types::EchoParams params;
                    if (p.contains("delay")) params.delay = p["delay"].get<float>();
                    if (p.contains("lrDelay")) params.lrDelay = p["lrDelay"].get<float>();
                    if (p.contains("damping")) params.damping = p["damping"].get<float>();
                    if (p.contains("feedback")) params.feedback = p["feedback"].get<float>();
                    if (p.contains("spread")) params.spread = p["spread"].get<float>();
                    config.params = params;
                    break;
                }
                case types::AudioEffectType::Chorus:
                {
                    types::ChorusParams params;
                    if (p.contains("waveform")) params.waveform = p["waveform"].get<int>();
                    if (p.contains("phase")) params.phase = p["phase"].get<int>();
                    if (p.contains("rate")) params.rate = p["rate"].get<float>();
                    if (p.contains("depth")) params.depth = p["depth"].get<float>();
                    if (p.contains("feedback")) params.feedback = p["feedback"].get<float>();
                    if (p.contains("delay")) params.delay = p["delay"].get<float>();
                    config.params = params;
                    break;
                }
                }
            }
            else
            {
                config.params = types::BusEffectConfig::createDefault(config.type).params;
            }

            return config;
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
                    vfx["maxSceneColliders"].get<uint32_t>(), 1u, 256u);
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

        j["distanceFilter"] = {
            {"enabled", settings.enableDistanceFilter},
            {"defaultStartDistance", settings.defaultFilterStartDistance},
            {"defaultMaxDistance", settings.defaultFilterMaxDistance},
            {"defaultIntensity", settings.defaultFilterIntensity}
        };

        json busesArray = json::array();
        for (const auto& bus : settings.busDefinitions)
        {
            json busJson;
            busJson["name"] = bus.name;
            busJson["parentName"] = bus.parentName;
            busJson["defaultVolume"] = bus.defaultVolume;

            if (!bus.effects.empty())
            {
                json effectsArray = json::array();
                for (const auto& effect : bus.effects)
                {
                    effectsArray.push_back(serializeEffectParams(effect));
                }
                busJson["effects"] = effectsArray;
            }

            busesArray.push_back(busJson);
        }
        j["buses"] = busesArray;

        json snapshotsArray = json::array();
        for (const auto& snapshot : settings.mixSnapshots)
        {
            json snapJson;
            snapJson["name"] = snapshot.name;
            json volumesObj = json::object();
            for (const auto& [busName, vol] : snapshot.busVolumes)
            {
                volumesObj[busName] = vol;
            }
            snapJson["busVolumes"] = volumesObj;
            json mutesObj = json::object();
            for (const auto& [busName, muted] : snapshot.busMutes)
            {
                mutesObj[busName] = muted;
            }
            snapJson["busMutes"] = mutesObj;
            snapshotsArray.push_back(snapJson);
        }
        j["mixSnapshots"] = snapshotsArray;

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

        if (j.contains("distanceFilter") && j["distanceFilter"].is_object())
        {
            const auto& df = j["distanceFilter"];
            if (df.contains("enabled") && df["enabled"].is_boolean())
                settings.enableDistanceFilter = df["enabled"].get<bool>();
            if (df.contains("defaultStartDistance") && df["defaultStartDistance"].is_number())
                settings.defaultFilterStartDistance = df["defaultStartDistance"].get<float>();
            if (df.contains("defaultMaxDistance") && df["defaultMaxDistance"].is_number())
                settings.defaultFilterMaxDistance = df["defaultMaxDistance"].get<float>();
            if (df.contains("defaultIntensity") && df["defaultIntensity"].is_number())
                settings.defaultFilterIntensity = df["defaultIntensity"].get<float>();
        }

        if (j.contains("buses") && j["buses"].is_array())
        {
            settings.busDefinitions.clear();
            for (const auto& busJson : j["buses"])
            {
                types::AudioBusDefinition bus;
                if (busJson.contains("name") && busJson["name"].is_string())
                    bus.name = busJson["name"].get<std::string>();
                if (busJson.contains("parentName") && busJson["parentName"].is_string())
                    bus.parentName = busJson["parentName"].get<std::string>();
                if (busJson.contains("defaultVolume") && busJson["defaultVolume"].is_number())
                    bus.defaultVolume = busJson["defaultVolume"].get<float>();
                if (busJson.contains("effects") && busJson["effects"].is_array())
                {
                    for (const auto& effectJson : busJson["effects"])
                    {
                        bus.effects.push_back(deserializeEffectParams(effectJson));
                    }
                }
                settings.busDefinitions.push_back(bus);
            }
        }

        if (j.contains("mixSnapshots") && j["mixSnapshots"].is_array())
        {
            settings.mixSnapshots.clear();
            for (const auto& snapJson : j["mixSnapshots"])
            {
                types::AudioMixSnapshotDefinition snapshot;
                if (snapJson.contains("name") && snapJson["name"].is_string())
                    snapshot.name = snapJson["name"].get<std::string>();
                if (snapJson.contains("busVolumes") && snapJson["busVolumes"].is_object())
                {
                    for (auto it = snapJson["busVolumes"].begin(); it != snapJson["busVolumes"].end(); ++it)
                    {
                        if (it.value().is_number())
                            snapshot.busVolumes[it.key()] = it.value().get<float>();
                    }
                }
                if (snapJson.contains("busMutes") && snapJson["busMutes"].is_object())
                {
                    for (auto it = snapJson["busMutes"].begin(); it != snapJson["busMutes"].end(); ++it)
                    {
                        if (it.value().is_boolean())
                            snapshot.busMutes[it.key()] = it.value().get<bool>();
                    }
                }
                settings.mixSnapshots.push_back(snapshot);
            }
        }
    }
}
