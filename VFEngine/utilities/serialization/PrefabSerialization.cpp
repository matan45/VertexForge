#include "PrefabSerialization.hpp"
#include "../print/Log.hpp"
#include "SceneSerialization.hpp"
#include "JsonConverters.hpp"
#include "../scene/SceneGraphSystem.hpp"
#include "../components/Components.hpp"
#include "../components/PhysicsAnimationComponent.hpp"
#include "../scene/EntityRegistry.hpp"

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

        if (entity.hasComponent<components::VehicleComponent>())
        {
            out["vehicle"] = SceneSerialization::serializeVehicle(
                entity.getComponent<components::VehicleComponent>());
        }

        if (entity.hasComponent<components::BuoyancyComponent>())
        {
            out["buoyancy"] = SceneSerialization::serializeBuoyancy(
                entity.getComponent<components::BuoyancyComponent>());
        }

        if (entity.hasComponent<components::WaterWakeEmitterComponent>())
        {
            out["waterWakeEmitter"] = SceneSerialization::serializeWaterWakeEmitter(
                entity.getComponent<components::WaterWakeEmitterComponent>());
        }

        if (entity.hasComponent<components::WaterBodyComponent>())   // VK-1607
        {
            out["waterBody"] = SceneSerialization::serializeWaterBody(
                entity.getComponent<components::WaterBodyComponent>());
        }

        if (entity.hasComponent<components::DestructibleComponent>())
        {
            out["destructible"] = SceneSerialization::serializeDestructible(
                entity.getComponent<components::DestructibleComponent>());
        }

        if (entity.hasComponent<components::PhysicsAnimationComponent>())
        {
            out["physicsAnimation"] = SceneSerialization::serializePhysicsAnimation(
                entity.getComponent<components::PhysicsAnimationComponent>());
        }

        if (entity.hasComponent<components::NavmeshAgentComponent>())
        {
            out["navmeshAgent"] = SceneSerialization::serializeNavmeshAgent(
                entity.getComponent<components::NavmeshAgentComponent>());
        }

        if (entity.hasComponent<components::NavmeshObstacleComponent>())
        {
            out["navmeshObstacle"] = SceneSerialization::serializeNavmeshObstacle(
                entity.getComponent<components::NavmeshObstacleComponent>());
        }

        if (entity.hasComponent<components::VFXComponent>())
        {
            out["vfx"] = SceneSerialization::serializeVFX(
                entity.getComponent<components::VFXComponent>());
        }

        if (entity.hasComponent<components::VFXSequenceComponent>())
        {
            out["vfxSequence"] = SceneSerialization::serializeVFXSequence(
                entity.getComponent<components::VFXSequenceComponent>());
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

        if (entity.hasComponent<components::SocketAttachmentComponent>())
        {
            auto socketJson = SceneSerialization::serializeSocketAttachment(
                entity.getComponent<components::SocketAttachmentComponent>());
            // VK-1590: prefab instantiation re-mints UUIDs, so a baked scene UUID would either
            // dangle or (worse) alias the original scene entity across every instance.
            // Prefab-internal attachments resolve by name via the ancestor walk.
            socketJson.erase("parentEntityUUID");
            out["socketAttachment"] = std::move(socketJson);
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

        // VK-1597: a prefab of an always-loaded landmark must instantiate still pinned, or the
        // instance is silently bucketed into a sector and vanishes on the next unload.
        if (entity.hasComponent<components::StreamingPolicyComponent>())
        {
            const auto& policy = entity.getComponent<components::StreamingPolicyComponent>();
            out["streamingPolicy"] = {
                {"spatiallyLoaded", policy.spatiallyLoaded}
            };
            // VK-1599: written only when non-zero, so a prefab that never touched grids keeps its
            // exact current payload.
            if (policy.gridIndex != 0)
                out["streamingPolicy"]["gridIndex"] = policy.gridIndex;
        }

        // Behavior tree must be baked so instantiated entities carry their AI brain
        // (the .vfBehaviorTree ref + enabled flag); matches the scene serializer.
        if (entity.hasComponent<components::BehaviorTreeComponent>())
        {
            out["behaviorTree"] = SceneSerialization::serializeBehaviorTree(
                entity.getComponent<components::BehaviorTreeComponent>());
        }

        // Script components must be baked into prefabs so instantiated entities
        // carry their @Script behavior (the scene serializer handles this too).
        if (entity.hasComponent<components::ScriptComponent>())
        {
            out["script"] = SceneSerialization::serializeScript(
                entity.getComponent<components::ScriptComponent>());
        }
    }

    void PrefabSerialization::serializeUIComponents(const scene::Entity& entity, json& out)
    {
        if (entity.hasComponent<components::UICanvasComponent>())
        {
            out["uiCanvas"] = SceneSerialization::serializeUICanvas(
                entity.getComponent<components::UICanvasComponent>());
        }

        if (entity.hasComponent<components::UIRectComponent>())
        {
            out["uiRect"] = SceneSerialization::serializeUIRect(
                entity.getComponent<components::UIRectComponent>());
        }

        if (entity.hasComponent<components::UIStyleComponent>())
        {
            out["uiStyle"] = SceneSerialization::serializeUIStyle(
                entity.getComponent<components::UIStyleComponent>());
        }

        if (entity.hasComponent<components::UIImageComponent>())
        {
            out["uiImage"] = SceneSerialization::serializeUIImage(
                entity.getComponent<components::UIImageComponent>());
        }

        if (entity.hasComponent<components::UIScrollComponent>())
        {
            out["uiScroll"] = SceneSerialization::serializeUIScroll(
                entity.getComponent<components::UIScrollComponent>());
        }

        if (entity.hasComponent<components::UILayoutGroupComponent>())
        {
            out["uiLayoutGroup"] = SceneSerialization::serializeUILayoutGroup(
                entity.getComponent<components::UILayoutGroupComponent>());
        }

        if (entity.hasComponent<components::UILabelComponent>())
        {
            out["uiLabel"] = SceneSerialization::serializeUILabel(
                entity.getComponent<components::UILabelComponent>());
        }

        if (entity.hasComponent<components::UITooltipComponent>())
        {
            out["uiTooltip"] = SceneSerialization::serializeUITooltip(
                entity.getComponent<components::UITooltipComponent>());
        }

        if (entity.hasComponent<components::UIWindowComponent>())
        {
            out["uiWindow"] = SceneSerialization::serializeUIWindow(
                entity.getComponent<components::UIWindowComponent>());
        }

        if (entity.hasComponent<components::UIListViewComponent>())
        {
            out["uiListView"] = SceneSerialization::serializeUIListView(
                entity.getComponent<components::UIListViewComponent>());
        }

        if (entity.hasComponent<components::UIAnimationComponent>())
        {
            out["uiAnimation"] = SceneSerialization::serializeUIAnimation(
                entity.getComponent<components::UIAnimationComponent>());
        }

        if (entity.hasComponent<components::UIMaskComponent>())
        {
            out["uiMask"] = SceneSerialization::serializeUIMask(
                entity.getComponent<components::UIMaskComponent>());
        }

        if (entity.hasComponent<components::UIDraggableComponent>())
        {
            out["uiDraggable"] = SceneSerialization::serializeUIDraggable(
                entity.getComponent<components::UIDraggableComponent>());
        }

        if (entity.hasComponent<components::UIDropTargetComponent>())
        {
            out["uiDropTarget"] = SceneSerialization::serializeUIDropTarget(
                entity.getComponent<components::UIDropTargetComponent>());
        }

        if (entity.hasComponent<components::UIButtonComponent>())
        {
            out["uiButton"] = SceneSerialization::serializeUIButton(
                entity.getComponent<components::UIButtonComponent>());
        }

        if (entity.hasComponent<components::UITextInputComponent>())
        {
            out["uiTextInput"] = SceneSerialization::serializeUITextInput(
                entity.getComponent<components::UITextInputComponent>());
        }

        if (entity.hasComponent<components::UICheckboxComponent>())
        {
            out["uiCheckbox"] = SceneSerialization::serializeUICheckbox(
                entity.getComponent<components::UICheckboxComponent>());
        }

        if (entity.hasComponent<components::UIDropdownComponent>())
        {
            out["uiDropdown"] = SceneSerialization::serializeUIDropdown(
                entity.getComponent<components::UIDropdownComponent>());
        }

        if (entity.hasComponent<components::UITabsComponent>())
        {
            out["uiTabs"] = SceneSerialization::serializeUITabs(
                entity.getComponent<components::UITabsComponent>());
        }

        if (entity.hasComponent<components::UISliderComponent>())
        {
            out["uiSlider"] = SceneSerialization::serializeUISlider(
                entity.getComponent<components::UISliderComponent>());
        }

        if (entity.hasComponent<components::UIProgressBarComponent>())
        {
            out["uiProgressBar"] = SceneSerialization::serializeUIProgressBar(
                entity.getComponent<components::UIProgressBarComponent>());
        }
    }

    json PrefabSerialization::serializeEntityTreeComponents(const scene::Entity& entity)
    {
        json componentsJson = json::object();
        serializeRenderComponents(entity, componentsJson);
        serializePhysicsAndEffectComponents(entity, componentsJson);
        serializeUIComponents(entity, componentsJson);

        // Plugin components
        if (SceneSerialization::pluginSerializeHook)
        {
            auto pluginJson = SceneSerialization::pluginSerializeHook(
                scene::EntityRegistry::getRegistry(), entity.getHandle());
            for (auto& [key, value] : pluginJson.items())
            {
                componentsJson[key] = std::move(value);
            }
        }

        return componentsJson;
    }

    json PrefabSerialization::serializeEntityTree(const scene::Entity& entity)
    {
        json entityJson;

        entityJson["name"] = entity.getName();

        // Active state. VK-1435 / VK-1433 Phase 4: the UI Layer Builder and Prefab Rig Preview
        // sandbox roots are held inactive in the live registry (so the main passes skip them);
        // that inactive flag is a sandbox-only artifact and must not bake into the saved .vfPrefab,
        // or the prefab would instantiate invisible. Both tags are editor-only and not serialized,
        // so normalize a tagged entity back to active.
        if (entity.hasComponent<components::NameComponent>())
        {
            const bool active = (entity.hasComponent<components::UIPreviewTagComponent>() ||
                                 entity.hasComponent<components::PreviewSandboxTagComponent>())
                ? true
                : entity.getComponent<components::NameComponent>().isActive;
            entityJson["isActive"] = active;
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
            // List-view item instances are engine-managed (rebuilt from the
            // item template on load) — baking them would duplicate items.
            if (child.hasComponent<components::UIListItemComponent>())
            {
                continue;
            }
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
            auto iblRef = SceneSerialization::deserializeIBLRef(componentsJson["ibl"]);
            if (iblRef.isValid())
            {
                auto& ibl = entity.addOrReplaceComponent<components::IBLComponent>();
                ibl.hdrRef = iblRef;
                SceneSerialization::deserializeIBLParams(componentsJson["ibl"], ibl); // VK-1574 knobs
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

        if (componentsJson.contains("vehicle"))
        {
            auto& vehicleComp = entity.addOrReplaceComponent<components::VehicleComponent>();
            SceneSerialization::deserializeVehicle(componentsJson["vehicle"], vehicleComp);
        }

        if (componentsJson.contains("buoyancy"))
        {
            auto& buoyancyComp = entity.addOrReplaceComponent<components::BuoyancyComponent>();
            SceneSerialization::deserializeBuoyancy(componentsJson["buoyancy"], buoyancyComp);
        }

        if (componentsJson.contains("waterWakeEmitter"))
        {
            auto& wakeComp = entity.addOrReplaceComponent<components::WaterWakeEmitterComponent>();
            SceneSerialization::deserializeWaterWakeEmitter(componentsJson["waterWakeEmitter"], wakeComp);
        }

        if (componentsJson.contains("waterBody"))   // VK-1607
        {
            auto& bodyComp = entity.addOrReplaceComponent<components::WaterBodyComponent>();
            SceneSerialization::deserializeWaterBody(componentsJson["waterBody"], bodyComp);
        }

        if (componentsJson.contains("destructible"))
        {
            auto& destructibleComp = entity.addOrReplaceComponent<components::DestructibleComponent>();
            SceneSerialization::deserializeDestructible(componentsJson["destructible"], destructibleComp);
        }

        if (componentsJson.contains("physicsAnimation"))
        {
            auto& physAnimComp = entity.addOrReplaceComponent<components::PhysicsAnimationComponent>();
            SceneSerialization::deserializePhysicsAnimation(componentsJson["physicsAnimation"], physAnimComp);
        }

        if (componentsJson.contains("navmeshAgent"))
        {
            auto& navAgentComp = entity.addOrReplaceComponent<components::NavmeshAgentComponent>();
            SceneSerialization::deserializeNavmeshAgent(componentsJson["navmeshAgent"], navAgentComp);
        }

        if (componentsJson.contains("navmeshObstacle"))
        {
            auto& navObstacleComp = entity.addOrReplaceComponent<components::NavmeshObstacleComponent>();
            SceneSerialization::deserializeNavmeshObstacle(componentsJson["navmeshObstacle"], navObstacleComp);
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

        if (componentsJson.contains("streamingPolicy")) // VK-1597
        {
            auto& policy = entity.addOrReplaceComponent<components::StreamingPolicyComponent>();
            policy.spatiallyLoaded = componentsJson["streamingPolicy"].value("spatiallyLoaded", true);
            // VK-1599: absent means the primary grid.
            policy.gridIndex =
                static_cast<uint8_t>(componentsJson["streamingPolicy"].value("gridIndex", 0));
        }

        if (componentsJson.contains("behaviorTree"))
        {
            auto& btComp = entity.addOrReplaceComponent<components::BehaviorTreeComponent>();
            SceneSerialization::deserializeBehaviorTree(componentsJson["behaviorTree"], btComp);
        }

        if (componentsJson.contains("script"))
        {
            auto& scriptComp = entity.addOrReplaceComponent<components::ScriptComponent>();
            SceneSerialization::deserializeScript(componentsJson["script"], scriptComp);
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

        if (componentsJson.contains("vfxSequence"))
        {
            auto& comp = entity.addOrReplaceComponent<components::VFXSequenceComponent>();
            SceneSerialization::deserializeVFXSequence(componentsJson["vfxSequence"], comp);
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

    }

    void PrefabSerialization::deserializeUIComponents(const json& componentsJson, scene::Entity& entity)
    {
        SceneSerialization::deserializeUIStructuralComponents(componentsJson, entity);
        SceneSerialization::deserializeUIInteractiveComponents(componentsJson, entity);
    }

    void PrefabSerialization::deserializeComponents(const json& componentsJson, scene::Entity& entity)
    {
        deserializeRenderingComponents(componentsJson, entity);
        deserializeSceneComponents(componentsJson, entity);
        deserializeMediaComponents(componentsJson, entity);
        deserializeLightComponents(componentsJson, entity);
        deserializeUIComponents(componentsJson, entity);

        // Plugin components
        if (SceneSerialization::pluginDeserializeHook)
        {
            SceneSerialization::pluginDeserializeHook(componentsJson,
                scene::EntityRegistry::getRegistry(), entity.getHandle());
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
}
