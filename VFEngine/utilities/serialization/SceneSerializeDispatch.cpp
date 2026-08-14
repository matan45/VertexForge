#include "SceneSerialization.hpp"
#include "JsonConverters.hpp"
#include "../scene/SceneGraphSystem.hpp"
#include "../components/Components.hpp"
#include "../asset/AssetDatabase.hpp"
#include "../asset/AssetRef.hpp"
#include "../asset/AssetMetadataSerializer.hpp"
#include "../print/Log.hpp"
#include "SerializationFileAccess.hpp"
#include <chrono>
#include <iomanip>
#include <fstream>
#include <sstream>

namespace serialization
{
    SceneSerialization::PluginSerializeFn SceneSerialization::pluginSerializeHook;
    SceneSerialization::PluginDeserializeFn SceneSerialization::pluginDeserializeHook;

    void SceneSerialization::setPluginSerializationHooks(PluginSerializeFn serialize, PluginDeserializeFn deserialize)
    {
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
        if (entity.hasComponent<components::ReflectionProbeComponent>())
            out["reflectionProbe"] = serializeReflectionProbe(entity.getComponent<components::ReflectionProbeComponent>());
        if (entity.hasComponent<components::WeatherZoneComponent>())
            out["weatherZone"] = serializeWeatherZone(entity.getComponent<components::WeatherZoneComponent>());
        if (entity.hasComponent<components::ScriptComponent>())
            out["script"] = serializeScript(entity.getComponent<components::ScriptComponent>());
        if (entity.hasComponent<components::ColliderComponent>())
            out["collider"] = serializeCollider(entity.getComponent<components::ColliderComponent>());
        if (entity.hasComponent<components::RigidBodyComponent>())
            out["rigidBody"] = serializeRigidBody(entity.getComponent<components::RigidBodyComponent>());
        if (entity.hasComponent<components::VehicleComponent>())
            out["vehicle"] = serializeVehicle(entity.getComponent<components::VehicleComponent>());
        if (entity.hasComponent<components::BuoyancyComponent>())
            out["buoyancy"] = serializeBuoyancy(entity.getComponent<components::BuoyancyComponent>());
        if (entity.hasComponent<components::WaterWakeEmitterComponent>())
            out["waterWakeEmitter"] = serializeWaterWakeEmitter(entity.getComponent<components::WaterWakeEmitterComponent>());
        if (entity.hasComponent<components::DestructibleComponent>())
            out["destructible"] = serializeDestructible(entity.getComponent<components::DestructibleComponent>());
        if (entity.hasComponent<components::PhysicsAnimationComponent>())
            out["physicsAnimation"] = serializePhysicsAnimation(entity.getComponent<components::PhysicsAnimationComponent>());
        if (entity.hasComponent<components::VFXComponent>())
            out["vfx"] = serializeVFX(entity.getComponent<components::VFXComponent>());
        if (entity.hasComponent<components::VFXSequenceComponent>())
            out["vfxSequence"] = serializeVFXSequence(entity.getComponent<components::VFXSequenceComponent>());
    }

    void SceneSerialization::serializeLightEnvironmentComponents(scene::Entity& entity, json& out,
                                                                  std::string_view sourceFilename)
    {
        if (entity.hasComponent<components::DirectionalLightComponent>())
            out["directionalLight"] = serializeDirectionalLight(entity.getComponent<components::DirectionalLightComponent>());
        if (entity.hasComponent<components::PointLightComponent>())
            out["pointLight"] = serializePointLight(entity.getComponent<components::PointLightComponent>());
        if (entity.hasComponent<components::SpotLightComponent>())
            out["spotLight"] = serializeSpotLight(entity.getComponent<components::SpotLightComponent>());
        if (entity.hasComponent<components::TerrainComponent>())
            out["terrain"] = serializeTerrain(entity.getComponent<components::TerrainComponent>(),
                                              sourceFilename);
        if (entity.hasComponent<components::TerrainTileComponent>())
            out["terrainTile"] = serializeTerrainTile(entity.getComponent<components::TerrainTileComponent>());
        if (entity.hasComponent<components::GrassComponent>())
            out["grass"] = serializeGrass(entity.getComponent<components::GrassComponent>());
        if (entity.hasComponent<components::OceanComponent>())
            out["ocean"] = serializeOcean(entity.getComponent<components::OceanComponent>());
        if (entity.hasComponent<components::WaterBodyComponent>())   // VK-1607
            out["waterBody"] = serializeWaterBody(entity.getComponent<components::WaterBodyComponent>());
        if (entity.hasComponent<components::MeshBrushInstanceComponent>())
            out["meshBrushInstance"] = serializeMeshBrushInstance(entity.getComponent<components::MeshBrushInstanceComponent>());
        if (entity.hasComponent<components::RoadSplineComponent>())   // VK-1621
            out["roadSpline"] = serializeRoadSpline(entity.getComponent<components::RoadSplineComponent>());
    }

