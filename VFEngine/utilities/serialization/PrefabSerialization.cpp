#include "PrefabSerialization.hpp"
#include "SceneSerialization.hpp"
#include "JsonConverters.hpp"
#include "../scene/SceneGraphSystem.hpp"
#include "../components/Components.hpp"
#include "../print/EditorLogger.hpp"
#include <fstream>

namespace serialization
{
    json PrefabSerialization::serializeEntityTree(const scene::Entity& entity)
    {
        json entityJson;

        entityJson["name"] = entity.getName();

        // Active state
        if (entity.hasComponent<components::NameComponent>())
        {
            entityJson["isActive"] = entity.getComponent<components::NameComponent>().isActive;
        }

        if (entity.hasComponent<components::TransformComponent>())
        {
            entityJson["transform"] = SceneSerialization::serializeTransform(
                entity.getComponent<components::TransformComponent>());
        }

        json componentsJson = json::object();

        if (entity.hasComponent<components::CameraComponent>())
        {
            componentsJson["camera"] = SceneSerialization::serializeCamera(
                entity.getComponent<components::CameraComponent>());
        }

        if (entity.hasComponent<components::IBLComponent>())
        {
            componentsJson["ibl"] = SceneSerialization::serializeIBL(
                entity.getComponent<components::IBLComponent>());
        }

        if (entity.hasComponent<components::MeshComponent>())
        {
            componentsJson["mesh"] = SceneSerialization::serializeMesh(
                entity.getComponent<components::MeshComponent>());
        }

        if (entity.hasComponent<components::MaterialComponent>())
        {
            componentsJson["material"] = SceneSerialization::serializeMaterial(
                entity.getComponent<components::MaterialComponent>());
        }

        if (entity.hasComponent<components::BillboardComponent>())
        {
            componentsJson["billboard"] = SceneSerialization::serializeBillboard(
                entity.getComponent<components::BillboardComponent>());
        }

        if (entity.hasComponent<components::AudioSource2DComponent>())
        {
            componentsJson["audioSource2D"] = SceneSerialization::serializeAudioSource2D(
                entity.getComponent<components::AudioSource2DComponent>());
        }

        if (entity.hasComponent<components::AudioSource3DComponent>())
        {
            componentsJson["audioSource3D"] = SceneSerialization::serializeAudioSource3D(
                entity.getComponent<components::AudioSource3DComponent>());
        }

        if (entity.hasComponent<components::ColliderComponent>())
        {
            componentsJson["collider"] = SceneSerialization::serializeCollider(
                entity.getComponent<components::ColliderComponent>());
        }

        if (entity.hasComponent<components::RigidBodyComponent>())
        {
            componentsJson["rigidBody"] = SceneSerialization::serializeRigidBody(
                entity.getComponent<components::RigidBodyComponent>());
        }

        if (entity.hasComponent<components::VFXComponent>())
        {
            componentsJson["vfx"] = SceneSerialization::serializeVFX(
                entity.getComponent<components::VFXComponent>());
        }

        if (entity.hasComponent<components::DirectionalLightComponent>())
        {
            componentsJson["directionalLight"] = SceneSerialization::serializeDirectionalLight(
                entity.getComponent<components::DirectionalLightComponent>());
        }

        if (entity.hasComponent<components::PointLightComponent>())
        {
            componentsJson["pointLight"] = SceneSerialization::serializePointLight(
                entity.getComponent<components::PointLightComponent>());
        }

        if (entity.hasComponent<components::SpotLightComponent>())
        {
            componentsJson["spotLight"] = SceneSerialization::serializeSpotLight(
                entity.getComponent<components::SpotLightComponent>());
        }

        entityJson["components"] = componentsJson;

        json childrenJson = json::array();
        for (const auto& child : entity.getChildren())
        {
            childrenJson.push_back(serializeEntityTree(child));
        }
        entityJson["children"] = childrenJson;

        return entityJson;
    }

