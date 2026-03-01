#include "PrefabSerialization.hpp"
#include "SceneSerialization.hpp"
#include "JsonConverters.hpp"
#include "../scene/SceneGraphSystem.hpp"
#include "../components/Components.hpp"
#include "../components/PhysicsAnimationComponent.hpp"
#include "../print/EditorLogger.hpp"
#include <fstream>

namespace serialization
{
    void PrefabSerialization::serializeRenderComponents(const scene::Entity& entity, json& out)
    {
        if (entity.hasComponent<components::CameraComponent>())
        {
            out["camera"] = SceneSerialization::serializeCamera(
                entity.getComponent<components::CameraComponent>());
        }

        if (entity.hasComponent<components::IBLComponent>())
        {
            out["ibl"] = SceneSerialization::serializeIBL(
                entity.getComponent<components::IBLComponent>());
        }

        if (entity.hasComponent<components::MeshComponent>())
        {
            out["mesh"] = SceneSerialization::serializeMesh(
                entity.getComponent<components::MeshComponent>());
        }

        if (entity.hasComponent<components::MaterialComponent>())
        {
            out["material"] = SceneSerialization::serializeMaterial(
                entity.getComponent<components::MaterialComponent>());
        }

        if (entity.hasComponent<components::BillboardComponent>())
        {
            out["billboard"] = SceneSerialization::serializeBillboard(
                entity.getComponent<components::BillboardComponent>());
        }

        if (entity.hasComponent<components::TextComponent>())
        {
            out["text"] = SceneSerialization::serializeText(
                entity.getComponent<components::TextComponent>());
        }
    }

    void PrefabSerialization::serializePhysicsAndEffectComponents(const scene::Entity& entity, json& out)
    {
        if (entity.hasComponent<components::AudioSource2DComponent>())
        {
            out["audioSource2D"] = SceneSerialization::serializeAudioSource2D(
                entity.getComponent<components::AudioSource2DComponent>());
        }

        if (entity.hasComponent<components::AudioSource3DComponent>())
        {
            out["audioSource3D"] = SceneSerialization::serializeAudioSource3D(
                entity.getComponent<components::AudioSource3DComponent>());
        }

        if (entity.hasComponent<components::ColliderComponent>())
        {
            out["collider"] = SceneSerialization::serializeCollider(
                entity.getComponent<components::ColliderComponent>());
        }

        if (entity.hasComponent<components::RigidBodyComponent>())
        {
            out["rigidBody"] = SceneSerialization::serializeRigidBody(
                entity.getComponent<components::RigidBodyComponent>());
        }

        if (entity.hasComponent<components::PhysicsAnimationComponent>())
        {
            out["physicsAnimation"] = SceneSerialization::serializePhysicsAnimation(
                entity.getComponent<components::PhysicsAnimationComponent>());
        }

        if (entity.hasComponent<components::VFXComponent>())
        {
            out["vfx"] = SceneSerialization::serializeVFX(
                entity.getComponent<components::VFXComponent>());
        }

        if (entity.hasComponent<components::DirectionalLightComponent>())
        {
            out["directionalLight"] = SceneSerialization::serializeDirectionalLight(
                entity.getComponent<components::DirectionalLightComponent>());
        }

        if (entity.hasComponent<components::PointLightComponent>())
        {
            out["pointLight"] = SceneSerialization::serializePointLight(
                entity.getComponent<components::PointLightComponent>());
        }

        if (entity.hasComponent<components::SpotLightComponent>())
        {
            out["spotLight"] = SceneSerialization::serializeSpotLight(
                entity.getComponent<components::SpotLightComponent>());
        }

        if (entity.hasComponent<components::LightmapComponent>())
        {
            out["lightmap"] = SceneSerialization::serializeLightmap(
                entity.getComponent<components::LightmapComponent>());
        }

        if (entity.hasComponent<components::SocketAttachmentComponent>())
        {
            out["socketAttachment"] = SceneSerialization::serializeSocketAttachment(
                entity.getComponent<components::SocketAttachmentComponent>());
        }

        if (entity.hasComponent<components::SocketOverrideComponent>())
        {
            out["socketOverride"] = SceneSerialization::serializeSocketOverride(
                entity.getComponent<components::SocketOverrideComponent>());
        }

        if (entity.hasComponent<components::ControllerComponent>())
        {
            out["controller"] = SceneSerialization::serializeController(
                entity.getComponent<components::ControllerComponent>());
        }

        if (entity.hasComponent<components::IKTargetComponent>())
        {
            out["ikTarget"] = SceneSerialization::serializeIKTarget(
                entity.getComponent<components::IKTargetComponent>());
        }
    }

