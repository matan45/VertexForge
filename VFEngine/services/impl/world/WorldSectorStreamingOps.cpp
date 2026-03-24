#include "WorldSectorServiceImpl.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "world/WorldSectorSerialization.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/world/WorldSectorEvents.hpp"
#include "../../events/render/DebugDrawEvents.hpp"
#include "../../events/render/LightStreamingEvents.hpp"
#include "../../events/render/ObjectStreamingEvents.hpp"
#include "../../events/scene/ScenePersistenceEvents.hpp"
#include "../../events/scene/EntityTransformEvents.hpp"
#include "../../data/EntityConversion.hpp"
#include "print/Log.hpp"
#include <nlohmann/json.hpp>

namespace
{
    static constexpr uint32_t kMaxConcurrentSectorLoads = 4;
}

namespace services
{
    void WorldSectorServiceImpl::update()
    {
        if (!worldMode)
            return;

        // Poll completed async sector loads (works in both edit and play mode)
        pollAsyncSectorLoads();

        // Only stream sectors during play mode (based on primary camera distance).
        // In edit mode, sectors stay as-is — no auto load/unload from editor camera.
        if (isPlayMode)
        {
            // Build streaming sources: camera is always source[0]
            std::vector<world::StreamingSource> sources;
            {
                world::StreamingSource cameraSrc;
                cameraSrc.position = getPrimaryCameraPosition();
                cameraSrc.radiusMultiplier = 1.0f;
                cameraSrc.priority = 0;
                cameraSrc.id = 0;
                sources.push_back(cameraSrc);
            }
            for (const auto& [id, src] : streamingSources)
                sources.push_back(src);

            streamer.update(sources, sectorManager, streamingActions);

            for (const auto& action : streamingActions)
            {
                if (action.isLoad)
                {
                    handleSectorLoad(action.coord);
                }
                else
                {
                    handleSectorUnload(action.coord);
                }
            }
        }

        drawDebugSectors();

        entityLoader.update(*sceneGraph, worldDefinition.streamingConfig.maxEntitiesPerFrame);

        // Transition sectors from Loading to Loaded once all their entities are processed
        sectorManager.forEachSector([&](world::WorldSector& sector)
        {
            if (sector.state == world::SectorState::Loading &&
                !entityLoader.hasPendingLoadsForSector(sector.coord))
            {
                sector.state = world::SectorState::Loaded;

                // Register sector lights for streaming
                {
                    auto& registry = scene::EntityRegistry::getRegistry();
                    std::vector<uint32_t> lightEntityIds;
                    for (uint64_t uuid : sector.entityUUIDs)
                    {
                        auto ent = scene::EntityRegistry::findByUUID(uuid);
                        if (ent != entt::null)
                        {
                            if (registry.any_of<components::PointLightComponent,
                                                components::SpotLightComponent>(ent))
                            {
                                lightEntityIds.push_back(static_cast<uint32_t>(ent));
                            }
                        }
                    }
                    if (!lightEntityIds.empty())
                    {
                        events::render::lightstreaming::RegisterSectorLightsCommand cmd;
                        cmd.sectorId = world::sectorCoordToId(sector.coord);
                        cmd.lightEntityIds = std::move(lightEntityIds);
                        ::events::EventDispatcher::instance().execute(cmd);
                    }
                }

                // Register sector mesh objects for GPU streaming (including sub-entities)
                {
                    auto& registry = scene::EntityRegistry::getRegistry();
                    std::vector<std::pair<uint64_t, entt::entity>> meshEntities;

                    auto collectMeshEntities = [&](auto&& self, entt::entity ent) -> void
                    {
                        if (ent == entt::null || !registry.valid(ent)) return;

                        if (registry.any_of<components::MeshComponent>(ent))
                        {
                            auto* uuidComp = registry.try_get<components::UUIDComponent>(ent);
                            if (uuidComp)
                            {
                                meshEntities.emplace_back(uuidComp->id.getValue(), ent);
                            }
                        }

                        auto* childrenComp = registry.try_get<components::ChildrenComponent>(ent);
                        if (childrenComp)
                        {
                            for (auto child : childrenComp->children)
                            {
                                self(self, child);
                            }
                        }
                    };

                    for (uint64_t uuid : sector.entityUUIDs)
                    {
                        auto ent = scene::EntityRegistry::findByUUID(uuid);
                        if (ent != entt::null)
                        {
                            collectMeshEntities(collectMeshEntities, ent);
                        }
                    }

                    if (!meshEntities.empty())
                    {
                        events::render::objectstreaming::RegisterSectorObjectsCommand cmd;
                        cmd.sectorId = world::sectorCoordToId(sector.coord);
                        cmd.entities = std::move(meshEntities);
                        ::events::EventDispatcher::instance().execute(cmd);
                    }
                }

                // Clear any unconsumed snapshots for this sector's entities
                for (uint64_t uuid : sector.entityUUIDs)
                {
                    physicsSnapshots.erase(uuid);
                    animationSnapshots.erase(uuid);
                    vfxSnapshots.erase(uuid);
                    audioSnapshots.erase(uuid);
                }

                ::events::world::SectorLoadedNotification notif;
                notif.coord = sector.coord;
                notif.entityCount = static_cast<uint32_t>(sector.entityUUIDs.size());
                ::events::EventDispatcher::instance().publish(notif);

                referenceResolver.onSectorLoaded(sector.entityUUIDs);
            }
        });
    }

