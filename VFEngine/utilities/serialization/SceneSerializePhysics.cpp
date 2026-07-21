#include "SceneSerialization.hpp"
#include "AssetRefSerializationHelper.hpp"
#include "JsonConverters.hpp"
#include "../components/Components.hpp"

#include <algorithm>

namespace serialization
{
    namespace
    {
        // VK-1513. Absent in scenes saved before the voice cap, so the struct default
        // (128 == neutral) applies — no migration needed. Clamped because the field is a
        // uint8_t and a hand-edited scene must not wrap around.
        void readVoicePriority(const json& j, uint8_t& priority)
        {
            if (auto it = j.find("priority"); it != j.end() && it->is_number_integer())
                priority = static_cast<uint8_t>(std::clamp(it->get<int>(), 0, 255));
        }

        // VK-1521. Absent in scenes saved before fade-in, so the struct default (0 == no
        // fade) applies and playback stays byte-identical — no migration needed. Negatives
        // are clamped away rather than left to the ramp's isRampable() guard: a hand-edited
        // scene should round-trip as the 0 the editor would show, not as a value that only
        // behaves like 0.
        void readAudioFadeIn(const json& j, float& fadeInMs)
        {
            if (auto it = j.find("fadeInMs"); it != j.end() && it->is_number())
                fadeInMs = std::max(0.0f, it->get<float>());
        }

        // VK-1520. Shared by both audio source components — their variation blocks
        // are identical, so template over the component rather than duplicating.
        //
        // clipVariants is an array of OBJECTS, not of GUID strings: writeAssetRef
        // emits TWO keys (the GUID and a resolvable "<key>Path" sibling that keeps
        // scenes loadable with a cold AssetDatabase), so it cannot target an array
        // element. Same shape as ScriptComponent::scripts below.
        template<typename AudioComp>
        void writeAudioVariation(json& j, const AudioComp& audioSource)
        {
            json variantsArray = json::array();
            for (const auto& ref : audioSource.clipVariants)
            {
                json entryJson;
                writeAssetRef(entryJson, "clipRef", ref);
                variantsArray.push_back(entryJson);
            }
            j["clipVariants"] = variantsArray;
            j["playOrder"] = static_cast<uint8_t>(audioSource.playOrder);
            j["pitchVariation"] = audioSource.pitchVariation;
            j["volumeVariation"] = audioSource.volumeVariation;
        }

        // Absent in scenes saved before variation containers, so the struct defaults
        // apply (Single order + zero jitter == the old fixed-clip playback) — no
        // migration needed.
        template<typename AudioComp>
        void readAudioVariation(const json& j, AudioComp& audioSource)
        {
            audioSource.clipVariants.clear();
            if (auto it = j.find("clipVariants"); it != j.end() && it->is_array())
            {
                for (const auto& entryJson : *it)
                {
                    if (!entryJson.is_object())
                        continue;
                    audioSource.clipVariants.push_back(readAssetRef(entryJson, "clipRef"));
                }
            }
            if (auto it = j.find("playOrder"); it != j.end() && it->is_number_integer())
            {
                // Clamped rather than cast blind — a hand-edited scene must not land
                // the enum outside its declared range.
                const int order = std::clamp(it->get<int>(), 0,
                                             static_cast<int>(types::AudioPlayOrder::RoundRobin));
                audioSource.playOrder = static_cast<types::AudioPlayOrder>(order);
            }
            if (auto it = j.find("pitchVariation"); it != j.end() && it->is_number())
                audioSource.pitchVariation = it->get<float>();
            if (auto it = j.find("volumeVariation"); it != j.end() && it->is_number())
                audioSource.volumeVariation = it->get<float>();
        }

        // VK-1520 runtime state, reset alongside activeHandle/isPlaying: a saved
        // scene must not remember which variant happened to play last.
        template<typename AudioComp>
        void resetAudioVariationRuntime(AudioComp& audioSource)
        {
            audioSource.lastVariant = types::AUDIO_VARIANT_NONE;
            audioSource.playCount = 0;
        }
    }

    json SceneSerialization::serializeAudioSource2D(const components::AudioSource2DComponent& audioSource)
    {
        json j;
        writeAssetRef(j, "audioRef", audioSource.audioRef);
        j["volume"] = audioSource.volume;
        j["pitch"] = audioSource.pitch;
        j["loop"] = audioSource.loop;
        j["busName"] = audioSource.busName;
        j["fadeInMs"] = audioSource.fadeInMs;
        writeAudioVariation(j, audioSource);
        return j;
    }