    void PrefabSerialization::deserializeComponents(const json& componentsJson, scene::Entity& entity)
    {
        if (componentsJson.contains("camera"))
        {
            auto& camera = entity.addOrReplaceComponent<components::CameraComponent>();
            SceneSerialization::deserializeCamera(componentsJson["camera"], camera);
            if (!componentsJson.contains("billboard"))
            {
                auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
                billboard.iconType = components::BillboardIconType::Camera;
            }
        }

        if (componentsJson.contains("ibl"))
        {
            std::string iblFileName = SceneSerialization::deserializeIBL(componentsJson["ibl"]);
            if (!iblFileName.empty())
            {
                entity.addOrReplaceComponent<components::IBLComponent>().fileName = iblFileName;
            }
        }

        if (componentsJson.contains("mesh"))
        {
            auto& meshComp = entity.addOrReplaceComponent<components::MeshComponent>();
            SceneSerialization::deserializeMesh(componentsJson["mesh"], meshComp);
        }

        if (componentsJson.contains("material"))
        {
            auto& matComp = entity.addOrReplaceComponent<components::MaterialComponent>();
            SceneSerialization::deserializeMaterial(componentsJson["material"], matComp);
        }

        if (componentsJson.contains("billboard"))
        {
            auto& billboardComp = entity.addOrReplaceComponent<components::BillboardComponent>();
            SceneSerialization::deserializeBillboard(componentsJson["billboard"], billboardComp);
        }

        if (componentsJson.contains("audioSource2D"))
        {
            auto& audioComp = entity.addOrReplaceComponent<components::AudioSource2DComponent>();
            SceneSerialization::deserializeAudioSource2D(componentsJson["audioSource2D"], audioComp);
            if (!componentsJson.contains("billboard"))
            {
                auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
                billboard.iconType = components::BillboardIconType::Audio2D;
            }
        }

        if (componentsJson.contains("audioSource3D"))
        {
            auto& audioComp = entity.addOrReplaceComponent<components::AudioSource3DComponent>();
            SceneSerialization::deserializeAudioSource3D(componentsJson["audioSource3D"], audioComp);
            if (!componentsJson.contains("billboard"))
            {
                auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
                billboard.iconType = components::BillboardIconType::Audio3D;
            }
        }

        if (componentsJson.contains("collider"))
        {
            auto& colliderComp = entity.addOrReplaceComponent<components::ColliderComponent>();
            SceneSerialization::deserializeCollider(componentsJson["collider"], colliderComp);
        }

        if (componentsJson.contains("rigidBody"))
        {
            auto& rigidBodyComp = entity.addOrReplaceComponent<components::RigidBodyComponent>();
            SceneSerialization::deserializeRigidBody(componentsJson["rigidBody"], rigidBodyComp);
        }

        if (componentsJson.contains("vfx"))
        {
            auto& vfxComp = entity.addOrReplaceComponent<components::VFXComponent>();
            SceneSerialization::deserializeVFX(componentsJson["vfx"], vfxComp);
            // Auto-attach billboard if not explicitly serialized
            if (!componentsJson.contains("billboard"))
            {
                auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
                billboard.iconType = components::BillboardIconType::Particle;
            }
        }

        if (componentsJson.contains("directionalLight"))
        {
            auto& lightComp = entity.addOrReplaceComponent<components::DirectionalLightComponent>();
            SceneSerialization::deserializeDirectionalLight(componentsJson["directionalLight"], lightComp);
            // Auto-attach billboard if not explicitly serialized
            if (!componentsJson.contains("billboard"))
            {
                auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
                billboard.iconType = components::BillboardIconType::DirectionalLight;
            }
        }

        if (componentsJson.contains("pointLight"))
        {
            auto& lightComp = entity.addOrReplaceComponent<components::PointLightComponent>();
            SceneSerialization::deserializePointLight(componentsJson["pointLight"], lightComp);
            // Auto-attach billboard if not explicitly serialized
            if (!componentsJson.contains("billboard"))
            {
                auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
                billboard.iconType = components::BillboardIconType::PointLight;
            }
        }

        if (componentsJson.contains("spotLight"))
        {
            auto& lightComp = entity.addOrReplaceComponent<components::SpotLightComponent>();
            SceneSerialization::deserializeSpotLight(componentsJson["spotLight"], lightComp);
            // Auto-attach billboard if not explicitly serialized
            if (!componentsJson.contains("billboard"))
            {
                auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
                billboard.iconType = components::BillboardIconType::SpotLight;
            }
        }
    }

