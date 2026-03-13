#include "WaterService.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "water/WaterGrid.hpp"
#include "water/WaterTile.hpp"
#include "water/WaterTypes.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../providers/physics/IPhysicsProvider.hpp"

namespace services
{
    std::vector<water::WaterTile*> WaterService::getVisibleWaterTiles(
        const math::Frustum& frustum,
        const glm::vec3& cameraPosition)
    {
        std::vector<water::WaterTile*> result;

        for (auto& [entityId, grid] : waterGrids)
        {
            // Run water streaming before visibility query
            auto streamerIt = waterStreamers.find(entityId);
            if (streamerIt != waterStreamers.end() && streamerIt->second && streamerIt->second->isEnabled())
            {
                auto defIt = definitionMaps.find(entityId);
                if (defIt != definitionMaps.end())
                {
                    EntityHandle waterHandle{entityId};
                    float tileSize = grid->getConfig().worldTileSize;

                    streamerIt->second->update(
                        cameraPosition, tileSize, defIt->second, *grid, waterStreamingActions);

                    bool streamingChanged = false;
                    for (const auto& action : waterStreamingActions)
                    {
                        if (action.isLoad)
                        {
                            // Guard: skip if tile already exists in grid (avoids duplicate entities)
                            if (grid->hasTile(action.coord))
                                continue;

                            const auto* def = defIt->second.getDefinition(action.coord);
                            if (def)
                            {
                                water::WaterTile* tile = grid->getOrCreateTile(action.coord);
                                if (tile)
                                {
                                    tile->updateHeight(def->waterHeight, tileSize);
                                    tile->waveIntensity = def->waveIntensity;
                                    tile->physicsEnabled = def->physicsEnabled;
                                    createTileEntity(waterHandle, tile, tileSize);
                                    streamingChanged = true;
                                }
                            }
                        }
                        else
                        {
                            // Remove tile entity and grid tile
                            scene::Entity parentEntity(internal::fromHandle(waterHandle));
                            for (auto& child : parentEntity.getChildren())
                            {
                                if (!child.hasComponent<components::WaterTileComponent>())
                                    continue;
                                const auto& tileComp = child.getComponent<components::WaterTileComponent>();
                                if (tileComp.tileX == action.coord.x && tileComp.tileZ == action.coord.z)
                                {
                                    EntityHandle tileHandle = internal::toHandle(child.getHandle());
                                    if (physicsProvider)
                                        physicsProvider->removeWaterSensorBody(tileHandle);
                                    sceneGraph->removeEntity(child);
                                    break;
                                }
                            }
                            grid->removeTile(action.coord);
                            streamingChanged = true;
                        }
                    }

                    // Sync component bounds and tile count after streaming changes
                    if (streamingChanged)
                    {
                        auto& registry = scene::EntityRegistry::getRegistry();
                        entt::entity ent = internal::fromHandle(waterHandle);
                        if (registry.valid(ent) && registry.all_of<components::WaterComponent>(ent))
                        {
                            auto& comp = registry.get<components::WaterComponent>(ent);
                            int32_t minX, minZ, maxX, maxZ;
                            grid->computeBounds(minX, minZ, maxX, maxZ);
                            comp.gridMinX = minX;
                            comp.gridMinZ = minZ;
                            comp.gridMaxX = maxX;
                            comp.gridMaxZ = maxZ;
                            comp.activeTileCount = static_cast<uint32_t>(grid->getTileCount());
                        }
                    }
                }
            }

            auto visibleTiles = grid->getVisibleTiles(frustum);

            if (distanceCullingEnabled && maxWaterDistSq > 0.0f)
            {
                for (auto* tile : visibleTiles)
                {
                    glm::vec3 tileCenter = (tile->worldBounds.min + tile->worldBounds.max) * 0.5f;
                    glm::vec3 diff = tileCenter - cameraPosition;
                    float distSq = glm::dot(diff, diff);
                    if (distSq <= maxWaterDistSq)
                    {
                        result.push_back(tile);
                    }
                }
            }
            else
            {
                result.insert(result.end(), visibleTiles.begin(), visibleTiles.end());
            }

            // Update visibleTileCount on the component
            auto& registry = scene::EntityRegistry::getRegistry();
            entt::entity ent = internal::fromHandle(EntityHandle{entityId});
            if (registry.valid(ent) && registry.all_of<components::WaterComponent>(ent))
            {
                registry.get<components::WaterComponent>(ent).visibleTileCount =
                    static_cast<uint32_t>(visibleTiles.size());
            }
        }

        return result;
    }

    std::vector<water::WaterTile*> WaterService::queryVisibleWaterTiles(
        const math::Frustum& frustum,
        const glm::vec3& cameraPosition)
    {
        std::vector<water::WaterTile*> result;

        for (auto& [entityId, grid] : waterGrids)
        {
            for (auto* tile : grid->getAllTiles())
            {
                if (!tile)
                    continue;

                if (!frustum.intersectsAABB(tile->worldBounds))
                    continue;

                if (distanceCullingEnabled && maxWaterDistSq > 0.0f)
                {
                    glm::vec3 tileCenter = (tile->worldBounds.min + tile->worldBounds.max) * 0.5f;
                    glm::vec3 diff = tileCenter - cameraPosition;
                    float distSq = glm::dot(diff, diff);
                    if (distSq > maxWaterDistSq)
                        continue;
                }

                result.push_back(tile);
            }
        }

        return result;
    }
}