    void SceneSerialization::deserializeAudioSource2D(const json& j, components::AudioSource2DComponent& audioSource)
    {
        audioSource.audioRef = readAssetRef(j, "audioRef", "audioFilePath");
        if (auto it = j.find("volume"); it != j.end() && it->is_number())
        {
            audioSource.volume = it->get<float>();
        }
        if (auto it = j.find("pitch"); it != j.end() && it->is_number())
        {
            audioSource.pitch = it->get<float>();
        }
        if (auto it = j.find("loop"); it != j.end() && it->is_boolean())
        {
            audioSource.loop = it->get<bool>();
        }
        if (auto it = j.find("busName"); it != j.end() && it->is_string())
        {
            audioSource.busName = it->get<std::string>();
        }
        readAudioFadeIn(j, audioSource.fadeInMs);
        readAudioVariation(j, audioSource);
        // Reset runtime state
        audioSource.activeHandle = 0;
        audioSource.isPlaying = false;
        resetAudioVariationRuntime(audioSource);
    }

    json SceneSerialization::serializeAudioSource3D(const components::AudioSource3DComponent& audioSource)
    {
        json j;
        writeAssetRef(j, "audioRef", audioSource.audioRef);
        j["volume"] = audioSource.volume;
        j["pitch"] = audioSource.pitch;
        j["loop"] = audioSource.loop;
        j["minDistance"] = audioSource.minDistance;
        j["maxDistance"] = audioSource.maxDistance;
        j["showDebugSpheres"] = audioSource.showDebugSpheres;
        j["enableDistanceFilter"] = audioSource.enableDistanceFilter;
        j["filterStartDistance"] = audioSource.filterStartDistance;
        j["filterMaxDistance"] = audioSource.filterMaxDistance;
        j["filterIntensity"] = audioSource.filterIntensity;
        j["enableOcclusion"] = audioSource.enableOcclusion;
        j["occlusionLpf"] = audioSource.occlusionLpf;
        j["occlusionVolume"] = audioSource.occlusionVolume;
        j["occlusionLayerMask"] = audioSource.occlusionLayerMask;
        j["innerConeAngle"] = audioSource.innerConeAngle;
        j["outerConeAngle"] = audioSource.outerConeAngle;
        j["outerConeGain"] = audioSource.outerConeGain;
        j["showDebugCone"] = audioSource.showDebugCone;
        j["busName"] = audioSource.busName;
        j["priority"] = audioSource.priority;
        j["fadeInMs"] = audioSource.fadeInMs;
        writeAudioVariation(j, audioSource);
        return j;
    }

    namespace
    {
        void deserializeAudio3DBasicFields(const json& j, components::AudioSource3DComponent& audioSource)
        {
            if (auto it = j.find("volume"); it != j.end() && it->is_number())
                audioSource.volume = it->get<float>();
            if (auto it = j.find("pitch"); it != j.end() && it->is_number())
                audioSource.pitch = it->get<float>();
            if (auto it = j.find("loop"); it != j.end() && it->is_boolean())
                audioSource.loop = it->get<bool>();
            if (auto it = j.find("minDistance"); it != j.end() && it->is_number())
                audioSource.minDistance = it->get<float>();
            if (auto it = j.find("maxDistance"); it != j.end() && it->is_number())
                audioSource.maxDistance = it->get<float>();
            if (auto it = j.find("showDebugSpheres"); it != j.end() && it->is_boolean())
                audioSource.showDebugSpheres = it->get<bool>();
        }

