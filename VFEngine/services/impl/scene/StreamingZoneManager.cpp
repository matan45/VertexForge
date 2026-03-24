#include "StreamingZoneManager.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/scene/SceneManagementEvents.hpp"
#include "../../events/scene/StreamingZoneEvents.hpp"
#include "print/Log.hpp"
#include <filesystem>

namespace services
{
    void StreamingZoneManager::registerEventHandlers(events::EventDispatcher& dispatcher)
    {
        dispatcher.registerCommandHandler<events::scene::SetStreamingTriggerZoneCommand>(
            [this](const events::scene::SetStreamingTriggerZoneCommand& cmd)
            {
                return addZone(cmd.boundsMin, cmd.boundsMax, cmd.scenePath);
            });

        dispatcher.registerCommandHandler<events::scene::RemoveStreamingTriggerZoneCommand>(
            [this](const events::scene::RemoveStreamingTriggerZoneCommand& cmd)
            {
                return removeZone(cmd.zoneId);
            });

        dispatcher.registerCommandHandler<events::scene::PreloadSceneCommand>(
            [this](const events::scene::PreloadSceneCommand&)
            {
                // Preloading is a no-op for now - scene files are small enough
                // to load on demand. Can be extended to cache parsed JSON later.
                return true;
            });

        dispatcher.registerQueryHandler<events::scene::IsStreamingSceneLoadedQuery>(
            [this](const events::scene::IsStreamingSceneLoadedQuery& q)
            {
                return isScenePathLoaded(q.scenePath);
            });
    }

    uint32_t StreamingZoneManager::addZone(const glm::vec3& boundsMin, const glm::vec3& boundsMax,
                                            const std::string& scenePath)
    {
        uint32_t id = nextZoneId++;
        StreamingZone zone;
        zone.id = id;
        zone.boundsMin = boundsMin;
        zone.boundsMax = boundsMax;
        zone.scenePath = scenePath;
        zone.sceneName = generateSceneName(scenePath, id);
        zone.isLoaded = false;

        zones[id] = std::move(zone);
        return id;
    }

    bool StreamingZoneManager::removeZone(uint32_t zoneId)
    {
        auto it = zones.find(zoneId);
        if (it == zones.end())
        {
            return false;
        }

        // Unload the scene if it was loaded by this zone
        if (it->second.isLoaded)
        {
            auto& dispatcher = events::EventDispatcher::instance();
            events::scene::UnloadAdditiveSceneCommand cmd;
            cmd.sceneName = it->second.sceneName;
            dispatcher.execute(cmd);
        }

        zones.erase(it);
        return true;
    }

    void StreamingZoneManager::clear()
    {
        auto& dispatcher = events::EventDispatcher::instance();
        for (auto& [id, zone] : zones)
        {
            if (zone.isLoaded)
            {
                events::scene::UnloadAdditiveSceneCommand cmd;
                cmd.sceneName = zone.sceneName;
                dispatcher.execute(cmd);
            }
        }
        zones.clear();
    }

    bool StreamingZoneManager::isScenePathLoaded(const std::string& scenePath) const
    {
        for (const auto& [id, zone] : zones)
        {
            if (zone.scenePath == scenePath && zone.isLoaded)
                return true;
        }
        return false;
    }

    void StreamingZoneManager::update()
    {
        if (zones.empty())
        {
            return;
        }

        glm::vec3 cameraPos = getCameraPosition();
        auto& dispatcher = events::EventDispatcher::instance();

        for (auto& [id, zone] : zones)
        {
            bool inZone = isPointInAABB(cameraPos, zone.boundsMin, zone.boundsMax);

            if (inZone && !zone.isLoaded)
            {
                events::scene::LoadSceneAdditiveCommand cmd;
                cmd.scenePath = zone.scenePath;
                cmd.sceneName = zone.sceneName;
                bool accepted = dispatcher.execute(cmd);
                if (accepted)
                {
                    zone.isLoaded = true;

                    events::scene::StreamingZoneActivatedNotification notif;
                    notif.zoneId = zone.id;
                    notif.scenePath = zone.scenePath;
                    dispatcher.publish(notif);
                }
            }
            else if (!inZone && zone.isLoaded)
            {
                events::scene::UnloadAdditiveSceneCommand cmd;
                cmd.sceneName = zone.sceneName;
                bool success = dispatcher.execute(cmd);
                if (success)
                {
                    zone.isLoaded = false;

                    events::scene::StreamingZoneDeactivatedNotification notif;
                    notif.zoneId = zone.id;
                    notif.scenePath = zone.scenePath;
                    dispatcher.publish(notif);
                }
            }
        }
    }

    bool StreamingZoneManager::isPointInAABB(const glm::vec3& point, const glm::vec3& min,
                                              const glm::vec3& max) const
    {
        return point.x >= min.x && point.x <= max.x &&
               point.y >= min.y && point.y <= max.y &&
               point.z >= min.z && point.z <= max.z;
    }

    glm::vec3 StreamingZoneManager::getCameraPosition() const
    {
        // Use the primary camera entity's transform position.
        // We subscribe to CameraPositionUpdatedNotification, but for simplicity
        // we query the registry directly for the primary camera.
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::CameraComponent, components::TransformComponent>();
        for (auto entity : view)
        {
            const auto& cam = view.get<components::CameraComponent>(entity);
            if (cam.isPrimary)
            {
                const auto& transform = view.get<components::TransformComponent>(entity);
                return transform.position;
            }
        }
        return glm::vec3(0.0f);
    }

    std::string StreamingZoneManager::generateSceneName(const std::string& scenePath, uint32_t zoneId) const
    {
        std::filesystem::path p(scenePath);
        std::string stem = p.stem().string();
        return stem + "_zone_" + std::to_string(zoneId);
    }
}