    void SceneSerialization::serializeUIStructuralComponents(scene::Entity& entity, json& out)
    {
        if (entity.hasComponent<components::UICanvasComponent>())
            out["uiCanvas"] = serializeUICanvas(entity.getComponent<components::UICanvasComponent>());
        if (entity.hasComponent<components::UIRectComponent>())
            out["uiRect"] = serializeUIRect(entity.getComponent<components::UIRectComponent>());
        if (entity.hasComponent<components::UIStyleComponent>())
            out["uiStyle"] = serializeUIStyle(entity.getComponent<components::UIStyleComponent>());
        if (entity.hasComponent<components::UIImageComponent>())
            out["uiImage"] = serializeUIImage(entity.getComponent<components::UIImageComponent>());
        if (entity.hasComponent<components::UIScrollComponent>())
            out["uiScroll"] = serializeUIScroll(entity.getComponent<components::UIScrollComponent>());
        if (entity.hasComponent<components::UILayoutGroupComponent>())
            out["uiLayoutGroup"] = serializeUILayoutGroup(entity.getComponent<components::UILayoutGroupComponent>());
        if (entity.hasComponent<components::UILabelComponent>())
            out["uiLabel"] = serializeUILabel(entity.getComponent<components::UILabelComponent>());
        if (entity.hasComponent<components::UITooltipComponent>())
            out["uiTooltip"] = serializeUITooltip(entity.getComponent<components::UITooltipComponent>());
        if (entity.hasComponent<components::UIWindowComponent>())
            out["uiWindow"] = serializeUIWindow(entity.getComponent<components::UIWindowComponent>());
        if (entity.hasComponent<components::UIListViewComponent>())
            out["uiListView"] = serializeUIListView(entity.getComponent<components::UIListViewComponent>());
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
        if (entity.hasComponent<components::NavmeshObstacleComponent>())
            out["navmeshObstacle"] = serializeNavmeshObstacle(entity.getComponent<components::NavmeshObstacleComponent>());
        if (entity.hasComponent<components::NavmeshModifierVolumeComponent>())
            out["navmeshModifierVolume"] = serializeNavmeshModifierVolume(entity.getComponent<components::NavmeshModifierVolumeComponent>());
        if (entity.hasComponent<components::NavInvokerComponent>())
            out["navInvoker"] = serializeNavInvoker(entity.getComponent<components::NavInvokerComponent>());
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
        if (entity.hasComponent<components::VolumetricNavVolumeComponent>())
            out["volumetricNavVolume"] = serializeVolumetricNavVolume(entity.getComponent<components::VolumetricNavVolumeComponent>());
        if (entity.hasComponent<components::VolumetricAgentComponent>())
            out["volumetricAgent"] = serializeVolumetricAgent(entity.getComponent<components::VolumetricAgentComponent>());
        // VK-1597: written only when the entity actually carries the component - absence is the
        // "spatially loaded" default, so untouched entities keep their exact current payload.
        if (entity.hasComponent<components::StreamingPolicyComponent>())
        {
            const auto& policy = entity.getComponent<components::StreamingPolicyComponent>();
            out["streamingPolicy"] = {
                {"spatiallyLoaded", policy.spatiallyLoaded}
            };
            // VK-1599: written only when non-zero. Grid 0 is the default for every entity that has
            // never been assigned, so a scene that predates named grids stays byte-identical.
            if (policy.gridIndex != 0)
                out["streamingPolicy"]["gridIndex"] = policy.gridIndex;
        }
        if (entity.hasComponent<components::PrefabInstanceComponent>())
            out["prefabInstance"] = {
                {"sourcePrefabPath", entity.getComponent<components::PrefabInstanceComponent>().sourcePrefabPath}
            };
    }