        void deserializeAudio3DFilterFields(const json& j, components::AudioSource3DComponent& audioSource)
        {
            if (auto it = j.find("enableDistanceFilter"); it != j.end() && it->is_boolean())
                audioSource.enableDistanceFilter = it->get<bool>();
            if (auto it = j.find("filterStartDistance"); it != j.end() && it->is_number())
                audioSource.filterStartDistance = it->get<float>();
            if (auto it = j.find("filterMaxDistance"); it != j.end() && it->is_number())
                audioSource.filterMaxDistance = it->get<float>();
            if (auto it = j.find("filterIntensity"); it != j.end() && it->is_number())
                audioSource.filterIntensity = it->get<float>();
            // VK-1518. Absent in scenes saved before geometry occlusion, so the struct
            // defaults apply (enableOcclusion = false) — no migration needed.
            if (auto it = j.find("enableOcclusion"); it != j.end() && it->is_boolean())
                audioSource.enableOcclusion = it->get<bool>();
            if (auto it = j.find("occlusionLpf"); it != j.end() && it->is_number())
                audioSource.occlusionLpf = it->get<float>();
            if (auto it = j.find("occlusionVolume"); it != j.end() && it->is_number())
                audioSource.occlusionVolume = it->get<float>();
            if (auto it = j.find("occlusionLayerMask"); it != j.end() && it->is_number_unsigned())
                audioSource.occlusionLayerMask = static_cast<uint16_t>(
                    std::min(it->get<uint64_t>(), static_cast<uint64_t>(0xFFFF)));
            if (auto it = j.find("innerConeAngle"); it != j.end() && it->is_number())
                audioSource.innerConeAngle = it->get<float>();
            if (auto it = j.find("outerConeAngle"); it != j.end() && it->is_number())
                audioSource.outerConeAngle = it->get<float>();
            if (auto it = j.find("outerConeGain"); it != j.end() && it->is_number())
                audioSource.outerConeGain = it->get<float>();
            if (auto it = j.find("showDebugCone"); it != j.end() && it->is_boolean())
                audioSource.showDebugCone = it->get<bool>();
            if (auto it = j.find("busName"); it != j.end() && it->is_string())
                audioSource.busName = it->get<std::string>();
            readVoicePriority(j, audioSource.priority);
            readAudioFadeIn(j, audioSource.fadeInMs);
        }
    } // anonymous namespace

    void SceneSerialization::deserializeAudioSource3D(const json& j, components::AudioSource3DComponent& audioSource)
    {
        audioSource.audioRef = readAssetRef(j, "audioRef", "audioFilePath");
        deserializeAudio3DBasicFields(j, audioSource);
        deserializeAudio3DFilterFields(j, audioSource);
        readAudioVariation(j, audioSource);
        // Reset runtime state
        audioSource.activeHandle = 0;
        audioSource.isPlaying = false;
        resetAudioVariationRuntime(audioSource);
    }

    json SceneSerialization::serializeReverbZone(const components::ReverbZoneComponent& zone)
    {
        json j;
        j["shape"] = static_cast<int>(zone.shape);
        j["radius"] = zone.radius;
        j["halfExtents"] = json::array({zone.halfExtents.x, zone.halfExtents.y, zone.halfExtents.z});
        j["presetName"] = zone.presetName;
        j["priority"] = zone.priority;
        j["falloffDistance"] = zone.falloffDistance;
        j["wetLevel"] = zone.wetLevel;
        j["showDebugVolume"] = zone.showDebugVolume;
        return j;
    }

    void SceneSerialization::deserializeReverbZone(const json& j, components::ReverbZoneComponent& zone)
    {
        if (auto it = j.find("shape"); it != j.end() && it->is_number())
        {
            zone.shape = static_cast<components::ReverbZoneShape>(it->get<int>());
        }
        if (auto it = j.find("radius"); it != j.end() && it->is_number())
        {
            zone.radius = it->get<float>();
        }
        if (j.contains("halfExtents") && j["halfExtents"].is_array() && j["halfExtents"].size() == 3)
        {
            zone.halfExtents.x = j["halfExtents"][0].get<float>();
            zone.halfExtents.y = j["halfExtents"][1].get<float>();
            zone.halfExtents.z = j["halfExtents"][2].get<float>();
        }
        if (auto it = j.find("presetName"); it != j.end() && it->is_string())
        {
            zone.presetName = it->get<std::string>();
        }
        if (auto it = j.find("priority"); it != j.end() && it->is_number())
        {
            zone.priority = it->get<int>();
        }
        if (auto it = j.find("falloffDistance"); it != j.end() && it->is_number())
        {
            zone.falloffDistance = it->get<float>();
        }
        if (auto it = j.find("wetLevel"); it != j.end() && it->is_number())
        {
            zone.wetLevel = it->get<float>();
        }
        if (auto it = j.find("showDebugVolume"); it != j.end() && it->is_boolean())
        {
            zone.showDebugVolume = it->get<bool>();
        }
        // Reset runtime state
        zone.isListenerInside = false;
        zone.currentBlendWeight = 0.0f;
    }

