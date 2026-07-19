#include "SceneSerialization.hpp"
#include "JsonConverters.hpp"
#include "../scene/SceneGraphSystem.hpp"
#include "../components/Components.hpp"

namespace serialization
{
    void SceneSerialization::deserializeRenderComponents(const json& c, scene::Entity& entity)
    {
        if (c.contains("camera"))
        {
            auto& camera = entity.addOrReplaceComponent<components::CameraComponent>();
            deserializeCamera(c["camera"], camera);
            if (!c.contains("billboard"))
            {
                auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
                billboard.iconType = components::BillboardIconType::Camera;
            }
        }
        if (c.contains("ibl"))
        {
            auto iblRef = deserializeIBLRef(c["ibl"]);
            if (iblRef.isValid())
            {
                auto& ibl = entity.addOrReplaceComponent<components::IBLComponent>();
                ibl.hdrRef = iblRef;
                deserializeIBLParams(c["ibl"], ibl); // VK-1574 knobs
            }
        }
        if (c.contains("worldSector"))
        {
            const auto& wsJson = c["worldSector"];
            if (wsJson.contains("worldFilePath") && wsJson["worldFilePath"].is_string())
                entity.addOrReplaceComponent<components::WorldSectorComponent>().worldFilePath =
                    wsJson["worldFilePath"].get<std::string>();
        }
        if (c.contains("mesh"))
        {
            auto& meshComp = entity.addOrReplaceComponent<components::MeshComponent>();
            deserializeMesh(c["mesh"], meshComp);
        }
        if (c.contains("material"))
        {
            auto& matComp = entity.addOrReplaceComponent<components::MaterialComponent>();
            deserializeMaterial(c["material"], matComp);
        }
        if (c.contains("billboard"))
        {
            auto& billboardComp = entity.addOrReplaceComponent<components::BillboardComponent>();
            deserializeBillboard(c["billboard"], billboardComp);
        }
        if (c.contains("text"))
        {
            auto& textComp = entity.addOrReplaceComponent<components::TextComponent>();
            deserializeText(c["text"], textComp);
        }
    }

    void SceneSerialization::deserializeAudioComponents(const json& c, scene::Entity& entity)
    {
        if (c.contains("audioSource2D"))
        {
            auto& audioComp = entity.addOrReplaceComponent<components::AudioSource2DComponent>();
            deserializeAudioSource2D(c["audioSource2D"], audioComp);
            if (!c.contains("billboard"))
            {
                auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
                billboard.iconType = components::BillboardIconType::Audio2D;
            }
        }
        if (c.contains("audioSource3D"))
        {
            auto& audioComp = entity.addOrReplaceComponent<components::AudioSource3DComponent>();
            deserializeAudioSource3D(c["audioSource3D"], audioComp);
            if (!c.contains("billboard"))
            {
                auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
                billboard.iconType = components::BillboardIconType::Audio3D;
            }
        }
        if (c.contains("reverbZone"))
        {
            auto& reverbComp = entity.addOrReplaceComponent<components::ReverbZoneComponent>();
            deserializeReverbZone(c["reverbZone"], reverbComp);
        }
        if (c.contains("fogVolume"))
        {
            auto& fogComp = entity.addOrReplaceComponent<components::FogVolumeComponent>();
            deserializeFogVolume(c["fogVolume"], fogComp);
        }
        if (c.contains("reflectionProbe"))
        {
            auto& probeComp = entity.addOrReplaceComponent<components::ReflectionProbeComponent>();
            deserializeReflectionProbe(c["reflectionProbe"], probeComp);
        }
        if (c.contains("weatherZone"))
        {
            auto& weatherZoneComp = entity.addOrReplaceComponent<components::WeatherZoneComponent>();
            deserializeWeatherZone(c["weatherZone"], weatherZoneComp);
        }
    }

