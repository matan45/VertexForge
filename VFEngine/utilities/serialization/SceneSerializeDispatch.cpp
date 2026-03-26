#include "SceneSerialization.hpp"
#include "JsonConverters.hpp"
#include "../scene/SceneGraphSystem.hpp"
#include "../components/Components.hpp"

namespace serialization
{
    SceneSerialization::PluginSerializeFn SceneSerialization::pluginSerializeHook;
    SceneSerialization::PluginDeserializeFn SceneSerialization::pluginDeserializeHook;
    std::shared_mutex SceneSerialization::pluginHookMutex;

    void SceneSerialization::setPluginSerializationHooks(PluginSerializeFn serialize, PluginDeserializeFn deserialize)
    {
        std::unique_lock lock(pluginHookMutex);
        pluginSerializeHook = std::move(serialize);
        pluginDeserializeHook = std::move(deserialize);
    }

    void SceneSerialization::serializeRenderComponents(scene::Entity& entity, json& out)
    {
        if (entity.hasComponent<components::CameraComponent>())
            out["camera"] = serializeCamera(entity.getComponent<components::CameraComponent>());
        if (entity.hasComponent<components::IBLComponent>())
            out["ibl"] = serializeIBL(entity.getComponent<components::IBLComponent>());
        if (entity.hasComponent<components::WorldSectorComponent>())
        {
            json wsJson;
            wsJson["worldFilePath"] = entity.getComponent<components::WorldSectorComponent>().worldFilePath;
            out["worldSector"] = wsJson;
        }
        if (entity.hasComponent<components::MeshComponent>())
            out["mesh"] = serializeMesh(entity.getComponent<components::MeshComponent>());
        if (entity.hasComponent<components::MaterialComponent>())
            out["material"] = serializeMaterial(entity.getComponent<components::MaterialComponent>());
        if (entity.hasComponent<components::BillboardComponent>())
            out["billboard"] = serializeBillboard(entity.getComponent<components::BillboardComponent>());
        if (entity.hasComponent<components::TextComponent>())
            out["text"] = serializeText(entity.getComponent<components::TextComponent>());
    }

    void SceneSerialization::serializeAudioPhysicsComponents(scene::Entity& entity, json& out)
    {
        if (entity.hasComponent<components::AudioSource2DComponent>())
            out["audioSource2D"] = serializeAudioSource2D(entity.getComponent<components::AudioSource2DComponent>());
        if (entity.hasComponent<components::AudioSource3DComponent>())
            out["audioSource3D"] = serializeAudioSource3D(entity.getComponent<components::AudioSource3DComponent>());
        if (entity.hasComponent<components::ReverbZoneComponent>())
            out["reverbZone"] = serializeReverbZone(entity.getComponent<components::ReverbZoneComponent>());
        if (entity.hasComponent<components::FogVolumeComponent>())
            out["fogVolume"] = serializeFogVolume(entity.getComponent<components::FogVolumeComponent>());
        if (entity.hasComponent<components::ScriptComponent>())
            out["script"] = serializeScript(entity.getComponent<components::ScriptComponent>());
        if (entity.hasComponent<components::ColliderComponent>())
            out["collider"] = serializeCollider(entity.getComponent<components::ColliderComponent>());
        if (entity.hasComponent<components::RigidBodyComponent>())
            out["rigidBody"] = serializeRigidBody(entity.getComponent<components::RigidBodyComponent>());
        if (entity.hasComponent<components::PhysicsAnimationComponent>())
            out["physicsAnimation"] = serializePhysicsAnimation(entity.getComponent<components::PhysicsAnimationComponent>());
        if (entity.hasComponent<components::VFXComponent>())
            out["vfx"] = serializeVFX(entity.getComponent<components::VFXComponent>());
    }

    void SceneSerialization::serializeLightEnvironmentComponents(scene::Entity& entity, json& out)
    {
        if (entity.hasComponent<components::DirectionalLightComponent>())
            out["directionalLight"] = serializeDirectionalLight(entity.getComponent<components::DirectionalLightComponent>());
        if (entity.hasComponent<components::PointLightComponent>())
            out["pointLight"] = serializePointLight(entity.getComponent<components::PointLightComponent>());
        if (entity.hasComponent<components::SpotLightComponent>())
            out["spotLight"] = serializeSpotLight(entity.getComponent<components::SpotLightComponent>());
        if (entity.hasComponent<components::TerrainComponent>())
            out["terrain"] = serializeTerrain(entity.getComponent<components::TerrainComponent>());
        if (entity.hasComponent<components::TerrainTileComponent>())
            out["terrainTile"] = serializeTerrainTile(entity.getComponent<components::TerrainTileComponent>());
        if (entity.hasComponent<components::GrassComponent>())
            out["grass"] = serializeGrass(entity.getComponent<components::GrassComponent>());
        if (entity.hasComponent<components::WaterComponent>())
            out["water"] = serializeWater(entity.getComponent<components::WaterComponent>());
        if (entity.hasComponent<components::WaterTileComponent>())
            out["waterTile"] = serializeWaterTile(entity.getComponent<components::WaterTileComponent>());
    }

