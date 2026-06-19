#include "ScenePersistenceService.hpp"
#include "StreamingZoneManager.hpp"
#include "EntityStateService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "serialization/SceneSerialization.hpp"
#include "serialization/PrefabSerialization.hpp"
#include "resource/AssetLifecycleManager.hpp"
#include "resource/AssetLifecycleHelpers.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/project/SceneEvents.hpp"
#include "../../events/project/ApplicationEvents.hpp"
#include "../../events/scene/SceneManagementEvents.hpp"
#include "../../events/scene/StreamingZoneEvents.hpp"
#include "../../events/render/RenderEvents.hpp"
#include "../../events/terrain/TerrainEvents.hpp"
#include "../../events/terrain/OceanEvents.hpp"
#include "../../events/physics/PhysicsSettingsEvents.hpp"
#include "../../events/audio/AudioSettingsEvents.hpp"
#include "../../events/render/PostProcessEvents.hpp"
#include "../../events/render/AtmosphereEvents.hpp"
#include "../../events/render/CloudEvents.hpp"
#include "../../events/navmesh/NavmeshEvents.hpp"
#include "../../events/project/ProjectEvents.hpp"
#include "../../events/input/ActionMappingEvents.hpp"
#include <algorithm>
#include <functional>
#include <fstream>
#include <filesystem>

