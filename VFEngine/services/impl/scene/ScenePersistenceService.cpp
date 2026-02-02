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
#include "../../events/SceneEvents.hpp"
#include "../../events/RenderEvents.hpp"
#include "../../events/TerrainEvents.hpp"
#include "../../events/PhysicsSettingsEvents.hpp"
#include "../../events/AudioSettingsEvents.hpp"
#include "print/EditorLogger.hpp"
#include <functional>

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

        // Physics settings handlers
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

        // Audio settings handlers
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

        auto& dispatcher = events::EventDispatcher::instance();

        events::render::RemoveIBLCommand removeIblCmd;
        dispatcher.execute(removeIblCmd);

        // Clear terrain GPU data before clearing scene
        events::terrain::TerrainDeletedNotification terrainNotif;
        dispatcher.publish(terrainNotif);

        sceneGraph->clearScene();

        // Reset physics settings to defaults for new scene
        sceneGraph->setPhysicsSettings(types::PhysicsSettings::createDefault());
        events::physics::ApplyPhysicsSettingsCommand physicsCmd;
        physicsCmd.settings = sceneGraph->getPhysicsSettings();
        dispatcher.execute(physicsCmd);

        // Reset audio settings to defaults for new scene
        sceneGraph->setAudioSettings(types::AudioSettings::createDefault());
        events::audio::ApplyAudioSettingsCommand audioCmd;
        audioCmd.settings = sceneGraph->getAudioSettings();
        dispatcher.execute(audioCmd);

        // Reset render settings to defaults for new scene
        sceneGraph->setRenderSettings(types::RenderSettings::createDefault());

        if (entityStateService)
        {
            entityStateService->clearSelection();
        }

        events::scene::SceneClearedNotification notification;
        dispatcher.publish(notification);

        vfLogInfo("New scene created.");
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

        return serialization::SceneSerialization::saveScene(*sceneGraph, filePath);
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

        auto& dispatcher = events::EventDispatcher::instance();

        events::render::RemoveIBLCommand removeIblCmd;
        dispatcher.execute(removeIblCmd);

        // Clear terrain GPU data before loading new scene
        events::terrain::TerrainDeletedNotification terrainNotif;
        dispatcher.publish(terrainNotif);

        if (entityStateService)
        {
            entityStateService->clearSelection();
        }

        events::scene::SceneLoadingStartedNotification startNotif;
        startNotif.scenePath = filePath;
        dispatcher.publish(startNotif);

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

        if (success)
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

            auto& registry = scene::EntityRegistry::getRegistry();
            auto meshView = registry.view<components::MeshComponent>();
            for (auto entity : meshView)
            {
                const auto& meshComp = meshView.get<components::MeshComponent>(entity);
                if (!meshComp.meshPath.empty())
                {
                    events::scene::MeshDataChangedNotification meshNotif;
                    meshNotif.entity = internal::toHandle(entity);
                    meshNotif.meshPath = meshComp.meshPath;
                    meshNotif.animatorPath = meshComp.animatorPath;
                    dispatcher.publish(meshNotif);
                }
            }

            // Apply physics settings from the loaded scene
            events::physics::ApplyPhysicsSettingsCommand physicsCmd;
            physicsCmd.settings = sceneGraph->getPhysicsSettings();
            dispatcher.execute(physicsCmd);

            // Apply audio settings from the loaded scene
            events::audio::ApplyAudioSettingsCommand audioCmd;
            audioCmd.settings = sceneGraph->getAudioSettings();
            dispatcher.execute(audioCmd);

            // Apply render/shadow settings from the loaded scene
            events::render::ApplyShadowSettingsCommand renderCmd;
            renderCmd.settings = sceneGraph->getRenderSettings();
            dispatcher.execute(renderCmd);

            events::scene::SceneLoadedNotification notification;
            notification.scenePath = filePath;
            dispatcher.publish(notification);
        }

        return success;
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