    void SceneSerialization::deserializePhysicsComponents(const json& c, scene::Entity& entity)
    {
        if (c.contains("script"))
        {
            auto& scriptComp = entity.addOrReplaceComponent<components::ScriptComponent>();
            deserializeScript(c["script"], scriptComp);
        }
        if (c.contains("collider"))
        {
            auto& colliderComp = entity.addOrReplaceComponent<components::ColliderComponent>();
            deserializeCollider(c["collider"], colliderComp);
        }
        if (c.contains("rigidBody"))
        {
            auto& rigidBodyComp = entity.addOrReplaceComponent<components::RigidBodyComponent>();
            deserializeRigidBody(c["rigidBody"], rigidBodyComp);
        }
        if (c.contains("vehicle"))
        {
            auto& vehicleComp = entity.addOrReplaceComponent<components::VehicleComponent>();
            deserializeVehicle(c["vehicle"], vehicleComp);
        }
        if (c.contains("buoyancy"))
        {
            auto& buoyancyComp = entity.addOrReplaceComponent<components::BuoyancyComponent>();
            deserializeBuoyancy(c["buoyancy"], buoyancyComp);
        }
        if (c.contains("destructible"))
        {
            auto& destructibleComp = entity.addOrReplaceComponent<components::DestructibleComponent>();
            deserializeDestructible(c["destructible"], destructibleComp);
        }
        if (c.contains("physicsAnimation"))
        {
            auto& physAnimComp = entity.addOrReplaceComponent<components::PhysicsAnimationComponent>();
            deserializePhysicsAnimation(c["physicsAnimation"], physAnimComp);
        }
        if (c.contains("vfx"))
        {
            auto& vfxComp = entity.addOrReplaceComponent<components::VFXComponent>();
            deserializeVFX(c["vfx"], vfxComp);
            if (!c.contains("billboard"))
            {
                auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
                billboard.iconType = components::BillboardIconType::Particle;
            }
        }
        if (c.contains("vfxSequence"))
        {
            auto& comp = entity.addOrReplaceComponent<components::VFXSequenceComponent>();
            deserializeVFXSequence(c["vfxSequence"], comp);
        }
    }

    void SceneSerialization::deserializeLightComponents(const json& c, scene::Entity& entity)
    {
        if (c.contains("directionalLight"))
        {
            auto& lightComp = entity.addOrReplaceComponent<components::DirectionalLightComponent>();
            deserializeDirectionalLight(c["directionalLight"], lightComp);
            if (!c.contains("billboard"))
            {
                auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
                billboard.iconType = components::BillboardIconType::DirectionalLight;
            }
        }
        if (c.contains("pointLight"))
        {
            auto& lightComp = entity.addOrReplaceComponent<components::PointLightComponent>();
            deserializePointLight(c["pointLight"], lightComp);
            if (!c.contains("billboard"))
            {
                auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
                billboard.iconType = components::BillboardIconType::PointLight;
            }
        }
        if (c.contains("spotLight"))
        {
            auto& lightComp = entity.addOrReplaceComponent<components::SpotLightComponent>();
            deserializeSpotLight(c["spotLight"], lightComp);
            if (!c.contains("billboard"))
            {
                auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
                billboard.iconType = components::BillboardIconType::SpotLight;
            }
        }
    }

    void SceneSerialization::deserializeEnvironmentComponents(const json& c, scene::Entity& entity)
    {
        if (c.contains("terrain"))
        {
            auto& terrainComp = entity.addOrReplaceComponent<components::TerrainComponent>();
            deserializeTerrain(c["terrain"], terrainComp);
        }
        if (c.contains("terrainTile"))
        {
            auto& tileComp = entity.addOrReplaceComponent<components::TerrainTileComponent>();
            deserializeTerrainTile(c["terrainTile"], tileComp);
        }
        if (c.contains("grass"))
        {
            auto& grassComp = entity.addOrReplaceComponent<components::GrassComponent>();
            deserializeGrass(c["grass"], grassComp);
        }
        if (c.contains("ocean"))
        {
            auto& oceanComp = entity.addOrReplaceComponent<components::OceanComponent>();
            deserializeOcean(c["ocean"], oceanComp);
        }
    }