namespace services
{
    ScenePersistenceService::ScenePersistenceService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph,
                                                     EntityStateService* entityStateService)
        : sceneGraph(sceneGraph)
          , entityStateService(entityStateService)
          , streamingZoneManager(std::make_unique<StreamingZoneManager>())
    {
    }

    ScenePersistenceService::~ScenePersistenceService() = default;

    void ScenePersistenceService::registerEventHandlers(events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<events::scene::NewSceneCommand>(
            [this](const events::scene::NewSceneCommand&)
            {
                return newScene();
            });

        dispatcher.registerCommandHandler<events::scene::SaveSceneCommand>(
            [this](const events::scene::SaveSceneCommand& cmd)
            {
                return saveScene(cmd.filePath);
            });

        dispatcher.registerCommandHandler<events::scene::LoadSceneCommand>(
            [this](const events::scene::LoadSceneCommand& cmd)
            {
                return loadScene(cmd.filePath);
            });

        dispatcher.registerCommandHandler<events::scene::SavePrefabCommand>(
            [this](const events::scene::SavePrefabCommand& cmd)
            {
                return savePrefab(cmd.entity, cmd.filePath);
            });

        dispatcher.registerCommandHandler<events::scene::LoadPrefabCommand>(
            [this](const events::scene::LoadPrefabCommand& cmd)
            {
                return loadPrefab(cmd.filePath, cmd.parent);
            });

        dispatcher.registerQueryHandler<events::scene::GetPrefabSourcePathQuery>(
            [this](const events::scene::GetPrefabSourcePathQuery& query)
            {
                return getPrefabSourcePath(query.entity);
            });

        dispatcher.registerQueryHandler<events::scene::CopyEntityToJsonQuery>(
            [this](const events::scene::CopyEntityToJsonQuery& query)
            {
                return copyEntityToJson(query.entity);
            });

        dispatcher.registerCommandHandler<events::scene::InstantiateEntityFromJsonCommand>(
            [this](const events::scene::InstantiateEntityFromJsonCommand& cmd)
            {
                return instantiateEntityFromJson(cmd.jsonText, cmd.parent);
            });

        dispatcher.registerQueryHandler<events::scene::GetPhysicsSettingsQuery>(
            [this](const events::scene::GetPhysicsSettingsQuery&)
            {
                return getPhysicsSettings();
            });

        dispatcher.registerCommandHandler<events::scene::SetPhysicsSettingsCommand>(
            [this](const events::scene::SetPhysicsSettingsCommand& cmd)
            {
                return setPhysicsSettings(cmd.settings);
            });

        dispatcher.registerQueryHandler<events::scene::GetAudioSettingsQuery>(
            [this](const events::scene::GetAudioSettingsQuery&)
            {
                return getAudioSettings();
            });

        dispatcher.registerCommandHandler<events::scene::SetAudioSettingsCommand>(
            [this](const events::scene::SetAudioSettingsCommand& cmd)
            {
                return setAudioSettings(cmd.settings);
            });

        dispatcher.registerQueryHandler<events::scene::GetRenderSettingsQuery>(
            [this](const events::scene::GetRenderSettingsQuery&)
            {
                return getRenderSettings();
            });

        dispatcher.registerCommandHandler<events::scene::SetRenderSettingsCommand>(
            [this](const events::scene::SetRenderSettingsCommand& cmd)
            {
                return setRenderSettings(cmd.settings);
            });

        // VK-1365: per-scene plugin enable overrides
        dispatcher.registerQueryHandler<events::scene::GetScenePluginSettingsQuery>(
            [this](const events::scene::GetScenePluginSettingsQuery&)
            {
                return sceneGraph ? sceneGraph->getPluginSettings() : std::map<std::string, bool>{};
            });

        dispatcher.registerCommandHandler<events::scene::SetScenePluginSettingsCommand>(
            [this](const events::scene::SetScenePluginSettingsCommand& cmd)
            {
                if (!sceneGraph) return false;
                sceneGraph->setPluginSettings(cmd.settings);
                return true;
            });

        // Additive scene management
        dispatcher.registerCommandHandler<events::scene::LoadSceneAdditiveCommand>(
            [this](const events::scene::LoadSceneAdditiveCommand& cmd)
            {
                return loadSceneAdditive(cmd.scenePath, cmd.sceneName);
            });

        dispatcher.registerCommandHandler<events::scene::UnloadAdditiveSceneCommand>(
            [this](const events::scene::UnloadAdditiveSceneCommand& cmd)
            {
                return unloadAdditiveScene(cmd.sceneName);
            });

        dispatcher.registerCommandHandler<events::scene::SetActiveSceneCommand>(
            [this](const events::scene::SetActiveSceneCommand& cmd)
            {
                return setActiveScene(cmd.sceneName);
            });

        dispatcher.registerQueryHandler<events::scene::GetActiveSceneQuery>(
            [this](const events::scene::GetActiveSceneQuery&)
            {
                return getActiveScene();
            });

        dispatcher.registerQueryHandler<events::scene::GetLoadedScenesQuery>(
            [this](const events::scene::GetLoadedScenesQuery&)
            {
                return getLoadedScenes();
            });

        dispatcher.registerQueryHandler<events::scene::IsSceneLoadedQuery>(
            [this](const events::scene::IsSceneLoadedQuery& q)
            {
                return isSceneLoaded(q.sceneName);
            });

        dispatcher.registerCommandHandler<events::scene::CancelPendingSceneLoadsCommand>(
            [this](const events::scene::CancelPendingSceneLoadsCommand&)
            {
                cancelPendingLoads();
            });

        // Streaming zone events
        streamingZoneManager->registerEventHandlers(dispatcher);
    }

    void ScenePersistenceService::cancelPendingLoads()
    {
        pendingLoadPath.reset();
        while (!pendingAdditiveLoads.empty()) pendingAdditiveLoads.pop();
    }

    bool ScenePersistenceService::newScene()
    {
        if (!sceneGraph)
        {
            vfLogError("SceneGraph is null, cannot create new scene.");
            return false;
        }

        scene::EntityRegistry::setSceneTransitioning(true);

        // Clear all additive scenes
        loadedAdditiveScenes.clear();
        activeSceneName = "Main";
        while (!pendingAdditiveLoads.empty()) pendingAdditiveLoads.pop();

        // Clear streaming zones
        if (streamingZoneManager)
        {
            streamingZoneManager->clear();
        }

        auto& dispatcher = events::EventDispatcher::instance();

        events::render::RemoveIBLCommand removeIblCmd;
        dispatcher.execute(removeIblCmd);

        events::terrain::TerrainDeletedNotification terrainNotif;
        dispatcher.publish(terrainNotif);

        sceneGraph->clearScene();

        sceneGraph->setPhysicsSettings(types::PhysicsSettings::createDefault());
        events::physics::ApplyPhysicsSettingsCommand physicsCmd;
        physicsCmd.settings = sceneGraph->getPhysicsSettings();
        dispatcher.execute(physicsCmd);

        sceneGraph->setAudioSettings(types::AudioSettings::createDefault());
        events::audio::ApplyAudioSettingsCommand audioCmd;
        audioCmd.settings = sceneGraph->getAudioSettings();
        dispatcher.execute(audioCmd);

        sceneGraph->setRenderSettings(types::RenderSettings::createDefault());
        sceneGraph->setPluginSettings({}); // VK-1365: new scene has no plugin overrides

        events::postprocess::ApplyPostProcessSettingsCommand postProcessCmd;
        postProcessCmd.settings = sceneGraph->getRenderSettings().postProcess;
        dispatcher.execute(postProcessCmd);

        events::atmosphere::ApplyAtmosphereSettingsCommand atmosphereCmd;
        atmosphereCmd.settings = sceneGraph->getRenderSettings().atmosphere;
        dispatcher.execute(atmosphereCmd);

        events::cloud::ApplyCloudSettingsCommand cloudCmd;
        cloudCmd.settings = sceneGraph->getRenderSettings().cloud;
        dispatcher.execute(cloudCmd);

        if (entityStateService)
        {
            entityStateService->clearSelection();
        }

        events::scene::SceneClearedNotification notification;
        dispatcher.publish(notification);

        scene::EntityRegistry::setSceneTransitioning(false);

        return true;
    }

    bool ScenePersistenceService::saveScene(const std::string& filePath)
    {
        if (!sceneGraph)
        {
            vfLogError("SceneGraph is null, cannot save scene.");
            return false;
        }

        if (filePath.empty())
        {
            vfLogError("File path is empty, cannot save scene.");
            return false;
        }

        return serialization::SceneSerialization::saveScene(*sceneGraph, filePath);;
    }

    void ScenePersistenceService::setIncrementalLoadBudget(int entitiesPerFrame)
    {
        incrementalLoadBudget = entitiesPerFrame;
    }

    void ScenePersistenceService::update()
    {
        // Drive an in-progress incremental load before anything else; only one
        // scene load runs at a time, so hold off other work until it finishes.
        if (incrementalActive)
        {
            bool more = serialization::SceneSerialization::stepIncrementalLoad(
                *incrementalState, incrementalLoadBudget);
            if (!more)
            {
                incrementalActive = false;
                finishLoad(incrementalFilePath, incrementalState->success);
            }
            return;
        }

        if (pendingLoadPath.has_value())
        {
            std::string filePath = std::move(pendingLoadPath.value());
            pendingLoadPath.reset();
            performDeferredLoad(filePath);
            if (incrementalActive)
            {
                return; // incremental load just began; resume stepping next frame
            }
        }

        if (!pendingAdditiveLoads.empty())
        {
            PendingAdditiveLoad load = std::move(pendingAdditiveLoads.front());
            pendingAdditiveLoads.pop();
            performDeferredAdditiveLoad(load);
        }

        if (streamingZoneManager)
        {
            streamingZoneManager->update();
        }
    }

    bool ScenePersistenceService::loadScene(const std::string& filePath)
    {
        if (!sceneGraph)
        {
            vfLogError("SceneGraph is null, cannot load scene.");
            return false;
        }

        if (filePath.empty())
        {
            vfLogError("File path is empty, cannot load scene.");
            return false;
        }

        // NOTE: This only queues the load for the next frame (deferred loading).
        // A return value of true means the request was accepted, NOT that the scene
        // loaded successfully. Callers must subscribe to SceneLoadingCompletedNotification
        // to determine actual load success/failure.
        events::scene::SceneLoadingStartedNotification startNotif;
        startNotif.scenePath = filePath;
        events::EventDispatcher::instance().publish(startNotif);

        pendingLoadPath = filePath;

        return true;
    }

    void ScenePersistenceService::performDeferredLoad(const std::string& filePath)
    {
        // Block render preparation from accessing registry during scene load
        scene::EntityRegistry::setSceneTransitioning(true);

        auto& dispatcher = events::EventDispatcher::instance();

        events::render::RemoveIBLCommand removeIblCmd;
        dispatcher.execute(removeIblCmd);

        events::terrain::TerrainDeletedNotification terrainNotif;
        dispatcher.publish(terrainNotif);

        sceneGraph->clearScene();

        if (entityStateService)
        {
            entityStateService->clearSelection();
        }

        events::scene::SceneClearedNotification clearedNotif;
        dispatcher.publish(clearedNotif);

        // Captureless so it can be safely stored and invoked across future frames
        // by the incremental loader (no dangling reference to a local dispatcher).
        auto progressCallback = [](const std::string& entityName, size_t loaded, size_t total)
        {
            events::scene::SceneLoadingProgressUpdatedNotification progressNotif;
            progressNotif.currentEntityName = entityName;
            progressNotif.progress = (total > 0) ? static_cast<float>(loaded) / static_cast<float>(total) : 0.0f;
            events::EventDispatcher::instance().publish(progressNotif);
        };

        if (incrementalLoadBudget > 0)
        {
            if (!incrementalState)
            {
                incrementalState = std::make_unique<serialization::IncrementalLoadState>();
            }
            bool stepping = serialization::SceneSerialization::beginIncrementalLoad(
                filePath, *sceneGraph, progressCallback, *incrementalState);
            if (stepping)
            {
                // Deserialization continues over the next frames in update();
                // finishLoad() runs when the work stack drains.
                incrementalActive = true;
                incrementalFilePath = filePath;
                return;
            }
            finishLoad(filePath, incrementalState->success);
            return;
        }

        bool success = serialization::SceneSerialization::loadSceneInto(filePath, *sceneGraph, progressCallback);
        finishLoad(filePath, success);
    }

    void ScenePersistenceService::finishLoad(const std::string& filePath, bool success)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        events::scene::SceneLoadingCompletedNotification completeNotif;
        completeNotif.scenePath = filePath;
        completeNotif.success = success;
        if (!success)
        {
            completeNotif.errorMessage = "Failed to load scene file";
        }
        dispatcher.publish(completeNotif);

        if (!success)
        {
            scene::EntityRegistry::setSceneTransitioning(false);
            return;
        }

        {
            scene::Entity& root = sceneGraph->GetRoot();
            if (root.hasComponent<components::IBLComponent>())
            {
                const auto& ibl = root.getComponent<components::IBLComponent>();
                if (ibl.hdrRef.isValid())
                {
                    events::render::SetIBLCommand setIblCmd;
                    setIblCmd.hdrPath = ibl.hdrRef.resolve();
                    dispatcher.execute(setIblCmd);
                }
            }

            if (root.hasComponent<components::NavmeshComponent>())
            {
                const auto& navmeshComp = root.getComponent<components::NavmeshComponent>();
                if (navmeshComp.navmeshRef.isValid())
                {
                    const std::string& navmeshPath = navmeshComp.navmeshRef.resolve();
                    std::string ext = std::filesystem::path(navmeshPath).extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                    if (ext == ".vfnavindex")
                    {
                        // Tiled navmesh: the asset is the index file, tiles live beside it
                        events::navmesh::LoadNavmeshTiledCommand loadTiledCmd;
                        loadTiledCmd.directory = std::filesystem::path(navmeshPath).parent_path().string();
                        dispatcher.execute(loadTiledCmd);
                    }
                    else
                    {
                        events::navmesh::LoadNavmeshCommand loadNavCmd;
                        loadNavCmd.filePath = navmeshPath;
                        dispatcher.execute(loadNavCmd);
                    }
                }
            }

            auto& registry = scene::EntityRegistry::getRegistry();
            auto meshView = registry.view<components::MeshComponent>();
            int meshCount = 0;
            for (auto entity : meshView)
            {
                const auto& meshComp = meshView.get<components::MeshComponent>(entity);
                if (meshComp.meshRef.isValid())
                {
                    meshCount++;
                    events::scene::MeshDataChangedNotification meshNotif;
                    meshNotif.entity = internal::toHandle(entity);
                    meshNotif.meshPath = meshComp.meshRef.resolve();
                    meshNotif.animatorPath = meshComp.animatorRef.resolve();
                    dispatcher.publish(meshNotif);
                }
            }

            {
                std::vector<std::string> terrainPaths;
                std::vector<EntityHandle> terrainEntitiesToDelete;

                auto terrainView = registry.view<components::TerrainComponent>();
                for (auto entity : terrainView)
                {
                    const auto& terrainComp = terrainView.get<components::TerrainComponent>(entity);
                    if (!terrainComp.savePath.empty())
                    {
                        terrainPaths.push_back(terrainComp.savePath);
                        terrainEntitiesToDelete.push_back(internal::toHandle(entity));
                    }
                }

                for (auto handle : terrainEntitiesToDelete)
                {
                    events::terrain::DeleteTerrainCommand delCmd;
                    delCmd.terrainEntity = handle;
                    dispatcher.execute(delCmd);
                }

                for (const auto& path : terrainPaths)
                {
                    events::terrain::LoadTerrainCommand loadCmd;
                    loadCmd.path = path;
                    dispatcher.execute(loadCmd);
                }
            }

            {
                events::ocean::RebuildOceanFromComponentsCommand rebuildOceanCmd;
                dispatcher.execute(rebuildOceanCmd);
            }

            events::physics::ApplyPhysicsSettingsCommand physicsCmd;
            physicsCmd.settings = sceneGraph->getPhysicsSettings();
            dispatcher.execute(physicsCmd);

            events::audio::ApplyAudioSettingsCommand audioCmd;
            audioCmd.settings = sceneGraph->getAudioSettings();
            dispatcher.execute(audioCmd);

            events::render::ApplyShadowSettingsCommand renderCmd;
            renderCmd.settings = sceneGraph->getRenderSettings();
            dispatcher.execute(renderCmd);

            // Apply the scene's display settings (VSync present mode) to the swapchain.
            events::application::ApplyDisplaySettingsNotification displayNotif;
            displayNotif.presentMode = sceneGraph->getRenderSettings().display.presentMode;
            displayNotif.msaa = sceneGraph->getRenderSettings().display.msaa;
            dispatcher.publish(displayNotif);

            events::postprocess::ApplyPostProcessSettingsCommand postProcessCmd;
            postProcessCmd.settings = sceneGraph->getRenderSettings().postProcess;
            dispatcher.execute(postProcessCmd);

            events::atmosphere::ApplyAtmosphereSettingsCommand atmosphereCmd;
            atmosphereCmd.settings = sceneGraph->getRenderSettings().atmosphere;
            dispatcher.execute(atmosphereCmd);

            events::cloud::ApplyCloudSettingsCommand cloudCmd;
            cloudCmd.settings = sceneGraph->getRenderSettings().cloud;
            dispatcher.execute(cloudCmd);

            const auto& sceneInputMapping = sceneGraph->getInputMappingPath();
            if (sceneInputMapping.has_value() && !sceneInputMapping->empty())
            {
                auto projectOpt = dispatcher.query(events::project::GetCurrentProjectQuery{});
                std::filesystem::path mappingPath = *sceneInputMapping;
                if (projectOpt.has_value() && !projectOpt->workingDirectory.empty() && mappingPath.is_relative())
                {
                    mappingPath = std::filesystem::path(projectOpt->workingDirectory) / mappingPath;
                }

                if (std::filesystem::exists(mappingPath))
                {
                    events::input::LoadActionBindingsCommand mappingCmd;
                    mappingCmd.filePath = mappingPath.string();
                    dispatcher.execute(mappingCmd);
                    vfLogInfo("Scene input mapping loaded: {}", mappingPath.string());
                }
                else
                {
                    vfLogWarning("Scene input mapping not found: {}", mappingPath.string());
                }
            }

            events::scene::SceneLoadedNotification notification;
            notification.scenePath = filePath;
            dispatcher.publish(notification);
        }

        // Re-allow render preparation to access registry
        scene::EntityRegistry::setSceneTransitioning(false);
    }

    bool ScenePersistenceService::savePrefab(EntityHandle entity, const std::string& filePath)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return false;
        }

        scene::Entity& root = sceneGraph->GetRoot();
        if (internal::fromHandle(entity) == root.getHandle())
        {
            vfLogWarning("Cannot save root entity as prefab");
            return false;
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        bool success = serialization::PrefabSerialization::savePrefab(sceneEntity, filePath);

        if (success)
        {
            // Saving an entity as a prefab makes it an instance of that prefab —
            // link it so it immediately becomes "Update Prefab"-able.
            registry.emplace_or_replace<components::PrefabInstanceComponent>(
                internal::fromHandle(entity)).sourcePrefabPath = filePath;

            events::scene::PrefabCreatedNotification notification;
            notification.filePath = filePath;
            notification.sourceEntity = entity;
            events::EventDispatcher::instance().publish(notification);
        }

        return success;
    }

    std::optional<EntityHandle> ScenePersistenceService::loadPrefab(const std::string& filePath,
                                                                    std::optional<EntityHandle> parent)
    {
        scene::Entity parentEntity = sceneGraph->GetRoot();
        if (parent.has_value() && parent->isValid())
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            if (internal::isValidHandle(*parent, registry))
            {
                parentEntity = scene::Entity(internal::fromHandle(*parent));
            }
        }

        auto result = serialization::PrefabSerialization::loadPrefab(filePath, parentEntity, *sceneGraph);

        if (result.has_value())
        {
            auto handle = internal::toHandle(result->getHandle());
            auto& dispatcher = events::EventDispatcher::instance();

            // Link the instantiated root back to its source asset so the editor can
            // "Update Prefab" later. Root only — children are never self-linked.
            scene::EntityRegistry::getRegistry()
                .emplace_or_replace<components::PrefabInstanceComponent>(result->getHandle())
                .sourcePrefabPath = filePath;

            acquireAndNotifyResources(*result);

            events::scene::PrefabInstantiatedNotification notification;
            notification.filePath = filePath;
            notification.rootEntity = handle;
            dispatcher.publish(notification);

            return handle;
        }

        return std::nullopt;
    }

    std::string ScenePersistenceService::getPrefabSourcePath(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry))
        {
            return {};
        }

        entt::entity handle = internal::fromHandle(entity);
        if (const auto* link = registry.try_get<components::PrefabInstanceComponent>(handle))
        {
            return link->sourcePrefabPath;
        }

        return {};
    }

    void ScenePersistenceService::acquireAndNotifyResources(scene::Entity& entity) const
    {
        // Balance the per-entity releaseEntityAssets() that fires on delete —
        // without this, deleting the instantiated subtree drops refcounts to zero
        // and the GPU resources get released even if the subtree is re-added.
        resource::acquireEntityAssets(entity, resource::AssetLifecycleManager::instance());

        if (entity.hasComponent<components::MeshComponent>())
        {
            const auto& meshComp = entity.getComponent<components::MeshComponent>();
            if (meshComp.meshRef.isValid())
            {
                events::scene::MeshDataChangedNotification meshNotif;
                meshNotif.entity = internal::toHandle(entity.getHandle());
                meshNotif.meshPath = meshComp.meshRef.resolve();
                meshNotif.animatorPath = meshComp.animatorRef.resolve();
                events::EventDispatcher::instance().publish(meshNotif);
            }
        }

        for (auto& child : entity.getChildren())
        {
            acquireAndNotifyResources(child);
        }
    }

    std::string ScenePersistenceService::copyEntityToJson(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!sceneGraph || !internal::isValidHandle(entity, registry))
        {
            return {};
        }

        scene::Entity& root = sceneGraph->GetRoot();
        if (internal::fromHandle(entity) == root.getHandle())
        {
            vfLogWarning("Cannot copy the root entity");
            return {};
        }

        scene::Entity sceneEntity(internal::fromHandle(entity));
        return serialization::PrefabSerialization::serializeEntityTree(sceneEntity).dump();
    }

    std::optional<EntityHandle> ScenePersistenceService::instantiateEntityFromJson(
        const std::string& jsonText, std::optional<EntityHandle> parent)
    {
        if (!sceneGraph || jsonText.empty())
        {
            return std::nullopt;
        }

        nlohmann::json entityJson = nlohmann::json::parse(jsonText, nullptr, false);
        if (entityJson.is_discarded() || !entityJson.is_object())
        {
            vfLogError("instantiateEntityFromJson: invalid entity JSON");
            return std::nullopt;
        }

        scene::Entity parentEntity = sceneGraph->GetRoot();
        if (parent.has_value() && parent->isValid())
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            if (internal::isValidHandle(*parent, registry))
            {
                parentEntity = scene::Entity(internal::fromHandle(*parent));
            }
        }

        scene::Entity instantiated = serialization::PrefabSerialization::deserializeEntityTree(
            entityJson, parentEntity, *sceneGraph);
        if (!instantiated.isValid())
        {
            return std::nullopt;
        }

        acquireAndNotifyResources(instantiated);

        return internal::toHandle(instantiated.getHandle());
    }

    bool ScenePersistenceService::loadSceneAdditive(const std::string& scenePath, const std::string& sceneName)
    {
        if (!sceneGraph)
        {
            vfLogError("SceneGraph is null, cannot load additive scene.");
            return false;
        }

        if (scenePath.empty() || sceneName.empty())
        {
            vfLogError("Scene path or name is empty, cannot load additive scene.");
            return false;
        }

        if (loadedAdditiveScenes.count(sceneName) > 0)
        {
            vfLogWarning("Additive scene '{}' is already loaded.", sceneName);
            return false;
        }

        // Queue for deferred loading
        pendingAdditiveLoads.push(PendingAdditiveLoad{scenePath, sceneName});
        return true;
    }

    void ScenePersistenceService::performDeferredAdditiveLoad(const PendingAdditiveLoad& load)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Create a container entity under the scene root
        scene::Entity& root = sceneGraph->GetRoot();
        scene::Entity container(load.sceneName);

        if (!container.isValid())
        {
            vfLogError("Failed to create container entity for additive scene '{}'", load.sceneName);
            return;
        }

        sceneGraph->addChild(root, container);

        // Tag the container with AdditiveSceneComponent
        container.addComponent<components::AdditiveSceneComponent>(
            components::AdditiveSceneComponent{load.sceneName, load.scenePath});

        auto progressCallback = [&dispatcher](const std::string& entityName, size_t loaded, size_t total)
        {
            events::scene::SceneLoadingProgressUpdatedNotification progressNotif;
            progressNotif.currentEntityName = entityName;
            progressNotif.progress = (total > 0) ? static_cast<float>(loaded) / static_cast<float>(total) : 0.0f;
            dispatcher.publish(progressNotif);
        };

        bool success = serialization::SceneSerialization::loadSceneAdditive(
            load.scenePath, *sceneGraph, container, progressCallback);

        if (!success)
        {
            vfLogError("Failed to load additive scene '{}' from '{}'", load.sceneName, load.scenePath);
            sceneGraph->removeEntity(container);
            return;
        }

        auto handle = internal::toHandle(container.getHandle());
        loadedAdditiveScenes[load.sceneName] = handle;

        // Trigger resource loading for all entities in the additive scene
        triggerResourceLoadingForEntity(container);

        events::scene::AdditiveSceneLoadedNotification notification;
        notification.sceneName = load.sceneName;
        notification.scenePath = load.scenePath;
        notification.rootEntity = handle;
        dispatcher.publish(notification);
    }

    bool ScenePersistenceService::unloadAdditiveScene(const std::string& sceneName)
    {
        if (!sceneGraph)
        {
            vfLogError("SceneGraph is null, cannot unload additive scene.");
            return false;
        }

        auto it = loadedAdditiveScenes.find(sceneName);
        if (it == loadedAdditiveScenes.end())
        {
            vfLogWarning("Additive scene '{}' is not loaded.", sceneName);
            return false;
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        auto entityHandle = it->second;
        loadedAdditiveScenes.erase(it);

        if (internal::isValidHandle(entityHandle, registry))
        {
            scene::Entity container(internal::fromHandle(entityHandle));
            sceneGraph->removeEntity(container);
        }

        auto& dispatcher = events::EventDispatcher::instance();
        events::scene::AdditiveSceneUnloadedNotification notification;
        notification.sceneName = sceneName;
        dispatcher.publish(notification);

        return true;
    }

    std::string ScenePersistenceService::getActiveScene() const
    {
        return activeSceneName;
    }

    bool ScenePersistenceService::setActiveScene(const std::string& sceneName)
    {
        if (sceneName == "Main" || loadedAdditiveScenes.count(sceneName) > 0)
        {
            activeSceneName = sceneName;
            return true;
        }

        vfLogWarning("Cannot set active scene to '{}': scene not loaded.", sceneName);
        return false;
    }

    std::vector<std::string> ScenePersistenceService::getLoadedScenes() const
    {
        std::vector<std::string> scenes;
        scenes.push_back("Main");
        for (const auto& [name, handle] : loadedAdditiveScenes)
        {
            scenes.push_back(name);
        }
        return scenes;
    }

    bool ScenePersistenceService::isSceneLoaded(const std::string& sceneName) const
    {
        if (sceneName == "Main") return true;
        return loadedAdditiveScenes.count(sceneName) > 0;
    }

    void ScenePersistenceService::triggerResourceLoadingForEntity(scene::Entity& entity) const
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (entity.hasComponent<components::MeshComponent>())
        {
            const auto& meshComp = entity.getComponent<components::MeshComponent>();
            if (meshComp.meshRef.isValid())
            {
                events::scene::MeshDataChangedNotification meshNotif;
                meshNotif.entity = internal::toHandle(entity.getHandle());
                meshNotif.meshPath = meshComp.meshRef.resolve();
                meshNotif.animatorPath = meshComp.animatorRef.resolve();
                dispatcher.publish(meshNotif);
            }
        }

        for (auto& child : entity.getChildren())
        {
            triggerResourceLoadingForEntity(child);
        }
    }

    types::PhysicsSettings ScenePersistenceService::getPhysicsSettings() const
    {
        if (!sceneGraph)
        {
            return types::PhysicsSettings::createDefault();
        }
        return sceneGraph->getPhysicsSettings();
    }

    bool ScenePersistenceService::setPhysicsSettings(const types::PhysicsSettings& settings)
    {
        if (!sceneGraph)
        {
            return false;
        }
        sceneGraph->setPhysicsSettings(settings);
        return true;
    }

    types::AudioSettings ScenePersistenceService::getAudioSettings() const
    {
        if (!sceneGraph)
        {
            return types::AudioSettings::createDefault();
        }
        return sceneGraph->getAudioSettings();
    }

    bool ScenePersistenceService::setAudioSettings(const types::AudioSettings& settings)
    {
        if (!sceneGraph)
        {
            return false;
        }
        sceneGraph->setAudioSettings(settings);
        return true;
    }

    types::RenderSettings ScenePersistenceService::getRenderSettings() const
    {
        if (!sceneGraph)
        {
            return types::RenderSettings::createDefault();
        }
        return sceneGraph->getRenderSettings();
    }

    bool ScenePersistenceService::setRenderSettings(const types::RenderSettings& settings)
    {
        if (!sceneGraph)
        {
            return false;
        }
        sceneGraph->setRenderSettings(settings);
        return true;
    }
}