    void SceneSerialization::serializeUIStructuralComponents(scene::Entity& entity, json& out)
    {
        if (entity.hasComponent<components::UICanvasComponent>())
            out["uiCanvas"] = serializeUICanvas(entity.getComponent<components::UICanvasComponent>());
        if (entity.hasComponent<components::UIRectComponent>())
            out["uiRect"] = serializeUIRect(entity.getComponent<components::UIRectComponent>());
        if (entity.hasComponent<components::UIImageComponent>())
            out["uiImage"] = serializeUIImage(entity.getComponent<components::UIImageComponent>());
        if (entity.hasComponent<components::UIScrollComponent>())
            out["uiScroll"] = serializeUIScroll(entity.getComponent<components::UIScrollComponent>());
        if (entity.hasComponent<components::UILayoutGroupComponent>())
            out["uiLayoutGroup"] = serializeUILayoutGroup(entity.getComponent<components::UILayoutGroupComponent>());
        if (entity.hasComponent<components::UILabelComponent>())
            out["uiLabel"] = serializeUILabel(entity.getComponent<components::UILabelComponent>());
        if (entity.hasComponent<components::UIAnimationComponent>())
            out["uiAnimation"] = serializeUIAnimation(entity.getComponent<components::UIAnimationComponent>());
        if (entity.hasComponent<components::UIMaskComponent>())
            out["uiMask"] = serializeUIMask(entity.getComponent<components::UIMaskComponent>());
        if (entity.hasComponent<components::UIDraggableComponent>())
            out["uiDraggable"] = serializeUIDraggable(entity.getComponent<components::UIDraggableComponent>());
        if (entity.hasComponent<components::UIDropTargetComponent>())
            out["uiDropTarget"] = serializeUIDropTarget(entity.getComponent<components::UIDropTargetComponent>());
    }

    void SceneSerialization::serializeUIInteractiveComponents(scene::Entity& entity, json& out)
    {
        if (entity.hasComponent<components::UIButtonComponent>())
            out["uiButton"] = serializeUIButton(entity.getComponent<components::UIButtonComponent>());
        if (entity.hasComponent<components::UITextInputComponent>())
            out["uiTextInput"] = serializeUITextInput(entity.getComponent<components::UITextInputComponent>());
        if (entity.hasComponent<components::UICheckboxComponent>())
            out["uiCheckbox"] = serializeUICheckbox(entity.getComponent<components::UICheckboxComponent>());
        if (entity.hasComponent<components::UIDropdownComponent>())
            out["uiDropdown"] = serializeUIDropdown(entity.getComponent<components::UIDropdownComponent>());
        if (entity.hasComponent<components::UITabsComponent>())
            out["uiTabs"] = serializeUITabs(entity.getComponent<components::UITabsComponent>());
        if (entity.hasComponent<components::UISliderComponent>())
            out["uiSlider"] = serializeUISlider(entity.getComponent<components::UISliderComponent>());
        if (entity.hasComponent<components::UIProgressBarComponent>())
            out["uiProgressBar"] = serializeUIProgressBar(entity.getComponent<components::UIProgressBarComponent>());
    }

    void SceneSerialization::serializeMiscComponents(scene::Entity& entity, json& out)
    {
        if (entity.hasComponent<components::SocketAttachmentComponent>())
            out["socketAttachment"] = serializeSocketAttachment(entity.getComponent<components::SocketAttachmentComponent>());
        if (entity.hasComponent<components::SocketOverrideComponent>())
            out["socketOverride"] = serializeSocketOverride(entity.getComponent<components::SocketOverrideComponent>());
        if (entity.hasComponent<components::NavmeshAgentComponent>())
            out["navmeshAgent"] = serializeNavmeshAgent(entity.getComponent<components::NavmeshAgentComponent>());
        if (entity.hasComponent<components::NavmeshComponent>())
            out["navmesh"] = serializeNavmesh(entity.getComponent<components::NavmeshComponent>());
        if (entity.hasComponent<components::OffMeshLinkComponent>())
            out["offMeshLink"] = serializeOffMeshLink(entity.getComponent<components::OffMeshLinkComponent>());
        if (entity.hasComponent<components::RenderTextureComponent>())
            out["renderTexture"] = serializeRenderTexture(entity.getComponent<components::RenderTextureComponent>());
        if (entity.hasComponent<components::ControllerComponent>())
            out["controller"] = serializeController(entity.getComponent<components::ControllerComponent>());
        if (entity.hasComponent<components::IKTargetComponent>())
            out["ikTarget"] = serializeIKTarget(entity.getComponent<components::IKTargetComponent>());
        if (entity.hasComponent<components::BehaviorTreeComponent>())
            out["behaviorTree"] = serializeBehaviorTree(entity.getComponent<components::BehaviorTreeComponent>());
        if (entity.hasComponent<components::DecalComponent>())
            out["decal"] = serializeDecal(entity.getComponent<components::DecalComponent>());
    }

    json SceneSerialization::serializeEntityComponents(scene::Entity& entity)
    {
        json componentsJson = json::object();

        serializeRenderComponents(entity, componentsJson);
        serializeAudioPhysicsComponents(entity, componentsJson);
        serializeLightEnvironmentComponents(entity, componentsJson);
        serializeUIStructuralComponents(entity, componentsJson);
        serializeUIInteractiveComponents(entity, componentsJson);
        serializeMiscComponents(entity, componentsJson);

        // Plugin components
        {
            std::shared_lock lock(pluginHookMutex);
            if (pluginSerializeHook)
            {
                auto pluginJson = pluginSerializeHook(entity);
                for (auto& [key, value] : pluginJson.items())
                {
                    componentsJson[key] = std::move(value);
                }
            }
        }

        return componentsJson;
    }

    void SceneSerialization::deserializeSceneSettings(const json& sceneJson, scene::SceneGraphSystem& sceneGraph)
    {
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
    }
}