    json SceneSerialization::serializeEntityComponents(scene::Entity& entity,
                                                        std::string_view sourceFilename)
    {
        json componentsJson = json::object();

        serializeRenderComponents(entity, componentsJson);
        serializeAudioPhysicsComponents(entity, componentsJson);
        serializeLightEnvironmentComponents(entity, componentsJson, sourceFilename);
        serializeUIStructuralComponents(entity, componentsJson);
        serializeUIInteractiveComponents(entity, componentsJson);
        serializeMiscComponents(entity, componentsJson);

        // Plugin components
        if (pluginSerializeHook)
        {
            auto pluginJson = pluginSerializeHook(
                scene::EntityRegistry::getRegistry(), entity.getHandle());
            for (auto& [key, value] : pluginJson.items())
            {
                componentsJson[key] = std::move(value);
            }
        }

        return componentsJson;
    }

    std::filesystem::path SceneSerialization::getSettingsPathForScene(std::string_view sceneFilename)
    {
        std::filesystem::path scenePath{std::string(sceneFilename)};
        scenePath.replace_extension(".vfSettings");
        return scenePath;
    }

    std::filesystem::path SceneSerialization::resolveSettingsRefPath(std::string_view sceneFilename,
                                                                     const std::string& settingsRefPath)
    {
        std::filesystem::path path{settingsRefPath};
        if (path.is_relative() && !sceneFilename.empty())
        {
            path = std::filesystem::path{std::string(sceneFilename)}.parent_path() / path;
        }
        return path.lexically_normal();
    }

    std::string SceneSerialization::makeSettingsRefPath(std::string_view sceneFilename,
                                                        const std::filesystem::path& settingsPath)
    {
        std::filesystem::path scenePath{std::string(sceneFilename)};
        if (!scenePath.empty())
        {
            std::error_code ec;
            auto relativePath = std::filesystem::relative(settingsPath, scenePath.parent_path(), ec);
            if (!ec && !relativePath.empty())
            {
                return relativePath.generic_string();
            }
        }

        return settingsPath.generic_string();
    }

    json SceneSerialization::serializeSceneSettings(scene::SceneGraphSystem& sceneGraph)
    {
        json settingsJson;
        settingsJson["version"] = "1.0";
        settingsJson["physicsSettings"] = serializePhysicsSettings(sceneGraph.getPhysicsSettings());
        settingsJson["audioSettings"] = serializeAudioSettings(sceneGraph.getAudioSettings());
        settingsJson["renderSettings"] = serializeRenderSettings(sceneGraph.getRenderSettings());

        const auto& inputMappingPath = sceneGraph.getInputMappingPath();
        if (inputMappingPath.has_value() && !inputMappingPath->empty())
        {
            settingsJson["inputMapping"] = *inputMappingPath;
        }

        // VK-1365: per-scene plugin enable overrides. Only explicit overrides are
        // written; scenes without overrides omit the key entirely. Object-of-objects
        // shape ({"Name": {"enabled": bool}}) leaves room for per-scene plugin
        // configuration values later (VK-1278).
        const auto& pluginSettings = sceneGraph.getPluginSettings();
        if (!pluginSettings.empty())
        {
            json pluginsJson = json::object();
            for (const auto& [name, enabled] : pluginSettings)
            {
                pluginsJson[name] = json{{"enabled", enabled}};
            }
            settingsJson["pluginSettings"] = std::move(pluginsJson);
        }

        return settingsJson;
    }

    bool SceneSerialization::saveSceneSettings(scene::SceneGraphSystem& sceneGraph,
                                               const std::filesystem::path& settingsPath)
    {
        try
        {
            auto parentPath = settingsPath.parent_path();
            if (!parentPath.empty())
            {
                std::filesystem::create_directories(parentPath);
            }

            std::ofstream file{settingsPath};
            if (!file.is_open())
            {
                vfLogError("Failed to open scene settings file for writing: {}", settingsPath.string());
                return false;
            }

            file << serializeSceneSettings(sceneGraph).dump(2);
            file.close();

            return saveSceneSettingsMetadata(settingsPath).isValid();
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to save scene settings {}: {}", settingsPath.string(), e.what());
            return false;
        }
    }

    asset::AssetGUID SceneSerialization::saveSceneSettingsMetadata(const std::filesystem::path& settingsPath)
    {
        auto metaPath = asset::AssetMetadataSerializer::getMetaPath(settingsPath);
        auto existingMeta = asset::AssetMetadataSerializer::load(metaPath);

        asset::AssetMetadata metadata;
        metadata.guid = existingMeta.has_value() ? existingMeta->guid : asset::AssetGUID::generate();
        metadata.type = resource::AssetType::Scene;
        metadata.importSourcePath = settingsPath.string();

        {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            std::tm tm{};
            localtime_s(&tm, &time);
            std::ostringstream oss;
            oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
            metadata.importTimestamp = oss.str();
        }

        if (!asset::AssetMetadataSerializer::save(metadata, metaPath))
        {
            return asset::AssetGUID::invalid();
        }

        auto& db = asset::AssetDatabase::instance();
        auto pathString = settingsPath.string();
        if (!db.getGUID(pathString).has_value())
        {
            db.registerAssetWithGUID(metadata.guid, pathString, resource::AssetType::Scene);
        }

        return metadata.guid;
    }