    void SceneSerialization::deserializeUIStructuralComponents(const json& c, scene::Entity& entity)
    {
        if (c.contains("uiCanvas"))
        {
            auto& comp = entity.addOrReplaceComponent<components::UICanvasComponent>();
            deserializeUICanvas(c["uiCanvas"], comp);
        }
        if (c.contains("uiRect"))
        {
            auto& comp = entity.addOrReplaceComponent<components::UIRectComponent>();
            deserializeUIRect(c["uiRect"], comp);
        }
        if (c.contains("uiStyle"))
        {
            auto& comp = entity.addOrReplaceComponent<components::UIStyleComponent>();
            deserializeUIStyle(c["uiStyle"], comp);
        }
        if (c.contains("uiImage"))
        {
            auto& comp = entity.addOrReplaceComponent<components::UIImageComponent>();
            deserializeUIImage(c["uiImage"], comp);
        }
        if (c.contains("uiScroll"))
        {
            auto& comp = entity.addOrReplaceComponent<components::UIScrollComponent>();
            deserializeUIScroll(c["uiScroll"], comp);
        }
        if (c.contains("uiLayoutGroup"))
        {
            auto& comp = entity.addOrReplaceComponent<components::UILayoutGroupComponent>();
            deserializeUILayoutGroup(c["uiLayoutGroup"], comp);
        }
        if (c.contains("uiLabel"))
        {
            auto& comp = entity.addOrReplaceComponent<components::UILabelComponent>();
            deserializeUILabel(c["uiLabel"], comp);
        }
        if (c.contains("uiTooltip"))
        {
            auto& comp = entity.addOrReplaceComponent<components::UITooltipComponent>();
            deserializeUITooltip(c["uiTooltip"], comp);
        }
        if (c.contains("uiWindow"))
        {
            auto& comp = entity.addOrReplaceComponent<components::UIWindowComponent>();
            deserializeUIWindow(c["uiWindow"], comp);
        }
        if (c.contains("uiListView"))
        {
            auto& comp = entity.addOrReplaceComponent<components::UIListViewComponent>();
            deserializeUIListView(c["uiListView"], comp);
        }
        if (c.contains("uiAnimation"))
        {
            auto& comp = entity.addOrReplaceComponent<components::UIAnimationComponent>();
            deserializeUIAnimation(c["uiAnimation"], comp);
        }
        if (c.contains("uiMask"))
        {
            auto& comp = entity.addOrReplaceComponent<components::UIMaskComponent>();
            deserializeUIMask(c["uiMask"], comp);
        }
        if (c.contains("uiDraggable"))
        {
            auto& comp = entity.addOrReplaceComponent<components::UIDraggableComponent>();
            deserializeUIDraggable(c["uiDraggable"], comp);
        }
        if (c.contains("uiDropTarget"))
        {
            auto& comp = entity.addOrReplaceComponent<components::UIDropTargetComponent>();
            deserializeUIDropTarget(c["uiDropTarget"], comp);
        }
    }

    void SceneSerialization::deserializeUIInteractiveComponents(const json& c, scene::Entity& entity)
    {
        if (c.contains("uiButton"))
        {
            auto& comp = entity.addOrReplaceComponent<components::UIButtonComponent>();
            deserializeUIButton(c["uiButton"], comp);
        }
        if (c.contains("uiTextInput"))
        {
            auto& comp = entity.addOrReplaceComponent<components::UITextInputComponent>();
            deserializeUITextInput(c["uiTextInput"], comp);
        }
        if (c.contains("uiCheckbox"))
        {
            auto& comp = entity.addOrReplaceComponent<components::UICheckboxComponent>();
            deserializeUICheckbox(c["uiCheckbox"], comp);
        }
        if (c.contains("uiDropdown"))
        {
            auto& comp = entity.addOrReplaceComponent<components::UIDropdownComponent>();
            deserializeUIDropdown(c["uiDropdown"], comp);
        }
        if (c.contains("uiTabs"))
        {
            auto& comp = entity.addOrReplaceComponent<components::UITabsComponent>();
            deserializeUITabs(c["uiTabs"], comp);
        }
        if (c.contains("uiSlider"))
        {
            auto& comp = entity.addOrReplaceComponent<components::UISliderComponent>();
            deserializeUISlider(c["uiSlider"], comp);
        }
        if (c.contains("uiProgressBar"))
        {
            auto& comp = entity.addOrReplaceComponent<components::UIProgressBarComponent>();
            deserializeUIProgressBar(c["uiProgressBar"], comp);
        }
    }

