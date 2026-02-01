#include "SceneSerialization.hpp"
#include "JsonConverters.hpp"
#include "../scene/SceneGraphSystem.hpp"
#include "../components/Components.hpp"
#include "../print/EditorLogger.hpp"
#include <fstream>
#include <algorithm>

namespace serialization
{
    // Helper to clean null terminators from strings
    static void cleanNullTerminators(std::string& str)
    {
        if (auto pos = str.find('\0'); pos != std::string::npos)
            str.resize(pos);
    }

    json SceneSerialization::serializeTransform(const components::TransformComponent& transform)
    {
        json j;
        j["position"] = json::array({transform.position.x, transform.position.y, transform.position.z});
        j["rotation"] = json::array({transform.rotation.x, transform.rotation.y, transform.rotation.z});
        j["scale"] = json::array({transform.scale.x, transform.scale.y, transform.scale.z});
        return j;
    }

    json SceneSerialization::serializeCamera(const components::CameraComponent& camera)
    {
        json j;
        j["cameraId"] = camera.cameraId;
        j["fieldOfView"] = camera.fieldOfView;
        j["nearPlane"] = camera.nearPlane;
        j["farPlane"] = camera.farPlane;
        j["aspectRatio"] = camera.aspectRatio;
        j["isPerspective"] = camera.isPerspective;
        j["isPrimary"] = camera.isPrimary;
        j["showFrustum"] = camera.showFrustum;
        j["orthoSize"] = camera.orthoSize;
        return j;
    }

    json SceneSerialization::serializeIBL(const components::IBLComponent& ibl)
    {
        json j;
        std::string cleanFileName = ibl.fileName;
        cleanNullTerminators(cleanFileName);
        j["fileName"] = cleanFileName;
        return j;
    }

    json SceneSerialization::serializeMesh(const components::MeshComponent& mesh)
    {
        json j;
        std::string cleanPath = mesh.meshPath;
        cleanNullTerminators(cleanPath);
        j["meshPath"] = cleanPath;
        j["showBoundingBox"] = mesh.showBoundingBox;

        if (!mesh.animatorPath.empty())
        {
            std::string cleanAnimatorPath = mesh.animatorPath;
            cleanNullTerminators(cleanAnimatorPath);
            j["animatorPath"] = cleanAnimatorPath;
        }
        return j;
    }

    json SceneSerialization::serializeEntity(scene::Entity& entity)
    {
        json entityJson;

        entityJson["uuid"] = entity.getUUID().getValue();
        entityJson["name"] = entity.getName();

        if (entity.hasComponent<components::NameComponent>())
        {
            entityJson["isActive"] = entity.getComponent<components::NameComponent>().isActive;
        }

        if (entity.hasComponent<components::TransformComponent>())
        {
            entityJson["transform"] = serializeTransform(entity.getComponent<components::TransformComponent>());
        }

        json componentsJson = json::object();

        if (entity.hasComponent<components::CameraComponent>())
        {
            componentsJson["camera"] = serializeCamera(entity.getComponent<components::CameraComponent>());
        }

        if (entity.hasComponent<components::IBLComponent>())
        {
            componentsJson["ibl"] = serializeIBL(entity.getComponent<components::IBLComponent>());
        }

        if (entity.hasComponent<components::MeshComponent>())
        {
            componentsJson["mesh"] = serializeMesh(entity.getComponent<components::MeshComponent>());
        }

        if (entity.hasComponent<components::MaterialComponent>())
        {
            componentsJson["material"] = serializeMaterial(entity.getComponent<components::MaterialComponent>());
        }

        if (entity.hasComponent<components::BillboardComponent>())
        {
            componentsJson["billboard"] = serializeBillboard(entity.getComponent<components::BillboardComponent>());
        }

        if (entity.hasComponent<components::AudioSource2DComponent>())
        {
            componentsJson["audioSource2D"] = serializeAudioSource2D(
                entity.getComponent<components::AudioSource2DComponent>());
        }

        if (entity.hasComponent<components::AudioSource3DComponent>())
        {
            componentsJson["audioSource3D"] = serializeAudioSource3D(
                entity.getComponent<components::AudioSource3DComponent>());
        }

        if (entity.hasComponent<components::ScriptComponent>())
        {
            componentsJson["script"] = serializeScript(entity.getComponent<components::ScriptComponent>());
        }

        if (entity.hasComponent<components::ColliderComponent>())
        {
            componentsJson["collider"] = serializeCollider(entity.getComponent<components::ColliderComponent>());
        }

        if (entity.hasComponent<components::RigidBodyComponent>())
        {
            componentsJson["rigidBody"] = serializeRigidBody(entity.getComponent<components::RigidBodyComponent>());
        }

        if (entity.hasComponent<components::VFXComponent>())
        {
            componentsJson["vfx"] = serializeVFX(entity.getComponent<components::VFXComponent>());
        }

        if (entity.hasComponent<components::DirectionalLightComponent>())
        {
            componentsJson["directionalLight"] = serializeDirectionalLight(
                entity.getComponent<components::DirectionalLightComponent>());
        }

        if (entity.hasComponent<components::PointLightComponent>())
        {
            componentsJson["pointLight"] = serializePointLight(
                entity.getComponent<components::PointLightComponent>());
        }

        if (entity.hasComponent<components::SpotLightComponent>())
        {
            componentsJson["spotLight"] = serializeSpotLight(
                entity.getComponent<components::SpotLightComponent>());
        }

        if (entity.hasComponent<components::TerrainComponent>())
        {
            componentsJson["terrain"] = serializeTerrain(
                entity.getComponent<components::TerrainComponent>());
        }

        if (entity.hasComponent<components::TerrainTileComponent>())
        {
            componentsJson["terrainTile"] = serializeTerrainTile(
                entity.getComponent<components::TerrainTileComponent>());
        }

        entityJson["components"] = componentsJson;

        json childrenJson = json::array();
        for (auto& child : entity.getChildren())
        {
            childrenJson.push_back(serializeEntity(child));
        }
        entityJson["children"] = childrenJson;

        return entityJson;
    }

