#include "ScenePersistenceService.hpp"
#include "EntityStateService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "serialization/SceneSerialization.hpp"
#include "serialization/PrefabSerialization.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/project/SceneEvents.hpp"
#include "../../events/render/RenderEvents.hpp"
#include "../../events/terrain/TerrainEvents.hpp"
#include "../../events/terrain/WaterEvents.hpp"
#include "../../events/physics/PhysicsSettingsEvents.hpp"
#include "../../events/audio/AudioSettingsEvents.hpp"
#include "../../events/render/PostProcessEvents.hpp"
#include "../../events/navmesh/NavmeshEvents.hpp"
#include <functional>
#include <fstream>

namespace services
{
    ScenePersistenceService::ScenePersistenceService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph,
                                                     EntityStateService* entityStateService)
        : sceneGraph(sceneGraph)
          , entityStateService(entityStateService)
    {
    }

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
    }

    bool ScenePersistenceService::newScene()
    {
        if (!sceneGraph)
        {
            vfLogError("SceneGraph is null, cannot create new scene.");
            return false;
        }

        scene::EntityRegistry::setSceneTransitioning(true);

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

        events::postprocess::ApplyPostProcessSettingsCommand postProcessCmd;
        postProcessCmd.settings = sceneGraph->getRenderSettings().postProcess;
        dispatcher.execute(postProcessCmd);

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

        bool result = serialization::SceneSerialization::saveScene(*sceneGraph, filePath);

        return result;
    }

    void ScenePersistenceService::update()
    {
        if (!pendingLoadPath.has_value())
        {
            return;
        }

        std::string filePath = std::move(pendingLoadPath.value());
        pendingLoadPath.reset();

        performDeferredLoad(filePath);
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

        auto progressCallback = [&dispatcher](const std::string& entityName, size_t loaded, size_t total)
        {
            events::scene::SceneLoadingProgressUpdatedNotification progressNotif;
            progressNotif.currentEntityName = entityName;
            progressNotif.progress = (total > 0) ? static_cast<float>(loaded) / static_cast<float>(total) : 0.0f;
            dispatcher.publish(progressNotif);
        };

        bool success = serialization::SceneSerialization::loadSceneInto(filePath, *sceneGraph, progressCallback);

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
                if (!ibl.fileName.empty())
                {
                    events::render::SetIBLCommand setIblCmd;
                    setIblCmd.hdrPath = ibl.fileName;
                    dispatcher.execute(setIblCmd);
                }
            }

            if (root.hasComponent<components::NavmeshComponent>())
            {
                const auto& navmeshComp = root.getComponent<components::NavmeshComponent>();
                if (!navmeshComp.navmeshPath.empty())
                {
                    events::navmesh::LoadNavmeshCommand loadNavCmd;
                    loadNavCmd.filePath = navmeshComp.navmeshPath;
                    dispatcher.execute(loadNavCmd);
                }
            }

            auto& registry = scene::EntityRegistry::getRegistry();
            auto meshView = registry.view<components::MeshComponent>();
            int meshCount = 0;
            for (auto entity : meshView)
            {
                const auto& meshComp = meshView.get<components::MeshComponent>(entity);
                if (!meshComp.meshPath.empty())
                {
                    meshCount++;
                    events::scene::MeshDataChangedNotification meshNotif;
                    meshNotif.entity = internal::toHandle(entity);
                    meshNotif.meshPath = meshComp.meshPath;
                    meshNotif.animatorPath = meshComp.animatorPath;
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
                events::water::RebuildWaterFromComponentsCommand rebuildWaterCmd;
                dispatcher.execute(rebuildWaterCmd);
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

            events::postprocess::ApplyPostProcessSettingsCommand postProcessCmd;
            postProcessCmd.settings = sceneGraph->getRenderSettings().postProcess;
            dispatcher.execute(postProcessCmd);

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

            std::function<void(scene::Entity&)> triggerResourceLoading = [&](scene::Entity& entity)
            {
                if (entity.hasComponent<components::MeshComponent>())
                {
                    const auto& meshComp = entity.getComponent<components::MeshComponent>();
                    if (!meshComp.meshPath.empty())
                    {
                        events::scene::MeshDataChangedNotification meshNotif;
                        meshNotif.entity = internal::toHandle(entity.getHandle());
                        meshNotif.meshPath = meshComp.meshPath;
                        meshNotif.animatorPath = meshComp.animatorPath;
                        dispatcher.publish(meshNotif);
                    }
                }

                for (auto& child : entity.getChildren())
                {
                    triggerResourceLoading(child);
                }
            };
            triggerResourceLoading(*result);

            events::scene::PrefabInstantiatedNotification notification;
            notification.filePath = filePath;
            notification.rootEntity = handle;
            dispatcher.publish(notification);

            return handle;
        }

        return std::nullopt;
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