    json SceneSerialization::serializeFogVolume(const components::FogVolumeComponent& fog)
    {
        json j;
        j["shape"] = static_cast<int>(fog.shape);
        j["halfExtents"] = json::array({fog.halfExtents.x, fog.halfExtents.y, fog.halfExtents.z});
        j["density"] = fog.density;
        j["albedo"] = json::array({fog.albedo.x, fog.albedo.y, fog.albedo.z});
        j["emission"] = json::array({fog.emission.x, fog.emission.y, fog.emission.z});
        j["edgeFalloff"] = fog.edgeFalloff;
        j["blendMode"] = static_cast<int>(fog.blendMode);
        j["showGizmo"] = fog.showGizmo;
        return j;
    }

    void SceneSerialization::deserializeFogVolume(const json& j, components::FogVolumeComponent& fog)
    {
        if (auto it = j.find("shape"); it != j.end() && it->is_number())
        {
            int val = std::clamp(it->get<int>(), 0, 2);
            fog.shape = static_cast<components::FogVolumeShape>(val);
        }
        if (j.contains("halfExtents") && j["halfExtents"].is_array() && j["halfExtents"].size() == 3)
        {
            fog.halfExtents.x = j["halfExtents"][0].get<float>();
            fog.halfExtents.y = j["halfExtents"][1].get<float>();
            fog.halfExtents.z = j["halfExtents"][2].get<float>();
        }
        if (auto it = j.find("density"); it != j.end() && it->is_number())
        {
            fog.density = it->get<float>();
        }
        if (j.contains("albedo") && j["albedo"].is_array() && j["albedo"].size() == 3)
        {
            fog.albedo.x = j["albedo"][0].get<float>();
            fog.albedo.y = j["albedo"][1].get<float>();
            fog.albedo.z = j["albedo"][2].get<float>();
        }
        if (j.contains("emission") && j["emission"].is_array() && j["emission"].size() == 3)
        {
            fog.emission.x = j["emission"][0].get<float>();
            fog.emission.y = j["emission"][1].get<float>();
            fog.emission.z = j["emission"][2].get<float>();
        }
        if (auto it = j.find("edgeFalloff"); it != j.end() && it->is_number())
        {
            fog.edgeFalloff = it->get<float>();
        }
        if (auto it = j.find("blendMode"); it != j.end() && it->is_number())
        {
            int val = std::clamp(it->get<int>(), 0, 1);
            fog.blendMode = static_cast<components::FogVolumeBlendMode>(val);
        }
        if (auto it = j.find("showGizmo"); it != j.end() && it->is_boolean())
        {
            fog.showGizmo = it->get<bool>();
        }
    }

    // VK-1577 — reflection probe. `dirty` is deliberately NOT serialized: a freshly loaded scene has
    // no baked cubemaps in memory (probes are baked at runtime, there is no on-disk cubemap format),
    // so every probe must come back from disk needing a bake. The component's default (dirty = true)
    // already says that, and writing the field would let a saved `false` suppress the load-time bake.
    json SceneSerialization::serializeReflectionProbe(const components::ReflectionProbeComponent& probe)
    {
        json j;
        j["shape"] = static_cast<int>(probe.shape);
        j["halfExtents"] = json::array({probe.halfExtents.x, probe.halfExtents.y, probe.halfExtents.z});
        j["blendDistance"] = probe.blendDistance;
        j["intensity"] = probe.intensity;
        j["nearPlane"] = probe.nearPlane;
        j["farPlane"] = probe.farPlane;
        j["priority"] = probe.priority;
        j["captureShadows"] = probe.captureShadows;
        j["showGizmo"] = probe.showGizmo;
        return j;
    }

