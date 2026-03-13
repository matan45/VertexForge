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
#include "../../events/scene/ScenePersistenceEvents.hpp"
#include "../../events/scene/EntityTransformEvents.hpp"
#include "../../data/EntityConversion.hpp"
#include "print/Log.hpp"
#include <nlohmann/json.hpp>

namespace
{
    // O(N) lookup — consider replacing with a UUID→entity cache if this becomes a bottleneck
    entt::entity findEntityByUUID(uint64_t uuid)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto uuidView = registry.view<components::UUIDComponent>();
        for (auto entity : uuidView)
        {
            if (uuidView.get<components::UUIDComponent>(entity).id.getValue() == uuid)
                return entity;
        }
        return entt::null;
    }
}

namespace services
{
    void WorldSectorServiceImpl::update()
    {
        if (!worldMode)
            return;

        // Only stream sectors during play mode (based on primary camera distance).
        // In edit mode, sectors stay as-is — no auto load/unload from editor camera.
        if (isPlayMode)
        {
            glm::vec3 cameraPos = getPrimaryCameraPosition();
            streamer.update(cameraPos, sectorManager, streamingActions);

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
                        auto ent = findEntityByUUID(uuid);
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

        sector->state = world::SectorState::Loading;

        std::vector<nlohmann::json> entityData;
        if (!world::WorldSectorSerialization::loadSector(sector->filePath, entityData))
        {
            sector->state = world::SectorState::Unloaded;
            return;
        }

        sector->entityUUIDs.clear();
        std::vector<std::pair<std::string, std::string>> entityNamesAndJson;
        entityNamesAndJson.reserve(entityData.size());
        for (const auto& data : entityData)
        {
            if (data.contains("uuid") && data["uuid"].is_number_unsigned())
            {
                sector->entityUUIDs.push_back(data["uuid"].get<uint64_t>());
            }
            entityNamesAndJson.emplace_back(data.value("name", "Unnamed"), data.dump());
        }

        // Queue deferred entity loading using pre-parsed data (avoids reading file twice)
        entityLoader.queueSectorLoadFromData(coord, entityNamesAndJson);

        // State stays Loading until all entities are processed (checked in update())
        sector->dirty = false; // Just loaded from disk — nothing to save
    }

    void WorldSectorServiceImpl::handleSectorUnload(const world::SectorCoord& coord)
    {
        auto* sector = sectorManager.getSector(coord);
        if (!sector)
            return;

        sector->state = world::SectorState::Unloading;

        // Unregister sector lights before entities are destroyed
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
            auto ent = findEntityByUUID(uuid);
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

        float sectorSize = sectorManager.getConfig().sectorWorldSize;
        float boxHeight = 10.0f; // Visual height for sector boxes
        glm::vec3 halfExtents(sectorSize * 0.5f, boxHeight * 0.5f, sectorSize * 0.5f);

        sectorManager.forEachSector([&](const world::WorldSector& sector)
        {
            float cx = (static_cast<float>(sector.coord.x) + 0.5f) * sectorSize;
            float cz = (static_cast<float>(sector.coord.z) + 0.5f) * sectorSize;
            glm::vec3 center(cx, boxHeight * 0.5f, cz);

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