    void SceneSerialization::deserializeTransform(const json& j, components::TransformComponent& transform)
    {
        if (auto it = j.find("position"); it != j.end() && it->is_array() && it->size() >= 3)
        {
            transform.position = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
        }
        if (auto it = j.find("rotation"); it != j.end() && it->is_array() && it->size() >= 3)
        {
            transform.rotation = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
        }
        if (auto it = j.find("scale"); it != j.end() && it->is_array() && it->size() >= 3)
        {
            transform.scale = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
        }
        transform.isDirty = true;
    }

    void SceneSerialization::deserializeCamera(const json& j, components::CameraComponent& camera)
    {
        if (auto it = j.find("cameraId"); it != j.end() && it->is_number_unsigned())
            camera.cameraId = it->get<uint32_t>();
        if (auto it = j.find("fieldOfView"); it != j.end() && it->is_number())
            camera.fieldOfView = it->get<float>();
        if (auto it = j.find("nearPlane"); it != j.end() && it->is_number())
            camera.nearPlane = it->get<float>();
        if (auto it = j.find("farPlane"); it != j.end() && it->is_number())
            camera.farPlane = it->get<float>();
        if (auto it = j.find("aspectRatio"); it != j.end() && it->is_number())
            camera.aspectRatio = it->get<float>();
        if (auto it = j.find("isPerspective"); it != j.end() && it->is_boolean())
            camera.isPerspective = it->get<bool>();
        if (auto it = j.find("isPrimary"); it != j.end() && it->is_boolean())
            camera.isPrimary = it->get<bool>();
        if (auto it = j.find("showFrustum"); it != j.end() && it->is_boolean())
            camera.showFrustum = it->get<bool>();
        if (auto it = j.find("orthoSize"); it != j.end() && it->is_number())
            camera.orthoSize = it->get<float>();
        camera.updateProjectionMatrix();
    }

    std::string SceneSerialization::deserializeIBL(const json& j)
    {
        if (auto it = j.find("fileName"); it != j.end() && it->is_string())
        {
            return it->get<std::string>();
        }
        return "";
    }

    void SceneSerialization::deserializeMesh(const json& j, components::MeshComponent& mesh)
    {
        if (auto it = j.find("meshPath"); it != j.end() && it->is_string())
        {
            mesh.meshPath = it->get<std::string>();
        }
        if (auto it = j.find("showBoundingBox"); it != j.end() && it->is_boolean())
        {
            mesh.showBoundingBox = it->get<bool>();
        }
        if (auto it = j.find("animatorPath"); it != j.end() && it->is_string())
        {
            mesh.animatorPath = it->get<std::string>();
        }
    }

    json SceneSerialization::serializeMaterial(const components::MaterialComponent& material)
    {
        json j;

        std::string cleanDefaultPath = material.defaultMaterial;
        cleanNullTerminators(cleanDefaultPath);
        j["defaultMaterial"] = cleanDefaultPath;

        json subMeshMaterialsJson = json::object();
        for (const auto& [submeshName, matPath] : material.subMeshMaterials)
        {
            std::string cleanMatPath = matPath;
            cleanNullTerminators(cleanMatPath);
            subMeshMaterialsJson[submeshName] = cleanMatPath;
        }
        j["subMeshMaterials"] = subMeshMaterialsJson;

        json paramOverridesJson = json::object();
        for (const auto& [paramName, value] : material.parameterOverrides)
        {
            paramOverridesJson[paramName] = value;
        }
        j["parameterOverrides"] = paramOverridesJson;

        return j;
    }

    void SceneSerialization::deserializeMaterial(const json& j, components::MaterialComponent& material)
    {
        if (auto it = j.find("defaultMaterial"); it != j.end() && it->is_string())
        {
            material.defaultMaterial = it->get<std::string>();
        }

        if (auto it = j.find("subMeshMaterials"); it != j.end() && it->is_object())
        {
            material.subMeshMaterials.clear();
            for (auto& [key, value] : it->items())
            {
                if (value.is_string())
                {
                    material.subMeshMaterials[key] = value.get<std::string>();
                }
            }
        }

        if (auto it = j.find("parameterOverrides"); it != j.end() && it->is_object())
        {
            material.parameterOverrides.clear();
            for (auto& [key, value] : it->items())
            {
                if (value.is_number())
                {
                    material.parameterOverrides[key] = value.get<float>();
                }
            }
        }
    }

    std::string SceneSerialization::billboardSizeModeToString(components::BillboardSizeMode mode)
    {
        switch (mode)
        {
        case components::BillboardSizeMode::WorldSpace: return "worldSpace";
        default: return "screenSpace";
        }
    }

    components::BillboardSizeMode SceneSerialization::stringToBillboardSizeMode(const std::string& str)
    {
        if (str == "worldSpace") return components::BillboardSizeMode::WorldSpace;
        return components::BillboardSizeMode::ScreenSpace;
    }