    bool WorldSectorServiceImpl::loadSector(const world::SectorCoord& coord)
    {
        auto* sector = sectorManager.getSector(coord);
        if (!sector || sector->filePath.empty())
        {
            vfLogError("Cannot load sector ({},{}): no file path", coord.x, coord.z);
            return false;
        }

        handleSectorLoad(coord);
        return true;
    }

    bool WorldSectorServiceImpl::unloadSector(const world::SectorCoord& coord)
    {
        auto* sector = sectorManager.getSector(coord);
        if (!sector || sector->state != world::SectorState::Loaded)
            return false;

        handleSectorUnload(coord);
        return true;
    }

    void WorldSectorServiceImpl::handleSectorLoad(const world::SectorCoord& coord)
    {
        auto* sector = sectorManager.getSector(coord);
        if (!sector || sector->filePath.empty())
            return;

        // Already have a pending async load for this sector
        if (pendingAsyncLoads.contains(coord))
            return;

        // Respect concurrency limit — streamer will re-emit next frame
        if (pendingAsyncLoads.size() >= kMaxConcurrentSectorLoads)
            return;

        sector->state = world::SectorState::Loading;

        // Notify subsystems (e.g. terrain) that this sector is now active
        {
            ::events::world::SectorActivatedNotification notif;
            notif.coord = coord;
            notif.sectorConfig = sectorManager.getConfig();
            ::events::EventDispatcher::instance().publish(notif);
        }

        std::string filePath = sector->filePath;

        auto future = std::async(std::launch::async, [filePath]() -> AsyncSectorLoadResult {
            AsyncSectorLoadResult result;
            result.success = world::WorldSectorSerialization::loadSector(filePath, result.entityData);
            return result;
        });

        PendingAsyncSectorLoad pending;
        pending.coord = coord;
        pending.future = std::move(future);
        pending.cancelled = false;
        pendingAsyncLoads.emplace(coord, std::move(pending));
    }