    void SceneSerialization::deserializeMiscComponents(const json& c, scene::Entity& entity)
    {
        if (c.contains("socketAttachment"))
        {
            auto& attachment = entity.addOrReplaceComponent<components::SocketAttachmentComponent>();
            deserializeSocketAttachment(c["socketAttachment"], attachment);
        }
        if (c.contains("socketOverride"))
        {
            auto& comp = entity.addOrReplaceComponent<components::SocketOverrideComponent>();
            deserializeSocketOverride(c["socketOverride"], comp);
        }
        if (c.contains("navmeshAgent"))
        {
            auto& comp = entity.addOrReplaceComponent<components::NavmeshAgentComponent>();
            deserializeNavmeshAgent(c["navmeshAgent"], comp);
        }
        if (c.contains("navmesh"))
        {
            auto& comp = entity.addOrReplaceComponent<components::NavmeshComponent>();
            deserializeNavmesh(c["navmesh"], comp);
        }
        if (c.contains("offMeshLink"))
        {
            auto& comp = entity.addOrReplaceComponent<components::OffMeshLinkComponent>();
            deserializeOffMeshLink(c["offMeshLink"], comp);
        }
        if (c.contains("navmeshObstacle"))
        {
            auto& comp = entity.addOrReplaceComponent<components::NavmeshObstacleComponent>();
            deserializeNavmeshObstacle(c["navmeshObstacle"], comp);
        }
        if (c.contains("navmeshModifierVolume"))
        {
            auto& comp = entity.addOrReplaceComponent<components::NavmeshModifierVolumeComponent>();
            deserializeNavmeshModifierVolume(c["navmeshModifierVolume"], comp);
        }
        if (c.contains("navInvoker"))
        {
            auto& comp = entity.addOrReplaceComponent<components::NavInvokerComponent>();
            deserializeNavInvoker(c["navInvoker"], comp);
        }
        if (c.contains("renderTexture"))
        {
            auto& comp = entity.addOrReplaceComponent<components::RenderTextureComponent>();
            deserializeRenderTexture(c["renderTexture"], comp);
        }
        if (c.contains("controller"))
        {
            auto& comp = entity.addOrReplaceComponent<components::ControllerComponent>();
            deserializeController(c["controller"], comp);
        }
        if (c.contains("ikTarget"))
        {
            auto& comp = entity.addOrReplaceComponent<components::IKTargetComponent>();
            deserializeIKTarget(c["ikTarget"], comp);
        }
        if (c.contains("behaviorTree"))
        {
            auto& comp = entity.addOrReplaceComponent<components::BehaviorTreeComponent>();
            deserializeBehaviorTree(c["behaviorTree"], comp);
        }
        if (c.contains("decal"))
        {
            auto& comp = entity.addOrReplaceComponent<components::DecalComponent>();
            deserializeDecal(c["decal"], comp);
        }
        if (c.contains("volumetricNavVolume"))
        {
            auto& comp = entity.addOrReplaceComponent<components::VolumetricNavVolumeComponent>();
            deserializeVolumetricNavVolume(c["volumetricNavVolume"], comp);
        }
        if (c.contains("volumetricAgent"))
        {
            auto& comp = entity.addOrReplaceComponent<components::VolumetricAgentComponent>();
            deserializeVolumetricAgent(c["volumetricAgent"], comp);
        }
        if (c.contains("prefabInstance"))
        {
            entity.addOrReplaceComponent<components::PrefabInstanceComponent>().sourcePrefabPath =
                c["prefabInstance"].value("sourcePrefabPath", std::string(""));
        }
    }

    void SceneSerialization::deserializeEntityComponents(const json& componentsJson, scene::Entity& entity)
    {
        deserializeRenderComponents(componentsJson, entity);
        deserializeAudioComponents(componentsJson, entity);
        deserializePhysicsComponents(componentsJson, entity);
        deserializeLightComponents(componentsJson, entity);
        deserializeEnvironmentComponents(componentsJson, entity);
        deserializeUIStructuralComponents(componentsJson, entity);
        deserializeUIInteractiveComponents(componentsJson, entity);
        deserializeMiscComponents(componentsJson, entity);

        // Plugin components
        if (pluginDeserializeHook)
        {
            pluginDeserializeHook(componentsJson,
                scene::EntityRegistry::getRegistry(), entity.getHandle());
        }
    }
}