    void SceneSerialization::deserializeReflectionProbe(const json& j, components::ReflectionProbeComponent& probe)
    {
        if (auto it = j.find("shape"); it != j.end() && it->is_number())
        {
            int val = std::clamp(it->get<int>(), 0, 1);
            probe.shape = static_cast<components::ReflectionProbeShape>(val);
        }
        if (j.contains("halfExtents") && j["halfExtents"].is_array() && j["halfExtents"].size() == 3)
        {
            probe.halfExtents.x = j["halfExtents"][0].get<float>();
            probe.halfExtents.y = j["halfExtents"][1].get<float>();
            probe.halfExtents.z = j["halfExtents"][2].get<float>();
        }
        if (auto it = j.find("blendDistance"); it != j.end() && it->is_number())
        {
            probe.blendDistance = it->get<float>();
        }
        if (auto it = j.find("intensity"); it != j.end() && it->is_number())
        {
            probe.intensity = it->get<float>();
        }
        if (auto it = j.find("nearPlane"); it != j.end() && it->is_number())
        {
            probe.nearPlane = it->get<float>();
        }
        if (auto it = j.find("farPlane"); it != j.end() && it->is_number())
        {
            probe.farPlane = it->get<float>();
        }
        if (auto it = j.find("priority"); it != j.end() && it->is_number())
        {
            probe.priority = it->get<int>();
        }
        if (auto it = j.find("captureShadows"); it != j.end() && it->is_boolean())
        {
            probe.captureShadows = it->get<bool>();
        }
        if (auto it = j.find("showGizmo"); it != j.end() && it->is_boolean())
        {
            probe.showGizmo = it->get<bool>();
        }
        // Always needs a (re)bake after load — see the note on serializeReflectionProbe.
        probe.dirty = true;
    }

    json SceneSerialization::serializeWeatherZone(const components::WeatherZoneComponent& zone)
    {
        json j;
        j["shape"] = static_cast<int>(zone.shape);
        j["radius"] = zone.radius;
        j["halfExtents"] = json::array({zone.halfExtents.x, zone.halfExtents.y, zone.halfExtents.z});
        j["priority"] = zone.priority;
        j["falloffDistance"] = zone.falloffDistance;
        j["active"] = zone.active;
        j["showDebugVolume"] = zone.showDebugVolume;

        nlohmann::json weatherJson;
        weather::to_json(weatherJson, zone.overrideState);
        j["overrideState"] = weatherJson;

        return j;
    }

    void SceneSerialization::deserializeWeatherZone(const json& j, components::WeatherZoneComponent& zone)
    {
        if (auto it = j.find("shape"); it != j.end() && it->is_number())
            zone.shape = static_cast<components::WeatherZoneShape>(std::clamp(it->get<int>(), 0, 1));
        if (auto it = j.find("radius"); it != j.end() && it->is_number())
            zone.radius = it->get<float>();
        if (j.contains("halfExtents") && j["halfExtents"].is_array() && j["halfExtents"].size() == 3)
        {
            zone.halfExtents.x = j["halfExtents"][0].get<float>();
            zone.halfExtents.y = j["halfExtents"][1].get<float>();
            zone.halfExtents.z = j["halfExtents"][2].get<float>();
        }
        if (auto it = j.find("priority"); it != j.end() && it->is_number())
            zone.priority = it->get<int>();
        if (auto it = j.find("falloffDistance"); it != j.end() && it->is_number())
            zone.falloffDistance = it->get<float>();
        if (auto it = j.find("active"); it != j.end() && it->is_boolean())
            zone.active = it->get<bool>();
        if (auto it = j.find("showDebugVolume"); it != j.end() && it->is_boolean())
            zone.showDebugVolume = it->get<bool>();
        if (j.contains("overrideState") && j["overrideState"].is_object())
            weather::from_json(j["overrideState"], zone.overrideState);

        // Reset runtime state
        zone.isCameraInside = false;
        zone.currentBlendWeight = 0.0f;
    }

    json SceneSerialization::serializeScript(const components::ScriptComponent& script)
    {
        json j;
        json scriptsArray = json::array();
        for (const auto& entry : script.scripts)
        {
            json entryJson;
            writeAssetRef(entryJson, "scriptRef", entry.scriptRef);
            entryJson["enabled"] = entry.enabled;
            // inputPriority previously never round-tripped, so an authored execution order was
            // silently reset to 0 on every load. VK-1536 adds the tick-governor fields beside it and
            // fixes that at the same time.
            entryJson["inputPriority"] = entry.inputPriority;
            entryJson["updateInterval"] = entry.updateInterval;
            entryJson["tickSignificance"] = entry.tickSignificance;
            entryJson["pinFullRate"] = entry.pinFullRate;
            scriptsArray.push_back(entryJson);
        }
        j["scripts"] = scriptsArray;
        return j;
    }