    void WorldSectorServiceImpl::pollAsyncSectorLoads()
    {
        auto it = pendingAsyncLoads.begin();
        while (it != pendingAsyncLoads.end())
        {
            if (it->second.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            {
                ++it;
                continue;
            }

            auto result = it->second.future.get();
            auto coord = it->second.coord;
            bool wasCancelled = it->second.cancelled;
            it = pendingAsyncLoads.erase(it);

            if (wasCancelled)
                continue;

            auto* sector = sectorManager.getSector(coord);
            if (!sector)
                continue;

            if (!result.success)
            {
                sector->state = world::SectorState::Unloaded;
                vfLogError("Async sector load failed for ({},{})", coord.x, coord.z);
                continue;
            }

            finalizeSectorLoad(coord, result.entityData);
        }
    }

    void WorldSectorServiceImpl::finalizeSectorLoad(const world::SectorCoord& coord,
                                                     std::vector<nlohmann::json>& entityData)
    {
        auto* sector = sectorManager.getSector(coord);
        if (!sector)
            return;

        sector->entityUUIDs.clear();
        std::vector<std::pair<std::string, nlohmann::json>> entityNamesAndJson;
        entityNamesAndJson.reserve(entityData.size());
        for (auto& data : entityData)
        {
            if (data.contains("uuid") && data["uuid"].is_number_unsigned())
            {
                sector->entityUUIDs.push_back(data["uuid"].get<uint64_t>());
            }
            std::string name = data.value("name", "Unnamed");
            entityNamesAndJson.emplace_back(std::move(name), std::move(data));
        }

        entityLoader.queueSectorLoadFromData(coord, entityNamesAndJson);

        // State stays Loading until all entities are processed (checked in update())
        sector->dirty = false; // Just loaded from disk — nothing to save
    }

    void WorldSectorServiceImpl::handleSectorUnload(const world::SectorCoord& coord)
    {
        auto* sector = sectorManager.getSector(coord);
        if (!sector)
            return;

        // Cancel any in-flight async file I/O for this sector.
        // The background thread still runs to completion (std::async has no cooperative
        // cancellation), but pollAsyncSectorLoads() will discard the result.
        auto asyncIt = pendingAsyncLoads.find(coord);
        if (asyncIt != pendingAsyncLoads.end())
        {
            asyncIt->second.cancelled = true;
        }

        sector->state = world::SectorState::Unloading;

        // Notify subsystems (e.g. terrain) that this sector is now inactive
        {
            ::events::world::SectorDeactivatedNotification notif;
            notif.coord = coord;
            notif.sectorConfig = sectorManager.getConfig();
            ::events::EventDispatcher::instance().publish(notif);
        }

        // Unregister sector objects and lights before entities are destroyed
        {
            events::render::objectstreaming::UnregisterSectorObjectsCommand objCmd;
            objCmd.sectorId = world::sectorCoordToId(coord);
            ::events::EventDispatcher::instance().execute(objCmd);
        }
        {
            events::render::lightstreaming::UnregisterSectorLightsCommand cmd;
            cmd.sectorId = world::sectorCoordToId(coord);
            ::events::EventDispatcher::instance().execute(cmd);
        }

        // Cancel any pending entity loads for this sector (prevents recreating entities after unload)
        entityLoader.cancelPendingLoads(coord);

        // Separate static entities (to unload) from dynamic entities (to keep alive)
        std::vector<uint64_t> staticUUIDs;
        std::vector<uint64_t> dynamicUUIDs;

        for (uint64_t uuid : sector->entityUUIDs)
        {
            bool isDynamic = false;
            auto ent = scene::EntityRegistry::findByUUID(uuid);
            if (ent != entt::null)
            {
                scene::Entity sceneEntity(ent);
                if (sceneEntity.hasComponent<components::TransformComponent>())
                {
                    isDynamic = !sceneEntity.getComponent<components::TransformComponent>().isStatic;
                }
            }

            if (isDynamic)
                dynamicUUIDs.push_back(uuid);
            else
                staticUUIDs.push_back(uuid);
        }

        referenceResolver.onSectorUnloaded(staticUUIDs);

        // Only unload static entities — dynamic entities persist in the scene
        entityLoader.queueSectorUnload(coord, staticUUIDs);

        // Clear the sector's entity list, then re-add dynamic entities so they remain tracked
        sector->entityUUIDs = dynamicUUIDs;
        sector->state = world::SectorState::Unloaded;

        ::events::world::SectorUnloadedNotification notif;
        notif.coord = coord;
        ::events::EventDispatcher::instance().publish(notif);
    }

    void WorldSectorServiceImpl::onTransformChanged(uint64_t uuid, const glm::vec3& newPosition)
    {
        world::SectorCoord oldCoord = sectorManager.getEntitySector(uuid);
        world::SectorCoord newCoord = sectorManager.worldPositionToSectorCoord(newPosition);

        if (oldCoord == newCoord)
            return;

        sectorManager.removeEntityFromSector(uuid, oldCoord);
        sectorManager.assignEntityToSector(uuid, newPosition);
    }

    glm::vec3 WorldSectorServiceImpl::getPrimaryCameraPosition() const
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        auto primaryCameraOpt = dispatcher.query(::events::scene::GetPrimaryCameraQuery{});
        if (!primaryCameraOpt.has_value())
            return cachedCameraPos;

        ::events::scene::GetWorldTransformQuery transformQuery;
        transformQuery.entity = *primaryCameraOpt;
        auto transformOpt = dispatcher.query(transformQuery);
        if (transformOpt.has_value())
            return transformOpt->position;

        return cachedCameraPos;
    }