    scene::Entity PrefabSerialization::deserializeEntityTree(
        const json& entityJson,
        scene::Entity& parent,
        scene::SceneGraphSystem& sceneGraph)
    {
        std::string entityName = entityJson.value("name", "Prefab");
        scene::Entity entity(entityName);

        if (!entity.isValid())
        {
            vfLogError("Failed to create entity '{}' during prefab load", entityName);
            return entity;
        }

        sceneGraph.addChild(parent, entity);

        // Restore active state
        if (entityJson.contains("isActive") && entityJson["isActive"].is_boolean())
        {
            if (entity.hasComponent<components::NameComponent>())
            {
                entity.getComponent<components::NameComponent>().isActive = entityJson["isActive"].get<bool>();
            }
        }

        if (entityJson.contains("transform"))
        {
            auto& transform = entity.getComponent<components::TransformComponent>();
            SceneSerialization::deserializeTransform(entityJson["transform"], transform);
        }

        if (entityJson.contains("components"))
        {
            deserializeComponents(entityJson["components"], entity);
        }

        if (entityJson.contains("children") && entityJson["children"].is_array())
        {
            for (const auto& childJson : entityJson["children"])
            {
                if (!childJson.is_object())
                {
                    vfLogWarning("Skipping invalid child entry in prefab (not an object)");
                    continue;
                }
                deserializeEntityTree(childJson, entity, sceneGraph);
            }
        }

        return entity;
    }

    bool PrefabSerialization::savePrefab(const scene::Entity& entity, std::string_view filename)
    {
        try
        {
            json prefabJson;
            prefabJson["version"] = "1.0";
            prefabJson["prefab"]["name"] = entity.getName();
            prefabJson["prefab"]["entity"] = serializeEntityTree(entity);

            std::string filePath{filename};
            std::ofstream file{filePath};
            if (!file.is_open())
            {
                vfLogError("Failed to open file for writing: {}", filename);
                return false;
            }

            file << prefabJson.dump(2);
            if (!file.good())
            {
                vfLogError("Failed to write prefab data to: {}", filename);
                return false;
            }
            file.close();

            vfLogInfo("Prefab saved successfully to: {}", filename);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to save prefab: {}", e.what());
            return false;
        }
    }

    std::optional<scene::Entity> PrefabSerialization::loadPrefab(
        std::string_view filename,
        scene::Entity& parent,
        scene::SceneGraphSystem& sceneGraph)
    {
        try
        {
            std::string filePath{filename};
            std::ifstream file{filePath};
            if (!file.is_open())
            {
                vfLogError("Failed to open prefab file: {}", filename);
                return std::nullopt;
            }

            json prefabJson = json::parse(file);
            file.close();

            if (!prefabJson.is_object())
            {
                vfLogError("Invalid prefab file: root is not a JSON object");
                return std::nullopt;
            }

            if (!prefabJson.contains("prefab") || !prefabJson["prefab"].is_object())
            {
                vfLogError("Invalid prefab file: missing 'prefab' object");
                return std::nullopt;
            }

            if (!prefabJson["prefab"].contains("entity") || !prefabJson["prefab"]["entity"].is_object())
            {
                vfLogError("Invalid prefab file: missing 'entity' object");
                return std::nullopt;
            }

            if (prefabJson.contains("version") && prefabJson["version"].is_string())
            {
                std::string version = prefabJson["version"].get<std::string>();
                vfLogInfo("Loading prefab version: {}", version);
            }

            scene::Entity rootEntity = deserializeEntityTree(
                prefabJson["prefab"]["entity"],
                parent,
                sceneGraph
            );

            if (!rootEntity.isValid())
            {
                vfLogError("Failed to instantiate prefab from: {}", filename);
                return std::nullopt;
            }

            vfLogInfo("Prefab loaded successfully from: {}", filename);
            return rootEntity;
        }
        catch (const json::parse_error& e)
        {
            vfLogError("JSON parse error while loading prefab: {}", e.what());
            return std::nullopt;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to load prefab: {}", e.what());
            return std::nullopt;
        }
    }

    bool PrefabSerialization::validatePrefab(std::string_view filename)
    {
        try
        {
            std::string filePath{filename};
            std::ifstream file{filePath};
            if (!file.is_open())
            {
                return false;
            }

            json prefabJson = json::parse(file);
            file.close();

            if (!prefabJson.is_object()) return false;
            if (!prefabJson.contains("prefab")) return false;
            if (!prefabJson["prefab"].contains("entity")) return false;

            return true;
        }
        catch (...)
        {
            return false;
        }
    }
}