    void SceneSerialization::deserializeScript(const json& j, components::ScriptComponent& script)
    {
        script.scripts.clear();
        if (j.contains("scripts") && j["scripts"].is_array())
        {
            for (const auto& entryJson : j["scripts"])
            {
                components::ScriptEntry entry;
                entry.scriptRef = readAssetRef(entryJson, "scriptRef", "scriptPath");
                entry.scriptPath = entry.scriptRef.resolve();
                if (entryJson.contains("enabled") && entryJson["enabled"].is_boolean())
                {
                    entry.enabled = entryJson["enabled"].get<bool>();
                }
                // Tolerant defaults: a scene saved before VK-1536 has none of these keys and reads
                // back as "every frame, unthrottled" — i.e. exactly its current behavior.
                entry.inputPriority = entryJson.value("inputPriority", 0);
                entry.updateInterval = entryJson.value("updateInterval", 0.0f);
                entry.tickSignificance = entryJson.value("tickSignificance", 1.0f);
                entry.pinFullRate = entryJson.value("pinFullRate", false);
                // Reset runtime state
                entry.started = false;
                entry.instanceId = 0;
                entry.tickAccumulator = 0.0f;
                entry.tickFirstDone = false;
                script.scripts.push_back(entry);
            }
        }
    }

    std::string SceneSerialization::rigidBodyTypeToString(components::RigidBodyType type)
    {
        switch (type)
        {
        case components::RigidBodyType::Static: return "static";
        case components::RigidBodyType::Kinematic: return "kinematic";
        default: return "dynamic";
        }
    }

    components::RigidBodyType SceneSerialization::stringToRigidBodyType(const std::string& str)
    {
        if (str == "static") return components::RigidBodyType::Static;
        if (str == "kinematic") return components::RigidBodyType::Kinematic;
        return components::RigidBodyType::Dynamic;
    }

    std::string SceneSerialization::colliderShapeToString(components::ColliderShape shape)
    {
        switch (shape)
        {
        case components::ColliderShape::Sphere: return "sphere";
        case components::ColliderShape::Capsule: return "capsule";
        case components::ColliderShape::ConvexMesh: return "convexMesh";
        case components::ColliderShape::TriangleMesh: return "triangleMesh";
        default: return "box";
        }
    }

    components::ColliderShape SceneSerialization::stringToColliderShape(const std::string& str)
    {
        if (str == "sphere") return components::ColliderShape::Sphere;
        if (str == "capsule") return components::ColliderShape::Capsule;
        if (str == "convexMesh") return components::ColliderShape::ConvexMesh;
        if (str == "triangleMesh") return components::ColliderShape::TriangleMesh;
        return components::ColliderShape::Box;
    }

    json SceneSerialization::serializeCollider(const components::ColliderComponent& collider)
    {
        json j;
        j["shape"] = colliderShapeToString(collider.shape);
        j["size"] = json::array({collider.size.x, collider.size.y, collider.size.z});
        j["height"] = collider.height;
        j["offset"] = json::array({collider.offset.x, collider.offset.y, collider.offset.z});
        writeAssetRef(j, "meshRef", collider.meshRef);
        j["isTrigger"] = collider.isTrigger;
        j["collisionLayer"] = collider.collisionLayer;
        j["friction"] = collider.friction;
        j["restitution"] = collider.restitution;
        j["submeshIndex"] = collider.submeshIndex;
        return j;
    }

    void SceneSerialization::deserializeCollider(const json& j, components::ColliderComponent& collider)
    {
        if (auto it = j.find("shape"); it != j.end() && it->is_string())
        {
            collider.shape = stringToColliderShape(it->get<std::string>());
        }
        if (auto it = j.find("size"); it != j.end() && it->is_array() && it->size() >= 3)
        {
            collider.size = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
        }
        if (auto it = j.find("height"); it != j.end() && it->is_number())
        {
            collider.height = it->get<float>();
        }
        if (auto it = j.find("offset"); it != j.end() && it->is_array() && it->size() >= 3)
        {
            collider.offset = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
        }
        collider.meshRef = readAssetRef(j, "meshRef", "meshPath");
        if (auto it = j.find("isTrigger"); it != j.end() && it->is_boolean())
        {
            collider.isTrigger = it->get<bool>();
        }
        if (auto it = j.find("collisionLayer"); it != j.end() && it->is_number_unsigned())
        {
            uint8_t layer = it->get<uint8_t>();
            collider.collisionLayer = layer < 16 ? layer : 1;
        }
        if (auto it = j.find("friction"); it != j.end() && it->is_number())
        {
            collider.friction = it->get<float>();
        }
        if (auto it = j.find("restitution"); it != j.end() && it->is_number())
        {
            collider.restitution = it->get<float>();
        }
        if (auto it = j.find("submeshIndex"); it != j.end() && it->is_number_integer())
        {
            collider.submeshIndex = it->get<int32_t>();
        }
    }

