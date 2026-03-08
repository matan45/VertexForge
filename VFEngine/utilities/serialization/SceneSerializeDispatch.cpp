#include "SceneSerialization.hpp"
#include "JsonConverters.hpp"
#include "../scene/SceneGraphSystem.hpp"
#include "../components/Components.hpp"

namespace serialization
{
    json SceneSerialization::serializeEntityComponents(scene::Entity& entity)
    {
        json componentsJson = json::object();

        if (entity.hasComponent<components::CameraComponent>())
        {
            componentsJson["camera"] = serializeCamera(entity.getComponent<components::CameraComponent>());
        }

        if (entity.hasComponent<components::IBLComponent>())
        {
            componentsJson["ibl"] = serializeIBL(entity.getComponent<components::IBLComponent>());
        }

        if (entity.hasComponent<components::WorldSectorComponent>())
        {
            json wsJson;
            wsJson["worldFilePath"] = entity.getComponent<components::WorldSectorComponent>().worldFilePath;
            componentsJson["worldSector"] = wsJson;
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

        if (entity.hasComponent<components::TextComponent>())
        {
            componentsJson["text"] = serializeText(entity.getComponent<components::TextComponent>());
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

        if (entity.hasComponent<components::PhysicsAnimationComponent>())
        {
            componentsJson["physicsAnimation"] = serializePhysicsAnimation(
                entity.getComponent<components::PhysicsAnimationComponent>());
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

        if (entity.hasComponent<components::LightmapComponent>())
        {
            componentsJson["lightmap"] = serializeLightmap(
                entity.getComponent<components::LightmapComponent>());
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

        if (entity.hasComponent<components::GrassComponent>())
        {
            componentsJson["grass"] = serializeGrass(
                entity.getComponent<components::GrassComponent>());
        }

        if (entity.hasComponent<components::WaterComponent>())
        {
            componentsJson["water"] = serializeWater(
                entity.getComponent<components::WaterComponent>());
        }

        if (entity.hasComponent<components::WaterTileComponent>())
        {
            componentsJson["waterTile"] = serializeWaterTile(
                entity.getComponent<components::WaterTileComponent>());
        }

        if (entity.hasComponent<components::UICanvasComponent>())
        {
            componentsJson["uiCanvas"] = serializeUICanvas(
                entity.getComponent<components::UICanvasComponent>());
        }

        if (entity.hasComponent<components::UIRectComponent>())
        {
            componentsJson["uiRect"] = serializeUIRect(
                entity.getComponent<components::UIRectComponent>());
        }

        if (entity.hasComponent<components::UIImageComponent>())
        {
            componentsJson["uiImage"] = serializeUIImage(
                entity.getComponent<components::UIImageComponent>());
        }

        if (entity.hasComponent<components::UIScrollComponent>())
        {
            componentsJson["uiScroll"] = serializeUIScroll(
                entity.getComponent<components::UIScrollComponent>());
        }

        if (entity.hasComponent<components::UILayoutGroupComponent>())
        {
            componentsJson["uiLayoutGroup"] = serializeUILayoutGroup(
                entity.getComponent<components::UILayoutGroupComponent>());
        }

        if (entity.hasComponent<components::UILabelComponent>())
        {
            componentsJson["uiLabel"] = serializeUILabel(
                entity.getComponent<components::UILabelComponent>());
        }

        if (entity.hasComponent<components::UIButtonComponent>())
        {
            componentsJson["uiButton"] = serializeUIButton(
                entity.getComponent<components::UIButtonComponent>());
        }

        if (entity.hasComponent<components::UITextInputComponent>())
        {
            componentsJson["uiTextInput"] = serializeUITextInput(
                entity.getComponent<components::UITextInputComponent>());
        }

        if (entity.hasComponent<components::UICheckboxComponent>())
        {
            componentsJson["uiCheckbox"] = serializeUICheckbox(
                entity.getComponent<components::UICheckboxComponent>());
        }

        if (entity.hasComponent<components::UIDropdownComponent>())
        {
            componentsJson["uiDropdown"] = serializeUIDropdown(
                entity.getComponent<components::UIDropdownComponent>());
        }

        if (entity.hasComponent<components::UITabsComponent>())
        {
            componentsJson["uiTabs"] = serializeUITabs(
                entity.getComponent<components::UITabsComponent>());
        }

        if (entity.hasComponent<components::UISliderComponent>())
        {
            componentsJson["uiSlider"] = serializeUISlider(
                entity.getComponent<components::UISliderComponent>());
        }

        if (entity.hasComponent<components::UIProgressBarComponent>())
        {
            componentsJson["uiProgressBar"] = serializeUIProgressBar(
                entity.getComponent<components::UIProgressBarComponent>());
        }

        if (entity.hasComponent<components::SocketAttachmentComponent>())
        {
            componentsJson["socketAttachment"] = serializeSocketAttachment(
                entity.getComponent<components::SocketAttachmentComponent>());
        }

        if (entity.hasComponent<components::SocketOverrideComponent>())
        {
            componentsJson["socketOverride"] = serializeSocketOverride(
                entity.getComponent<components::SocketOverrideComponent>());
        }

        if (entity.hasComponent<components::NavmeshAgentComponent>())
        {
            componentsJson["navmeshAgent"] = serializeNavmeshAgent(
                entity.getComponent<components::NavmeshAgentComponent>());
        }

        if (entity.hasComponent<components::NavmeshComponent>())
        {
            componentsJson["navmesh"] = serializeNavmesh(
                entity.getComponent<components::NavmeshComponent>());
        }

        if (entity.hasComponent<components::RenderTextureComponent>())
        {
            componentsJson["renderTexture"] = serializeRenderTexture(
                entity.getComponent<components::RenderTextureComponent>());
        }

        if (entity.hasComponent<components::ControllerComponent>())
        {
            componentsJson["controller"] = serializeController(
                entity.getComponent<components::ControllerComponent>());
        }

        if (entity.hasComponent<components::IKTargetComponent>())
        {
            componentsJson["ikTarget"] = serializeIKTarget(
                entity.getComponent<components::IKTargetComponent>());
        }

        return componentsJson;
    }

    void SceneSerialization::deserializeEntityComponents(const json& componentsJson, scene::Entity& entity)
    {
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

        if (componentsJson.contains("worldSector"))
        {
            const auto& wsJson = componentsJson["worldSector"];
            if (wsJson.contains("worldFilePath") && wsJson["worldFilePath"].is_string())
            {
                entity.addOrReplaceComponent<components::WorldSectorComponent>().worldFilePath =
                    wsJson["worldFilePath"].get<std::string>();
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

        if (componentsJson.contains("text"))
        {
            auto& textComp = entity.addOrReplaceComponent<components::TextComponent>();
            deserializeText(componentsJson["text"], textComp);
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

        if (componentsJson.contains("physicsAnimation"))
        {
            auto& physAnimComp = entity.addOrReplaceComponent<components::PhysicsAnimationComponent>();
            deserializePhysicsAnimation(componentsJson["physicsAnimation"], physAnimComp);
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

        if (componentsJson.contains("lightmap"))
        {
            auto& lmComp = entity.addOrReplaceComponent<components::LightmapComponent>();
            deserializeLightmap(componentsJson["lightmap"], lmComp);
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

        if (componentsJson.contains("grass"))
        {
            auto& grassComp = entity.addOrReplaceComponent<components::GrassComponent>();
            deserializeGrass(componentsJson["grass"], grassComp);
        }

        if (componentsJson.contains("water"))
        {
            auto& waterComp = entity.addOrReplaceComponent<components::WaterComponent>();
            deserializeWater(componentsJson["water"], waterComp);
        }

        if (componentsJson.contains("waterTile"))
        {
            auto& waterTileComp = entity.addOrReplaceComponent<components::WaterTileComponent>();
            deserializeWaterTile(componentsJson["waterTile"], waterTileComp);
        }

        if (componentsJson.contains("uiCanvas"))
        {
            auto& canvasComp = entity.addOrReplaceComponent<components::UICanvasComponent>();
            deserializeUICanvas(componentsJson["uiCanvas"], canvasComp);
        }

        if (componentsJson.contains("uiRect"))
        {
            auto& rectComp = entity.addOrReplaceComponent<components::UIRectComponent>();
            deserializeUIRect(componentsJson["uiRect"], rectComp);
        }

        if (componentsJson.contains("uiImage"))
        {
            auto& imageComp = entity.addOrReplaceComponent<components::UIImageComponent>();
            deserializeUIImage(componentsJson["uiImage"], imageComp);
        }

        if (componentsJson.contains("uiScroll"))
        {
            auto& scrollComp = entity.addOrReplaceComponent<components::UIScrollComponent>();
            deserializeUIScroll(componentsJson["uiScroll"], scrollComp);
        }

        if (componentsJson.contains("uiLayoutGroup"))
        {
            auto& layoutGroupComp = entity.addOrReplaceComponent<components::UILayoutGroupComponent>();
            deserializeUILayoutGroup(componentsJson["uiLayoutGroup"], layoutGroupComp);
        }

        if (componentsJson.contains("uiLabel"))
        {
            auto& labelComp = entity.addOrReplaceComponent<components::UILabelComponent>();
            deserializeUILabel(componentsJson["uiLabel"], labelComp);
        }

        if (componentsJson.contains("uiButton"))
        {
            auto& buttonComp = entity.addOrReplaceComponent<components::UIButtonComponent>();
            deserializeUIButton(componentsJson["uiButton"], buttonComp);
        }

        if (componentsJson.contains("uiTextInput"))
        {
            auto& textInputComp = entity.addOrReplaceComponent<components::UITextInputComponent>();
            deserializeUITextInput(componentsJson["uiTextInput"], textInputComp);
        }

        if (componentsJson.contains("uiCheckbox"))
        {
            auto& checkboxComp = entity.addOrReplaceComponent<components::UICheckboxComponent>();
            deserializeUICheckbox(componentsJson["uiCheckbox"], checkboxComp);
        }

        if (componentsJson.contains("uiDropdown"))
        {
            auto& dropdownComp = entity.addOrReplaceComponent<components::UIDropdownComponent>();
            deserializeUIDropdown(componentsJson["uiDropdown"], dropdownComp);
        }

        if (componentsJson.contains("uiTabs"))
        {
            auto& tabsComp = entity.addOrReplaceComponent<components::UITabsComponent>();
            deserializeUITabs(componentsJson["uiTabs"], tabsComp);
        }

        if (componentsJson.contains("uiSlider"))
        {
            auto& sliderComp = entity.addOrReplaceComponent<components::UISliderComponent>();
            deserializeUISlider(componentsJson["uiSlider"], sliderComp);
        }

        if (componentsJson.contains("uiProgressBar"))
        {
            auto& pbComp = entity.addOrReplaceComponent<components::UIProgressBarComponent>();
            deserializeUIProgressBar(componentsJson["uiProgressBar"], pbComp);
        }

        if (componentsJson.contains("socketAttachment"))
        {
            auto& attachment = entity.addOrReplaceComponent<components::SocketAttachmentComponent>();
            deserializeSocketAttachment(componentsJson["socketAttachment"], attachment);
        }

        if (componentsJson.contains("socketOverride"))
        {
            auto& override = entity.addOrReplaceComponent<components::SocketOverrideComponent>();
            deserializeSocketOverride(componentsJson["socketOverride"], override);
        }

        if (componentsJson.contains("navmeshAgent"))
        {
            auto& agentComp = entity.addOrReplaceComponent<components::NavmeshAgentComponent>();
            deserializeNavmeshAgent(componentsJson["navmeshAgent"], agentComp);
        }

        if (componentsJson.contains("navmesh"))
        {
            auto& navmeshComp = entity.addOrReplaceComponent<components::NavmeshComponent>();
            deserializeNavmesh(componentsJson["navmesh"], navmeshComp);
        }

        if (componentsJson.contains("renderTexture"))
        {
            auto& rttComp = entity.addOrReplaceComponent<components::RenderTextureComponent>();
            deserializeRenderTexture(componentsJson["renderTexture"], rttComp);
        }

        if (componentsJson.contains("controller"))
        {
            auto& controllerComp = entity.addOrReplaceComponent<components::ControllerComponent>();
            deserializeController(componentsJson["controller"], controllerComp);
        }

        if (componentsJson.contains("ikTarget"))
        {
            auto& ikTargetComp = entity.addOrReplaceComponent<components::IKTargetComponent>();
            deserializeIKTarget(componentsJson["ikTarget"], ikTargetComp);
        }
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