    void WorldSectorServiceImpl::drawDebugSectors() const
    {
        if (!debugDrawSectors)
            return;

        auto& dispatcher = ::events::EventDispatcher::instance();
        auto& registry = scene::EntityRegistry::getRegistry();

        float sectorSize = sectorManager.getConfig().sectorWorldSize;

        sectorManager.forEachSector([&](const world::WorldSector& sector)
        {
            float cx = (static_cast<float>(sector.coord.x) + 0.5f) * sectorSize;
            float cz = (static_cast<float>(sector.coord.z) + 0.5f) * sectorSize;

            // Compute Y bounds from entity positions in this sector
            float yMin = 0.0f;
            float yMax = 10.0f;
            bool hasEntities = false;
            for (uint64_t uuid : sector.entityUUIDs)
            {
                auto ent = scene::EntityRegistry::findByUUID(uuid);
                if (ent != entt::null && registry.any_of<components::TransformComponent>(ent))
                {
                    float y = registry.get<components::TransformComponent>(ent).position.y;
                    if (!hasEntities)
                    {
                        yMin = y;
                        yMax = y + 1.0f;
                        hasEntities = true;
                    }
                    else
                    {
                        yMin = std::min(yMin, y);
                        yMax = std::max(yMax, y + 1.0f);
                    }
                }
            }
            // Add padding
            yMin -= 1.0f;
            yMax += 1.0f;

            float boxHeight = yMax - yMin;
            float cy = (yMin + yMax) * 0.5f;
            glm::vec3 center(cx, cy, cz);
            glm::vec3 halfExtents(sectorSize * 0.5f, boxHeight * 0.5f, sectorSize * 0.5f);

            glm::vec4 color;
            switch (sector.state)
            {
            case world::SectorState::Loaded:
                color = glm::vec4(0.2f, 0.9f, 0.2f, 1.0f); // Green
                break;
            case world::SectorState::Loading:
                color = glm::vec4(0.9f, 0.9f, 0.2f, 1.0f); // Yellow
                break;
            case world::SectorState::Unloading:
                color = glm::vec4(0.9f, 0.3f, 0.3f, 1.0f); // Red
                break;
            default:
                color = glm::vec4(0.5f, 0.5f, 0.5f, 0.6f); // Gray
                break;
            }

            ::events::debugdraw::DrawBoxCommand boxCmd;
            boxCmd.center = center;
            boxCmd.halfExtents = halfExtents;
            boxCmd.color = color;
            dispatcher.execute(boxCmd);
        });
    }

} // namespace services
