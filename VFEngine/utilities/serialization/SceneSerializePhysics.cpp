#include "SceneSerialization.hpp"
#include "AssetRefSerializationHelper.hpp"
#include "JsonConverters.hpp"
#include "../components/Components.hpp"

namespace serialization
{

    json SceneSerialization::serializeAudioSource2D(const components::AudioSource2DComponent& audioSource)
    {
        json j;
        writeAssetRef(j, "audioRef", audioSource.audioRef);
        j["volume"] = audioSource.volume;
        j["pitch"] = audioSource.pitch;
        j["loop"] = audioSource.loop;
        j["busName"] = audioSource.busName;
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
        // Reset runtime state
        audioSource.activeHandle = 0;
        audioSource.isPlaying = false;
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
        j["innerConeAngle"] = audioSource.innerConeAngle;
        j["outerConeAngle"] = audioSource.outerConeAngle;
        j["outerConeGain"] = audioSource.outerConeGain;
        j["showDebugCone"] = audioSource.showDebugCone;
        j["busName"] = audioSource.busName;
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
        }
    } // anonymous namespace

    void SceneSerialization::deserializeAudioSource3D(const json& j, components::AudioSource3DComponent& audioSource)
    {
        audioSource.audioRef = readAssetRef(j, "audioRef", "audioFilePath");
        deserializeAudio3DBasicFields(j, audioSource);
        deserializeAudio3DFilterFields(j, audioSource);
        // Reset runtime state
        audioSource.activeHandle = 0;
        audioSource.isPlaying = false;
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
                if (entryJson.contains("enabled") && entryJson["enabled"].is_boolean())
                {
                    entry.enabled = entryJson["enabled"].get<bool>();
                }
                // Reset runtime state
                entry.started = false;
                entry.instanceId = 0;
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

    json SceneSerialization::serializeDestructible(const components::DestructibleComponent& d)
    {
        json j;
        j["maxHealth"] = d.maxHealth;
        j["destructionThreshold"] = d.destructionThreshold;
        writeAssetRef(j, "fractureAssetRef", d.fractureAssetRef);
        j["mode"] = static_cast<int>(d.mode);
        j["damageFilter"] = static_cast<int>(d.damageFilter);
        j["fragmentMassTotal"] = d.fragmentMassTotal;
        j["fragmentLifetime"] = d.fragmentLifetime;
        j["propagationRadius"] = d.propagationRadius;
        j["propagationDamage"] = d.propagationDamage;
        j["materialType"] = static_cast<int>(d.materialType);
        writeAssetRef(j, "onDamageVFX", d.onDamageVFX);
        writeAssetRef(j, "onDestroyVFX", d.onDestroyVFX);
        writeAssetRef(j, "onDamageAudio", d.onDamageAudio);
        writeAssetRef(j, "onDestroyAudio", d.onDestroyAudio);
        writeAssetRef(j, "fragmentCollisionAudio", d.fragmentCollisionAudio);
        writeAssetRef(j, "damageDecalAlbedo", d.damageDecalAlbedo);
        writeAssetRef(j, "damageDecalNormal", d.damageDecalNormal);
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
        if (auto it = j.find("materialType"); it != j.end() && it->is_number())
            d.materialType = static_cast<components::MaterialType>(it->get<int>());
        d.onDamageVFX = readAssetRef(j, "onDamageVFX");
        d.onDestroyVFX = readAssetRef(j, "onDestroyVFX");
        d.onDamageAudio = readAssetRef(j, "onDamageAudio");
        d.onDestroyAudio = readAssetRef(j, "onDestroyAudio");
        d.fragmentCollisionAudio = readAssetRef(j, "fragmentCollisionAudio");
        d.damageDecalAlbedo = readAssetRef(j, "damageDecalAlbedo");
        d.damageDecalNormal = readAssetRef(j, "damageDecalNormal");
        d.isDestroyed = false;
    }
}