    json PrefabSerialization::serializeEntityTreeComponents(const scene::Entity& entity)
    {
        json componentsJson = json::object();
        serializeRenderComponents(entity, componentsJson);
        serializePhysicsAndEffectComponents(entity, componentsJson);
        return componentsJson;
    }

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

        entityJson["components"] = serializeEntityTreeComponents(entity);

        json childrenJson = json::array();
        for (const auto& child : entity.getChildren())
        {
            childrenJson.push_back(serializeEntityTree(child));
        }
        entityJson["children"] = childrenJson;

        return entityJson;
    }

    void PrefabSerialization::deserializeRenderingComponents(const json& componentsJson, scene::Entity& entity)
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
    }

    void PrefabSerialization::deserializeSceneComponents(const json& componentsJson, scene::Entity& entity)
    {
        if (componentsJson.contains("billboard"))
        {
            auto& billboardComp = entity.addOrReplaceComponent<components::BillboardComponent>();
            SceneSerialization::deserializeBillboard(componentsJson["billboard"], billboardComp);
        }

        if (componentsJson.contains("text"))
        {
            auto& textComp = entity.addOrReplaceComponent<components::TextComponent>();
            SceneSerialization::deserializeText(componentsJson["text"], textComp);
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

        if (componentsJson.contains("physicsAnimation"))
        {
            auto& physAnimComp = entity.addOrReplaceComponent<components::PhysicsAnimationComponent>();
            SceneSerialization::deserializePhysicsAnimation(componentsJson["physicsAnimation"], physAnimComp);
        }

        if (componentsJson.contains("socketAttachment"))
        {
            auto& attachment = entity.addOrReplaceComponent<components::SocketAttachmentComponent>();
            SceneSerialization::deserializeSocketAttachment(componentsJson["socketAttachment"], attachment);
        }

        if (componentsJson.contains("socketOverride"))
        {
            auto& socketOverride = entity.addOrReplaceComponent<components::SocketOverrideComponent>();
            SceneSerialization::deserializeSocketOverride(componentsJson["socketOverride"], socketOverride);
        }

        if (componentsJson.contains("controller"))
        {
            auto& controllerComp = entity.addOrReplaceComponent<components::ControllerComponent>();
            SceneSerialization::deserializeController(componentsJson["controller"], controllerComp);
        }

        if (componentsJson.contains("ikTarget"))
        {
            auto& ikTargetComp = entity.addOrReplaceComponent<components::IKTargetComponent>();
            SceneSerialization::deserializeIKTarget(componentsJson["ikTarget"], ikTargetComp);
        }
    }

    void PrefabSerialization::deserializeMediaComponents(const json& componentsJson, scene::Entity& entity)
    {
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

        if (componentsJson.contains("vfx"))
        {
            auto& vfxComp = entity.addOrReplaceComponent<components::VFXComponent>();
            SceneSerialization::deserializeVFX(componentsJson["vfx"], vfxComp);
            if (!componentsJson.contains("billboard"))
            {
                auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
                billboard.iconType = components::BillboardIconType::Particle;
            }
        }
    }

    void PrefabSerialization::deserializeLightComponents(const json& componentsJson, scene::Entity& entity)
    {
        if (componentsJson.contains("directionalLight"))
        {
            auto& lightComp = entity.addOrReplaceComponent<components::DirectionalLightComponent>();
            SceneSerialization::deserializeDirectionalLight(componentsJson["directionalLight"], lightComp);
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
            if (!componentsJson.contains("billboard"))
            {
                auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
                billboard.iconType = components::BillboardIconType::SpotLight;
            }
        }

        if (componentsJson.contains("lightmap"))
        {
            auto& lmComp = entity.addOrReplaceComponent<components::LightmapComponent>();
            SceneSerialization::deserializeLightmap(componentsJson["lightmap"], lmComp);
        }
    }

    void PrefabSerialization::deserializeComponents(const json& componentsJson, scene::Entity& entity)
    {
        deserializeRenderingComponents(componentsJson, entity);
        deserializeSceneComponents(componentsJson, entity);
        deserializeMediaComponents(componentsJson, entity);
        deserializeLightComponents(componentsJson, entity);
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

    std::optional<json> PrefabSerialization::parsePrefabJson(std::string_view filename)
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

        return prefabJson;
    }

    std::optional<scene::Entity> PrefabSerialization::loadPrefab(
        std::string_view filename,
        scene::Entity& parent,
        scene::SceneGraphSystem& sceneGraph)
    {
        try
        {
            auto prefabJsonOpt = parsePrefabJson(filename);
            if (!prefabJsonOpt.has_value())
            {
                return std::nullopt;
            }

            scene::Entity rootEntity = deserializeEntityTree(
                (*prefabJsonOpt)["prefab"]["entity"],
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