    bool SceneSerialization::writeSceneSettingsRef(json& sceneJson, std::string_view sceneFilename,
                                                   const std::filesystem::path& settingsPath)
    {
        asset::AssetGUID guid = saveSceneSettingsMetadata(settingsPath);
        if (!guid.isValid())
        {
            vfLogError("Failed to write scene settings metadata: {}", settingsPath.string());
            return false;
        }

        sceneJson["settingsRef"] = guid.toString();
        sceneJson["settingsRefPath"] = makeSettingsRefPath(sceneFilename, settingsPath);
        return true;
    }

    bool SceneSerialization::readLinkedSceneSettings(const json& sceneJson, std::string_view sceneFilename,
                                                     json& outSettingsJson)
    {
        if (!sceneJson.contains("settingsRef") || !sceneJson["settingsRef"].is_string())
        {
            // Legacy format: settings stored inline at the scene JSON root under the
            // same keys deserializeSceneSettings reads (missing keys fall back to defaults)
            vfLogInfo("Scene file has no 'settingsRef'; falling back to inline scene settings");
            outSettingsJson = sceneJson;
            return true;
        }

        if (!sceneJson.contains("settingsRefPath") || !sceneJson["settingsRefPath"].is_string())
        {
            vfLogError("Invalid scene file: missing or invalid 'settingsRefPath'");
            return false;
        }

        std::string settingsPathString;
        auto settingsRef = asset::AssetRef::fromHexString(sceneJson["settingsRef"].get<std::string>());
        if (settingsRef.isValid())
        {
            settingsPathString = settingsRef.resolve();
        }

        if (settingsPathString.empty())
        {
            settingsPathString = resolveSettingsRefPath(
                sceneFilename, sceneJson["settingsRefPath"].get<std::string>()).string();

            if (settingsRef.isValid() && serializationFileExists(settingsPathString))
            {
                asset::AssetDatabase::instance().registerAssetWithGUID(
                    settingsRef.getGUID(), settingsPathString, resource::AssetType::Scene);
                settingsRef.invalidateCache();
            }
        }

        if (settingsPathString.empty())
        {
            vfLogError("Invalid scene file: empty linked scene settings path");
            return false;
        }

        try
        {
            auto rawData = readSerializationFileBytes(settingsPathString);
            if (rawData.empty())
            {
                vfLogError("Failed to read linked scene settings: {}", settingsPathString);
                return false;
            }

            outSettingsJson = json::parse(rawData.begin(), rawData.end());
            if (!outSettingsJson.is_object())
            {
                vfLogError("Invalid scene settings file: root is not a JSON object");
                return false;
            }

            return true;
        }
        catch (const json::parse_error& e)
        {
            vfLogError("JSON parse error while loading scene settings {}: {}",
                       settingsPathString, e.what());
            return false;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to load scene settings {}: {}", settingsPathString, e.what());
            return false;
        }
    }

    bool SceneSerialization::deserializeSceneSettings(const json& sceneJson, scene::SceneGraphSystem& sceneGraph)
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

        if (sceneJson.contains("inputMapping") && sceneJson["inputMapping"].is_string())
        {
            std::string mappingPath = sceneJson["inputMapping"].get<std::string>();
            if (!mappingPath.empty())
            {
                sceneGraph.setInputMappingPath(std::move(mappingPath));
            }
            else
            {
                sceneGraph.setInputMappingPath(std::nullopt);
            }
        }
        else
        {
            sceneGraph.setInputMappingPath(std::nullopt);
        }

        // VK-1365: per-scene plugin enable overrides. Absent key (old scenes) ==
        // no overrides — every plugin follows its global .vfplugin flag.
        std::map<std::string, bool> pluginSettings;
        if (sceneJson.contains("pluginSettings") && sceneJson["pluginSettings"].is_object())
        {
            for (const auto& [name, entry] : sceneJson["pluginSettings"].items())
            {
                if (entry.is_object() && entry.contains("enabled") && entry["enabled"].is_boolean())
                {
                    pluginSettings[name] = entry["enabled"].get<bool>();
                }
            }
        }
        sceneGraph.setPluginSettings(std::move(pluginSettings));

        return true;
    }
}