    json SceneSerialization::serializeRigidBody(const components::RigidBodyComponent& rigidBody)
    {
        json j;
        j["type"] = rigidBodyTypeToString(rigidBody.type);
        j["mass"] = rigidBody.mass;
        j["linearDamping"] = rigidBody.linearDamping;
        j["angularDamping"] = rigidBody.angularDamping;
        j["freezePositionX"] = rigidBody.freezePositionX;
        j["freezePositionY"] = rigidBody.freezePositionY;
        j["freezePositionZ"] = rigidBody.freezePositionZ;
        j["freezeRotationX"] = rigidBody.freezeRotationX;
        j["freezeRotationY"] = rigidBody.freezeRotationY;
        j["freezeRotationZ"] = rigidBody.freezeRotationZ;
        return j;
    }

    void SceneSerialization::deserializeRigidBody(const json& j, components::RigidBodyComponent& rigidBody)
    {
        if (auto it = j.find("type"); it != j.end() && it->is_string())
        {
            rigidBody.type = stringToRigidBodyType(it->get<std::string>());
        }
        if (auto it = j.find("mass"); it != j.end() && it->is_number())
        {
            rigidBody.mass = it->get<float>();
        }
        if (auto it = j.find("linearDamping"); it != j.end() && it->is_number())
        {
            rigidBody.linearDamping = it->get<float>();
        }
        if (auto it = j.find("angularDamping"); it != j.end() && it->is_number())
        {
            rigidBody.angularDamping = it->get<float>();
        }
        if (auto it = j.find("freezePositionX"); it != j.end() && it->is_boolean())
        {
            rigidBody.freezePositionX = it->get<bool>();
        }
        if (auto it = j.find("freezePositionY"); it != j.end() && it->is_boolean())
        {
            rigidBody.freezePositionY = it->get<bool>();
        }
        if (auto it = j.find("freezePositionZ"); it != j.end() && it->is_boolean())
        {
            rigidBody.freezePositionZ = it->get<bool>();
        }
        if (auto it = j.find("freezeRotationX"); it != j.end() && it->is_boolean())
        {
            rigidBody.freezeRotationX = it->get<bool>();
        }
        if (auto it = j.find("freezeRotationY"); it != j.end() && it->is_boolean())
        {
            rigidBody.freezeRotationY = it->get<bool>();
        }
        if (auto it = j.find("freezeRotationZ"); it != j.end() && it->is_boolean())
        {
            rigidBody.freezeRotationZ = it->get<bool>();
        }
    }

    json SceneSerialization::serializeBuoyancy(const components::BuoyancyComponent& buoyancy)
    {
        json j;
        j["sampleMode"] = buoyancy.sampleMode == components::BuoyancyComponent::SampleMode::Custom
                              ? "custom"
                              : "auto";
        j["buoyancyScale"] = buoyancy.buoyancyScale;
        j["angularDrag"] = buoyancy.angularDrag;
        if (buoyancy.customPointCount > 0)
        {
            json points = json::array();
            for (uint32_t i = 0; i < buoyancy.customPointCount && i < 8; ++i)
            {
                points.push_back({buoyancy.customPoints[i].x,
                                  buoyancy.customPoints[i].y,
                                  buoyancy.customPoints[i].z});
            }
            j["customPoints"] = std::move(points);
        }
        return j;
    }