    std::string SceneSerialization::billboardIconTypeToString(components::BillboardIconType type)
    {
        switch (type)
        {
        case components::BillboardIconType::DirectionalLight: return "directionalLight";
        case components::BillboardIconType::PointLight: return "pointLight";
        case components::BillboardIconType::SpotLight: return "spotLight";
        case components::BillboardIconType::Camera: return "camera";
        case components::BillboardIconType::Audio2D: return "audio2D";
        case components::BillboardIconType::Audio3D: return "audio3D";
        case components::BillboardIconType::Particle: return "particle";
        default: return "custom";
        }
    }

    components::BillboardIconType SceneSerialization::stringToBillboardIconType(const std::string& str)
    {
        if (str == "directionalLight") return components::BillboardIconType::DirectionalLight;
        if (str == "pointLight") return components::BillboardIconType::PointLight;
        if (str == "spotLight") return components::BillboardIconType::SpotLight;
        if (str == "camera") return components::BillboardIconType::Camera;
        if (str == "audio2D") return components::BillboardIconType::Audio2D;
        if (str == "audio3D") return components::BillboardIconType::Audio3D;
        if (str == "particle") return components::BillboardIconType::Particle;
        // Legacy support
        if (str == "light") return components::BillboardIconType::PointLight;
        if (str == "audioSource") return components::BillboardIconType::Audio3D;
        return components::BillboardIconType::Custom;
    }

    json SceneSerialization::serializeBillboard(const components::BillboardComponent& billboard)
    {
        json j;
        j["iconType"] = billboardIconTypeToString(billboard.iconType);
        j["atlasIndex"] = billboard.atlasIndex;
        j["sizeMode"] = billboardSizeModeToString(billboard.sizeMode);
        j["size"] = json::array({billboard.size.x, billboard.size.y});
        j["colorTint"] = json::array({
            billboard.colorTint.r, billboard.colorTint.g, billboard.colorTint.b, billboard.colorTint.a
        });
        j["editorOnly"] = billboard.editorOnly;
        j["selectable"] = billboard.selectable;
        return j;
    }

    void SceneSerialization::deserializeBillboard(const json& j, components::BillboardComponent& billboard)
    {
        billboard.iconType = stringToBillboardIconType(j.value("iconType", "custom"));
        billboard.atlasIndex = j.value("atlasIndex", 0u);
        billboard.sizeMode = stringToBillboardSizeMode(j.value("sizeMode", "screenSpace"));
        if (j.contains("size") && j["size"].is_array() && j["size"].size() >= 2)
        {
            billboard.size = glm::vec2(j["size"][0].get<float>(), j["size"][1].get<float>());
        }
        if (j.contains("colorTint") && j["colorTint"].is_array() && j["colorTint"].size() >= 4)
        {
            billboard.colorTint = glm::vec4(
                j["colorTint"][0].get<float>(), j["colorTint"][1].get<float>(),
                j["colorTint"][2].get<float>(), j["colorTint"][3].get<float>()
            );
        }
        billboard.editorOnly = j.value("editorOnly", true);
        billboard.selectable = j.value("selectable", true);
    }

    json SceneSerialization::serializeAudioSource2D(const components::AudioSource2DComponent& audioSource)
    {
        json j;
        std::string cleanPath = audioSource.audioFilePath;
        cleanNullTerminators(cleanPath);
        j["audioFilePath"] = cleanPath;
        j["volume"] = audioSource.volume;
        j["pitch"] = audioSource.pitch;
        j["loop"] = audioSource.loop;
        return j;
    }

    void SceneSerialization::deserializeAudioSource2D(const json& j, components::AudioSource2DComponent& audioSource)
    {
        if (auto it = j.find("audioFilePath"); it != j.end() && it->is_string())
        {
            audioSource.audioFilePath = it->get<std::string>();
        }
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
        // Reset runtime state
        audioSource.activeHandle = 0;
        audioSource.isPlaying = false;
    }

    json SceneSerialization::serializeAudioSource3D(const components::AudioSource3DComponent& audioSource)
    {
        json j;
        std::string cleanPath = audioSource.audioFilePath;
        cleanNullTerminators(cleanPath);
        j["audioFilePath"] = cleanPath;
        j["volume"] = audioSource.volume;
        j["pitch"] = audioSource.pitch;
        j["loop"] = audioSource.loop;
        j["minDistance"] = audioSource.minDistance;
        j["maxDistance"] = audioSource.maxDistance;
        j["showDebugSpheres"] = audioSource.showDebugSpheres;
        return j;
    }

    void SceneSerialization::deserializeAudioSource3D(const json& j, components::AudioSource3DComponent& audioSource)
    {
        if (auto it = j.find("audioFilePath"); it != j.end() && it->is_string())
        {
            audioSource.audioFilePath = it->get<std::string>();
        }
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
        if (auto it = j.find("minDistance"); it != j.end() && it->is_number())
        {
            audioSource.minDistance = it->get<float>();
        }
        if (auto it = j.find("maxDistance"); it != j.end() && it->is_number())
        {
            audioSource.maxDistance = it->get<float>();
        }
        if (auto it = j.find("showDebugSpheres"); it != j.end() && it->is_boolean())
        {
            audioSource.showDebugSpheres = it->get<bool>();
        }
        // Reset runtime state
        audioSource.activeHandle = 0;
        audioSource.isPlaying = false;
    }