    void SceneSerialization::deserializeBuoyancy(const json& j, components::BuoyancyComponent& buoyancy)
    {
        if (auto it = j.find("sampleMode"); it != j.end() && it->is_string())
        {
            buoyancy.sampleMode = it->get<std::string>() == "custom"
                                      ? components::BuoyancyComponent::SampleMode::Custom
                                      : components::BuoyancyComponent::SampleMode::Auto;
        }
        if (auto it = j.find("buoyancyScale"); it != j.end() && it->is_number())
        {
            buoyancy.buoyancyScale = it->get<float>();
        }
        if (auto it = j.find("angularDrag"); it != j.end() && it->is_number())
        {
            buoyancy.angularDrag = it->get<float>();
        }
        if (auto it = j.find("customPoints"); it != j.end() && it->is_array())
        {
            buoyancy.customPointCount = 0;
            for (const auto& point : *it)
            {
                if (buoyancy.customPointCount >= 8)
                    break;
                if (!point.is_array() || point.size() < 3)
                    continue;
                buoyancy.customPoints[buoyancy.customPointCount++] =
                    glm::vec3(point[0].get<float>(), point[1].get<float>(), point[2].get<float>());
            }
        }
    }

    json SceneSerialization::serializeDestructible(const components::DestructibleComponent& d)
    {
        json j;
        j["maxHealth"] = d.maxHealth;
        j["destructionThreshold"] = d.destructionThreshold;
        writeAssetRef(j, "fractureAssetRef", d.fractureAssetRef);
        j["fragmentCount"] = d.fragmentCount;
        j["mode"] = static_cast<int>(d.mode);
        j["damageFilter"] = static_cast<int>(d.damageFilter);
        j["fragmentMassTotal"] = d.fragmentMassTotal;
        j["fragmentLifetime"] = d.fragmentLifetime;
        j["propagationRadius"] = d.propagationRadius;
        j["propagationDamage"] = d.propagationDamage;
        writeAssetRef(j, "onDamageVFX", d.onDamageVFX);
        writeAssetRef(j, "onDestroyVFX", d.onDestroyVFX);
        writeAssetRef(j, "onDamageAudio", d.onDamageAudio);
        writeAssetRef(j, "onDestroyAudio", d.onDestroyAudio);
        writeAssetRef(j, "fragmentCollisionAudio", d.fragmentCollisionAudio);
        writeAssetRef(j, "damageDecalAlbedo", d.damageDecalAlbedo);
        writeAssetRef(j, "damageDecalNormal", d.damageDecalNormal);
        j["decalHalfExtents"] = d.decalHalfExtents;
        return j;
    }

    void SceneSerialization::deserializeDestructible(const json& j, components::DestructibleComponent& d)
    {
        if (auto it = j.find("maxHealth"); it != j.end() && it->is_number())
            d.maxHealth = it->get<float>();
        d.currentHealth = d.maxHealth;
        if (auto it = j.find("destructionThreshold"); it != j.end() && it->is_number())
            d.destructionThreshold = it->get<float>();
        d.fractureAssetRef = readAssetRef(j, "fractureAssetRef");
        if (auto it = j.find("fragmentCount"); it != j.end() && it->is_number())
            d.fragmentCount = it->get<uint32_t>();
        if (auto it = j.find("mode"); it != j.end() && it->is_number())
            d.mode = static_cast<components::DestructionMode>(it->get<int>());
        if (auto it = j.find("damageFilter"); it != j.end() && it->is_number())
            d.damageFilter = static_cast<components::DamageType>(it->get<int>());
        if (auto it = j.find("fragmentMassTotal"); it != j.end() && it->is_number())
            d.fragmentMassTotal = it->get<float>();
        if (auto it = j.find("fragmentLifetime"); it != j.end() && it->is_number())
            d.fragmentLifetime = it->get<float>();
        if (auto it = j.find("propagationRadius"); it != j.end() && it->is_number())
            d.propagationRadius = it->get<float>();
        if (auto it = j.find("propagationDamage"); it != j.end() && it->is_number())
            d.propagationDamage = it->get<float>();
        d.onDamageVFX = readAssetRef(j, "onDamageVFX");
        d.onDestroyVFX = readAssetRef(j, "onDestroyVFX");
        d.onDamageAudio = readAssetRef(j, "onDamageAudio");
        d.onDestroyAudio = readAssetRef(j, "onDestroyAudio");
        d.fragmentCollisionAudio = readAssetRef(j, "fragmentCollisionAudio");
        d.damageDecalAlbedo = readAssetRef(j, "damageDecalAlbedo");
        d.damageDecalNormal = readAssetRef(j, "damageDecalNormal");
        if (auto it = j.find("decalHalfExtents"); it != j.end() && it->is_number())
            d.decalHalfExtents = it->get<float>();
        d.isDestroyed = false;
    }
}