    json SceneSerialization::serializeScript(const components::ScriptComponent& script)
    {
        json j;
        json scriptsArray = json::array();
        for (const auto& entry : script.scripts)
        {
            json entryJson;
            std::string cleanPath = entry.scriptPath;
            cleanNullTerminators(cleanPath);
            entryJson["scriptPath"] = cleanPath;
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
                if (entryJson.contains("scriptPath") && entryJson["scriptPath"].is_string())
                {
                    entry.scriptPath = entryJson["scriptPath"].get<std::string>();
                }
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

    json SceneSerialization::serializeCollider(const components::ColliderComponent& collider)
    {
        json j;
        j["shape"] = colliderShapeToString(collider.shape);
        j["size"] = json::array({collider.size.x, collider.size.y, collider.size.z});
        j["height"] = collider.height;
        j["offset"] = json::array({collider.offset.x, collider.offset.y, collider.offset.z});
        std::string cleanPath = collider.meshPath;
        cleanNullTerminators(cleanPath);
        j["meshPath"] = cleanPath;
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
        if (auto it = j.find("meshPath"); it != j.end() && it->is_string())
        {
            collider.meshPath = it->get<std::string>();
        }
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

    json SceneSerialization::serializeVFX(const components::VFXComponent& vfx)
    {
        json j;
        std::string cleanPath = vfx.vfxPath;
        cleanNullTerminators(cleanPath);
        j["vfxPath"] = cleanPath;
        j["autoPlay"] = vfx.autoPlay;
        j["loop"] = vfx.loop;
        return j;
    }

    void SceneSerialization::deserializeVFX(const json& j, components::VFXComponent& vfx)
    {
        if (auto it = j.find("vfxPath"); it != j.end() && it->is_string())
        {
            vfx.vfxPath = it->get<std::string>();
        }
        if (auto it = j.find("autoPlay"); it != j.end() && it->is_boolean())
        {
            vfx.autoPlay = it->get<bool>();
        }
        if (auto it = j.find("loop"); it != j.end() && it->is_boolean())
        {
            vfx.loop = it->get<bool>();
        }
        // Reset runtime state
        vfx.runtimeInstanceId = 0;
        vfx.isPlaying = false;
    }

    json SceneSerialization::serializeDirectionalLight(const components::DirectionalLightComponent& light)
    {
        json j;
        j["color"] = json::array({light.color.r, light.color.g, light.color.b});
        j["intensity"] = light.intensity;
        j["showGizmo"] = light.showGizmo;
        return j;
    }

    void SceneSerialization::deserializeDirectionalLight(const json& j, components::DirectionalLightComponent& light)
    {
        if (auto it = j.find("color"); it != j.end() && it->is_array() && it->size() >= 3)
        {
            light.color = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
        }
        if (auto it = j.find("intensity"); it != j.end() && it->is_number())
        {
            light.intensity = it->get<float>();
        }
        if (auto it = j.find("showGizmo"); it != j.end() && it->is_boolean())
        {
            light.showGizmo = it->get<bool>();
        }
    }

    json SceneSerialization::serializePointLight(const components::PointLightComponent& light)
    {
        json j;
        j["color"] = json::array({light.color.r, light.color.g, light.color.b});
        j["intensity"] = light.intensity;
        j["radius"] = light.radius;
        j["showGizmo"] = light.showGizmo;
        return j;
    }

    void SceneSerialization::deserializePointLight(const json& j, components::PointLightComponent& light)
    {
        if (auto it = j.find("color"); it != j.end() && it->is_array() && it->size() >= 3)
        {
            light.color = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
        }
        if (auto it = j.find("intensity"); it != j.end() && it->is_number())
        {
            light.intensity = it->get<float>();
        }
        if (auto it = j.find("radius"); it != j.end() && it->is_number())
        {
            light.radius = it->get<float>();
        }
        if (auto it = j.find("showGizmo"); it != j.end() && it->is_boolean())
        {
            light.showGizmo = it->get<bool>();
        }
    }

    json SceneSerialization::serializeSpotLight(const components::SpotLightComponent& light)
    {
        json j;
        j["color"] = json::array({light.color.r, light.color.g, light.color.b});
        j["intensity"] = light.intensity;
        j["innerAngle"] = light.innerAngle;
        j["outerAngle"] = light.outerAngle;
        j["range"] = light.range;
        j["showGizmo"] = light.showGizmo;
        return j;
    }

    void SceneSerialization::deserializeSpotLight(const json& j, components::SpotLightComponent& light)
    {
        if (auto it = j.find("color"); it != j.end() && it->is_array() && it->size() >= 3)
        {
            light.color = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
        }
        if (auto it = j.find("intensity"); it != j.end() && it->is_number())
        {
            light.intensity = it->get<float>();
        }
        if (auto it = j.find("innerAngle"); it != j.end() && it->is_number())
        {
            light.innerAngle = it->get<float>();
        }
        if (auto it = j.find("outerAngle"); it != j.end() && it->is_number())
        {
            light.outerAngle = it->get<float>();
        }
        if (auto it = j.find("range"); it != j.end() && it->is_number())
        {
            light.range = it->get<float>();
        }
        if (auto it = j.find("showGizmo"); it != j.end() && it->is_boolean())
        {
            light.showGizmo = it->get<bool>();
        }
    }

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

        if (j.contains("simulation"))
        {
            const auto& sim = j["simulation"];
            if (sim.contains("fixedTimestep"))
                settings.fixedTimestep = sim["fixedTimestep"].get<double>();
            if (sim.contains("maxAccumulator"))
                settings.maxAccumulator = sim["maxAccumulator"].get<double>();
            if (sim.contains("maxStepsPerFrame"))
                settings.maxStepsPerFrame = sim["maxStepsPerFrame"].get<int>();
        }

        if (j.contains("sleepThresholds"))
        {
            const auto& sleep = j["sleepThresholds"];
            if (sleep.contains("linearVelocity"))
                settings.linearSleepThreshold = sleep["linearVelocity"].get<float>();
            if (sleep.contains("angularVelocity"))
                settings.angularSleepThreshold = sleep["angularVelocity"].get<float>();
            if (sleep.contains("timeToSleep"))
                settings.timeToSleep = sleep["timeToSleep"].get<float>();
        }

        if (j.contains("collisionLayers") && j["collisionLayers"].is_array())
        {
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

        if (j.contains("collisionMatrix") && j["collisionMatrix"].is_array())
        {
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
    }

    std::string SceneSerialization::audioDistanceModelToString(types::AudioDistanceModel model)
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

    types::AudioDistanceModel SceneSerialization::stringToAudioDistanceModel(const std::string& str)
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

    json SceneSerialization::serializeRenderSettings(const types::RenderSettings& settings)
    {
        json j;

        j["shadows"] = {
            {"enabled", settings.shadows.enabled},
            {"quality", shadowQualityToString(settings.shadows.quality)},
            {"cascadeCount", settings.shadows.cascadeCount},
            {"cascadeSplitMode", cascadeSplitModeToString(settings.shadows.cascadeSplitMode)},
            {"shadowBias", settings.shadows.shadowBias},
            {"slopeBias", settings.shadows.slopeBias},
            {"normalBias", settings.shadows.normalBias},
            {"shadowIntensity", settings.shadows.shadowIntensity}
        };

        j["culling"] = {
            {"frustumCullingEnabled", settings.culling.frustumCullingEnabled},
            {"occlusionCullingEnabled", settings.culling.occlusionCullingEnabled},
            {"lodSelectionEnabled", settings.culling.lodSelectionEnabled},
            {"meshletFrustumCullingEnabled", settings.culling.meshletFrustumCullingEnabled},
            {"meshletBackfaceCullingEnabled", settings.culling.meshletBackfaceCullingEnabled}
        };

        return j;
    }

    void SceneSerialization::deserializeRenderSettings(const json& j, types::RenderSettings& settings)
    {
        if (j.contains("shadows") && j["shadows"].is_object())
        {
            const auto& shadows = j["shadows"];
            if (shadows.contains("enabled") && shadows["enabled"].is_boolean())
                settings.shadows.enabled = shadows["enabled"].get<bool>();
            if (shadows.contains("quality") && shadows["quality"].is_string())
                settings.shadows.quality = stringToShadowQuality(shadows["quality"].get<std::string>());
            if (shadows.contains("cascadeCount") && shadows["cascadeCount"].is_number_unsigned())
            {
                uint8_t count = shadows["cascadeCount"].get<uint8_t>();
                settings.shadows.cascadeCount = std::clamp(count, uint8_t(1), uint8_t(4));
            }
            if (shadows.contains("cascadeSplitMode") && shadows["cascadeSplitMode"].is_string())
                settings.shadows.cascadeSplitMode = stringToCascadeSplitMode(shadows["cascadeSplitMode"].get<std::string>());
            if (shadows.contains("shadowBias") && shadows["shadowBias"].is_number())
                settings.shadows.shadowBias = shadows["shadowBias"].get<float>();
            if (shadows.contains("slopeBias") && shadows["slopeBias"].is_number())
                settings.shadows.slopeBias = shadows["slopeBias"].get<float>();
            if (shadows.contains("normalBias") && shadows["normalBias"].is_number())
                settings.shadows.normalBias = shadows["normalBias"].get<float>();
            if (shadows.contains("shadowIntensity") && shadows["shadowIntensity"].is_number())
                settings.shadows.shadowIntensity = shadows["shadowIntensity"].get<float>();
        }

        if (j.contains("culling") && j["culling"].is_object())
        {
            const auto& culling = j["culling"];
            if (culling.contains("frustumCullingEnabled") && culling["frustumCullingEnabled"].is_boolean())
                settings.culling.frustumCullingEnabled = culling["frustumCullingEnabled"].get<bool>();
            if (culling.contains("occlusionCullingEnabled") && culling["occlusionCullingEnabled"].is_boolean())
                settings.culling.occlusionCullingEnabled = culling["occlusionCullingEnabled"].get<bool>();
            if (culling.contains("lodSelectionEnabled") && culling["lodSelectionEnabled"].is_boolean())
                settings.culling.lodSelectionEnabled = culling["lodSelectionEnabled"].get<bool>();
            if (culling.contains("meshletFrustumCullingEnabled") && culling["meshletFrustumCullingEnabled"].is_boolean())
                settings.culling.meshletFrustumCullingEnabled = culling["meshletFrustumCullingEnabled"].get<bool>();
            if (culling.contains("meshletBackfaceCullingEnabled") && culling["meshletBackfaceCullingEnabled"].is_boolean())
                settings.culling.meshletBackfaceCullingEnabled = culling["meshletBackfaceCullingEnabled"].get<bool>();
        }
    }

    json SceneSerialization::serializeTerrain(const components::TerrainComponent& terrain)
    {
        json j;
        j["resolution"] = terrain.resolution;
        j["worldTileSize"] = terrain.worldTileSize;
        j["maxHeight"] = terrain.maxHeight;
        j["minHeight"] = terrain.minHeight;
        j["gridMinX"] = terrain.gridMinX;
        j["gridMinZ"] = terrain.gridMinZ;
        j["gridMaxX"] = terrain.gridMaxX;
        j["gridMaxZ"] = terrain.gridMaxZ;
        j["lodDistances"] = json::array({
            terrain.lodDistances[0], terrain.lodDistances[1],
            terrain.lodDistances[2], terrain.lodDistances[3]
        });
        std::string cleanPath = terrain.heightmapPath;
        cleanNullTerminators(cleanPath);
        j["heightmapPath"] = cleanPath;
        return j;
    }

    void SceneSerialization::deserializeTerrain(const json& j, components::TerrainComponent& terrain)
    {
        if (auto it = j.find("resolution"); it != j.end() && it->is_number_unsigned())
            terrain.resolution = it->get<uint8_t>();
        if (auto it = j.find("worldTileSize"); it != j.end() && it->is_number())
            terrain.worldTileSize = it->get<float>();
        if (auto it = j.find("maxHeight"); it != j.end() && it->is_number())
            terrain.maxHeight = it->get<float>();
        if (auto it = j.find("minHeight"); it != j.end() && it->is_number())
            terrain.minHeight = it->get<float>();
        if (auto it = j.find("gridMinX"); it != j.end() && it->is_number_integer())
            terrain.gridMinX = it->get<int32_t>();
        if (auto it = j.find("gridMinZ"); it != j.end() && it->is_number_integer())
            terrain.gridMinZ = it->get<int32_t>();
        if (auto it = j.find("gridMaxX"); it != j.end() && it->is_number_integer())
            terrain.gridMaxX = it->get<int32_t>();
        if (auto it = j.find("gridMaxZ"); it != j.end() && it->is_number_integer())
            terrain.gridMaxZ = it->get<int32_t>();
        if (auto it = j.find("lodDistances"); it != j.end() && it->is_array() && it->size() >= 4)
        {
            terrain.lodDistances[0] = (*it)[0].get<float>();
            terrain.lodDistances[1] = (*it)[1].get<float>();
            terrain.lodDistances[2] = (*it)[2].get<float>();
            terrain.lodDistances[3] = (*it)[3].get<float>();
        }
        if (auto it = j.find("heightmapPath"); it != j.end() && it->is_string())
            terrain.heightmapPath = it->get<std::string>();
    }

    json SceneSerialization::serializeTerrainTile(const components::TerrainTileComponent& tile)
    {
        json j;
        j["tileX"] = tile.tileX;
        j["tileZ"] = tile.tileZ;
        j["currentLOD"] = tile.currentLOD;
        j["isVisible"] = tile.isVisible;
        return j;
    }

    void SceneSerialization::deserializeTerrainTile(const json& j, components::TerrainTileComponent& tile)
    {
        if (auto it = j.find("tileX"); it != j.end() && it->is_number_integer())
            tile.tileX = it->get<int32_t>();
        if (auto it = j.find("tileZ"); it != j.end() && it->is_number_integer())
            tile.tileZ = it->get<int32_t>();
        if (auto it = j.find("currentLOD"); it != j.end() && it->is_number_unsigned())
            tile.currentLOD = it->get<uint8_t>();
        if (auto it = j.find("isVisible"); it != j.end() && it->is_boolean())
            tile.isVisible = it->get<bool>();
    }

    void SceneSerialization::deserializeChildren(const json& childrenJson, scene::Entity& parent,
                                                 scene::SceneGraphSystem& sceneGraph,
                                                 SceneLoadProgressCallback progressCallback, size_t& entitiesLoaded,
                                                 size_t totalEntities)
    {
        for (const auto& childJson : childrenJson)
        {
            if (!childJson.is_object())
            {
                vfLogWarning("Skipping invalid child entry in scene file (not an object)");
                continue;
            }

            std::string childName = childJson.value("name", "Unnamed");

            scene::Entity child(childName);

            if (!child.isValid())
            {
                vfLogError("Failed to create child entity '{}' during scene load", childName);
                continue;
            }

            if (childJson.contains("uuid") && childJson["uuid"].is_number_unsigned())
            {
                uint64_t uuidValue = childJson["uuid"].get<uint64_t>();
                child.addOrReplaceComponent<components::UUIDComponent>(uuidValue);
            }

            sceneGraph.addChild(parent, child);

            deserializeEntity(childJson, child, sceneGraph, false, progressCallback, entitiesLoaded, totalEntities);
        }
    }

    void SceneSerialization::deserializeEntity(const json& entityJson, scene::Entity& entity,
                                               scene::SceneGraphSystem& sceneGraph, bool isRoot,
                                               SceneLoadProgressCallback progressCallback, size_t& entitiesLoaded,
                                               size_t totalEntities)
    {
        std::string entityName = "Unnamed";
        if (entityJson.contains("name"))
        {
            entityName = entityJson["name"].get<std::string>();
            entity.setName(entityName);
        }

        if (entityJson.contains("isActive") && entityJson["isActive"].is_boolean())
        {
            if (entity.hasComponent<components::NameComponent>())
            {
                entity.getComponent<components::NameComponent>().isActive = entityJson["isActive"].get<bool>();
            }
        }

        if (progressCallback)
        {
            progressCallback(entityName, entitiesLoaded, totalEntities);
        }
        ++entitiesLoaded;

        if (isRoot && entityJson.contains("uuid"))
        {
            uint64_t uuidValue = entityJson["uuid"].get<uint64_t>();
            entity.addOrReplaceComponent<components::UUIDComponent>(uuidValue);
        }


        if (entityJson.contains("transform"))
        {
            auto& transform = entity.getComponent<components::TransformComponent>();
            deserializeTransform(entityJson["transform"], transform);
        }

        if (entityJson.contains("components"))
        {
            const auto& componentsJson = entityJson["components"];

            if (componentsJson.contains("camera"))
            {
                auto& camera = entity.addOrReplaceComponent<components::CameraComponent>();
                deserializeCamera(componentsJson["camera"], camera);
                if (!componentsJson.contains("billboard"))
                {
                    auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
                    billboard.iconType = components::BillboardIconType::Camera;
                }
            }

            if (componentsJson.contains("ibl"))
            {
                std::string iblFileName = deserializeIBL(componentsJson["ibl"]);
                if (!iblFileName.empty())
                {
                    entity.addOrReplaceComponent<components::IBLComponent>().fileName = iblFileName;
                }
            }

            if (componentsJson.contains("mesh"))
            {
                auto& meshComp = entity.addOrReplaceComponent<components::MeshComponent>();
                deserializeMesh(componentsJson["mesh"], meshComp);
            }

            if (componentsJson.contains("material"))
            {
                auto& matComp = entity.addOrReplaceComponent<components::MaterialComponent>();
                deserializeMaterial(componentsJson["material"], matComp);
            }

            if (componentsJson.contains("billboard"))
            {
                auto& billboardComp = entity.addOrReplaceComponent<components::BillboardComponent>();
                deserializeBillboard(componentsJson["billboard"], billboardComp);
            }

            if (componentsJson.contains("audioSource2D"))
            {
                auto& audioComp = entity.addOrReplaceComponent<components::AudioSource2DComponent>();
                deserializeAudioSource2D(componentsJson["audioSource2D"], audioComp);
                if (!componentsJson.contains("billboard"))
                {
                    auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
                    billboard.iconType = components::BillboardIconType::Audio2D;
                }
            }

            if (componentsJson.contains("audioSource3D"))
            {
                auto& audioComp = entity.addOrReplaceComponent<components::AudioSource3DComponent>();
                deserializeAudioSource3D(componentsJson["audioSource3D"], audioComp);
                if (!componentsJson.contains("billboard"))
                {
                    auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
                    billboard.iconType = components::BillboardIconType::Audio3D;
                }
            }

            if (componentsJson.contains("script"))
            {
                auto& scriptComp = entity.addOrReplaceComponent<components::ScriptComponent>();
                deserializeScript(componentsJson["script"], scriptComp);
            }

            if (componentsJson.contains("collider"))
            {
                auto& colliderComp = entity.addOrReplaceComponent<components::ColliderComponent>();
                deserializeCollider(componentsJson["collider"], colliderComp);
            }

            if (componentsJson.contains("rigidBody"))
            {
                auto& rigidBodyComp = entity.addOrReplaceComponent<components::RigidBodyComponent>();
                deserializeRigidBody(componentsJson["rigidBody"], rigidBodyComp);
            }

            if (componentsJson.contains("vfx"))
            {
                auto& vfxComp = entity.addOrReplaceComponent<components::VFXComponent>();
                deserializeVFX(componentsJson["vfx"], vfxComp);
                if (!componentsJson.contains("billboard"))
                {
                    auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
                    billboard.iconType = components::BillboardIconType::Particle;
                }
            }

            if (componentsJson.contains("directionalLight"))
            {
                auto& lightComp = entity.addOrReplaceComponent<components::DirectionalLightComponent>();
                deserializeDirectionalLight(componentsJson["directionalLight"], lightComp);
                if (!componentsJson.contains("billboard"))
                {
                    auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
                    billboard.iconType = components::BillboardIconType::DirectionalLight;
                }
            }

            if (componentsJson.contains("pointLight"))
            {
                auto& lightComp = entity.addOrReplaceComponent<components::PointLightComponent>();
                deserializePointLight(componentsJson["pointLight"], lightComp);
                if (!componentsJson.contains("billboard"))
                {
                    auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
                    billboard.iconType = components::BillboardIconType::PointLight;
                }
            }

            if (componentsJson.contains("spotLight"))
            {
                auto& lightComp = entity.addOrReplaceComponent<components::SpotLightComponent>();
                deserializeSpotLight(componentsJson["spotLight"], lightComp);
                if (!componentsJson.contains("billboard"))
                {
                    auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
                    billboard.iconType = components::BillboardIconType::SpotLight;
                }
            }

            if (componentsJson.contains("terrain"))
            {
                auto& terrainComp = entity.addOrReplaceComponent<components::TerrainComponent>();
                deserializeTerrain(componentsJson["terrain"], terrainComp);
            }

            if (componentsJson.contains("terrainTile"))
            {
                auto& tileComp = entity.addOrReplaceComponent<components::TerrainTileComponent>();
                deserializeTerrainTile(componentsJson["terrainTile"], tileComp);
            }
        }

        if (entityJson.contains("children") && entityJson["children"].is_array())
        {
            deserializeChildren(entityJson["children"], entity, sceneGraph, progressCallback, entitiesLoaded,
                                totalEntities);
        }
    }

    scene::SceneGraphSystem SceneSerialization::loadScene(std::string_view filename)
    {
        scene::SceneGraphSystem sceneGraph;
        loadSceneInto(filename, sceneGraph);
        return sceneGraph;
    }

    size_t SceneSerialization::countEntities(const json& entityJson)
    {
        size_t count = 1;
        if (entityJson.contains("children") && entityJson["children"].is_array())
        {
            for (const auto& child : entityJson["children"])
            {
                count += countEntities(child);
            }
        }
        return count;
    }

    bool SceneSerialization::loadSceneInto(std::string_view filename, scene::SceneGraphSystem& sceneGraph,
                                           SceneLoadProgressCallback progressCallback)
    {
        json sceneJson;

        // Phase 1: Validate file and parse JSON before modifying scene
        try
        {
            std::string filePath{filename};
            std::ifstream file{filePath};
            if (!file.is_open())
            {
                vfLogError("Failed to open file for reading: {}", filename);
                return false;
            }

            sceneJson = json::parse(file);
            file.close();

            if (!sceneJson.is_object())
            {
                vfLogError("Invalid scene file: root is not a JSON object");
                return false;
            }

            if (!sceneJson.contains("root") || !sceneJson["root"].is_object())
            {
                vfLogError("Invalid scene file: missing or invalid 'root' object");
                return false;
            }

        }
        catch (const json::parse_error& e)
        {
            vfLogError("JSON parse error while loading scene: {}", e.what());
            return false;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to read scene file: {}", e.what());
            return false;
        }

        // Phase 2: File validated - safe to clear and load
        try
        {
            size_t totalEntities = countEntities(sceneJson["root"]);
            size_t entitiesLoaded = 0;

            sceneGraph.clearScene();

            if (sceneJson.contains("physicsSettings") && sceneJson["physicsSettings"].is_object())
            {
                types::PhysicsSettings settings = types::PhysicsSettings::createDefault();
                deserializePhysicsSettings(sceneJson["physicsSettings"], settings);
                sceneGraph.setPhysicsSettings(settings);
            }
            else
            {
                sceneGraph.setPhysicsSettings(types::PhysicsSettings::createDefault());
            }

            if (sceneJson.contains("audioSettings") && sceneJson["audioSettings"].is_object())
            {
                types::AudioSettings audioSettings = types::AudioSettings::createDefault();
                deserializeAudioSettings(sceneJson["audioSettings"], audioSettings);
                sceneGraph.setAudioSettings(audioSettings);
            }
            else
            {
                sceneGraph.setAudioSettings(types::AudioSettings::createDefault());
            }

            if (sceneJson.contains("renderSettings") && sceneJson["renderSettings"].is_object())
            {
                types::RenderSettings renderSettings = types::RenderSettings::createDefault();
                deserializeRenderSettings(sceneJson["renderSettings"], renderSettings);
                sceneGraph.setRenderSettings(renderSettings);
            }
            else
            {
                sceneGraph.setRenderSettings(types::RenderSettings::createDefault());
            }

            scene::Entity& root = sceneGraph.GetRoot();
            deserializeEntity(sceneJson["root"], root, sceneGraph, true, progressCallback, entitiesLoaded,
                              totalEntities);

            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to deserialize scene: {}", e.what());
            sceneGraph.clearScene();
            return false;
        }
    }

    bool SceneSerialization::saveScene(scene::SceneGraphSystem& sceneGraph, std::string_view filename)
    {
        try
        {
            json sceneJson;
            sceneJson["version"] = "1.1";

            scene::Entity& root = sceneGraph.GetRoot();

            sceneJson["root"] = serializeEntity(root);
            sceneJson["physicsSettings"] = serializePhysicsSettings(sceneGraph.getPhysicsSettings());
            sceneJson["audioSettings"] = serializeAudioSettings(sceneGraph.getAudioSettings());
            sceneJson["renderSettings"] = serializeRenderSettings(sceneGraph.getRenderSettings());

            std::string filePath{filename};
            std::ofstream file{filePath};
            if (!file.is_open())
            {
                vfLogError("Failed to open file for writing: {}", filename);
                return false;
            }

            file << sceneJson.dump(2);
            file.close();

            vfLogInfo("Scene saved successfully to: {}", filename);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to save scene: {}", e.what());
            return false;
        }
    }

    json SceneSerialization::createSnapshot(scene::SceneGraphSystem& sceneGraph)
    {
        try
        {
            json snapshot;
            snapshot["version"] = "1.1";

            scene::Entity& root = sceneGraph.GetRoot();
            snapshot["root"] = serializeEntity(root);
            snapshot["physicsSettings"] = serializePhysicsSettings(sceneGraph.getPhysicsSettings());
            snapshot["audioSettings"] = serializeAudioSettings(sceneGraph.getAudioSettings());
            snapshot["renderSettings"] = serializeRenderSettings(sceneGraph.getRenderSettings());

            return snapshot;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to create scene snapshot: {}", e.what());
            return json();
        }
    }

    bool SceneSerialization::restoreFromSnapshot(const json& snapshot, scene::SceneGraphSystem& sceneGraph)
    {
        try
        {
            if (!snapshot.is_object())
            {
                vfLogError("Invalid snapshot: not a JSON object");
                return false;
            }

            if (!snapshot.contains("root") || !snapshot["root"].is_object())
            {
                vfLogError("Invalid snapshot: missing or invalid 'root' object");
                return false;
            }

            sceneGraph.clearScene();

            if (snapshot.contains("physicsSettings") && snapshot["physicsSettings"].is_object())
            {
                types::PhysicsSettings settings = types::PhysicsSettings::createDefault();
                deserializePhysicsSettings(snapshot["physicsSettings"], settings);
                sceneGraph.setPhysicsSettings(settings);
            }

            if (snapshot.contains("audioSettings") && snapshot["audioSettings"].is_object())
            {
                types::AudioSettings audioSettings = types::AudioSettings::createDefault();
                deserializeAudioSettings(snapshot["audioSettings"], audioSettings);
                sceneGraph.setAudioSettings(audioSettings);
            }

            if (snapshot.contains("renderSettings") && snapshot["renderSettings"].is_object())
            {
                types::RenderSettings renderSettings = types::RenderSettings::createDefault();
                deserializeRenderSettings(snapshot["renderSettings"], renderSettings);
                sceneGraph.setRenderSettings(renderSettings);
            }

            scene::Entity& root = sceneGraph.GetRoot();
            size_t entitiesLoaded = 0;
            size_t totalEntities = countEntities(snapshot["root"]);
            deserializeEntity(snapshot["root"], root, sceneGraph, true, nullptr, entitiesLoaded, totalEntities);

            vfLogInfo("Scene restored from snapshot successfully");
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to restore scene from snapshot: {}", e.what());
            sceneGraph.clearScene();
            return false;
        }
    }
}
