#include "TerrainService.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "terrain/TerrainGrid.hpp"
#include "terrain/TerrainTypes.hpp"
#include "terrain/TerrainTile.hpp"
#include "../../data/EntityConversion.hpp"

namespace services
{
    std::vector<terrain::TerrainTile*> TerrainService::getRawVisibleTiles(
        const math::Frustum& frustum,
        const glm::vec3& cameraPosition)
    {
        std::vector<terrain::TerrainTile*> result;

        for (auto& [entityId, grid] : terrainGrids)
        {
            // Run world streaming before LOD updates (skip during save to avoid data races)
            auto streamerIt = worldStreamers.find(entityId);
            if (streamerIt != worldStreamers.end() && streamerIt->second && streamerIt->second->isEnabled()
                && !saveInProgress.load(std::memory_order_acquire))
            {
                auto cacheIt = fileCaches.find(entityId);
                if (cacheIt != fileCaches.end() && cacheIt->second)
                {
                    EntityHandle terrainHandle{entityId};
                    auto& comp = scene::EntityRegistry::getRegistry()
                        .get<components::TerrainComponent>(internal::fromHandle(terrainHandle));

                    streamerIt->second->update(
                        cameraPosition, comp.worldTileSize, *cacheIt->second, *grid, streamingActions);

                    for (const auto& action : streamingActions)
                    {
                        if (action.isLoad)
                            streamInTile(terrainHandle, action.coord.x, action.coord.z);
                        else
                            streamOutTile(terrainHandle, action.coord.x, action.coord.z);
                    }

                    if (!streamingActions.empty())
                        commitStreamingChanges(terrainHandle);
                }
            }

            grid->regenerateDirtyTiles(cameraPosition);

            // Create physics bodies for tiles that streamed in and now have height data
            if (!pendingPhysicsTiles.empty() && physicsProvider)
            {
                auto it = pendingPhysicsTiles.begin();
                while (it != pendingPhysicsTiles.end())
                {
                    if (it->first != entityId)
                    {
                        ++it;
                        continue;
                    }

                    auto* tile = grid->getTile(it->second);
                    if (!tile || !tile->hasHeightData())
                    {
                        ++it;
                        continue;
                    }

                    std::vector<float> physicsHeights;
                    auto info = buildTileColliderInfo(*tile, EntityHandle{entityId}, physicsHeights);
                    physicsProvider->addTerrainTileCollider(EntityHandle{entityId}, info);
                    it = pendingPhysicsTiles.erase(it);
                }
            }

            auto visibleTiles = grid->getVisibleTiles(frustum);

            if (distanceCullingEnabled_ && maxTerrainDistSq_ > 0.0f)
            {
                for (terrain::TerrainTile* tile : visibleTiles)
                {
                    if (!tile || !tile->isVisible)
                        continue;

                    glm::vec3 tileCenter = (tile->worldBounds.min + tile->worldBounds.max) * 0.5f;
                    glm::vec3 diff = tileCenter - cameraPosition;
                    float distSq = glm::dot(diff, diff);
                    if (distSq <= maxTerrainDistSq_)
                    {
                        result.push_back(tile);
                    }
                }
            }
            else
            {
                for (terrain::TerrainTile* tile : visibleTiles)
                {
                    if (tile && tile->isVisible)
                    {
                        result.push_back(tile);
                    }
                }
            }
        }

        return result;
    }

    std::vector<terrain::TerrainTile*> TerrainService::getAllLoadedTiles()
    {
        std::vector<terrain::TerrainTile*> result;

        for (auto& [entityId, grid] : terrainGrids)
        {
            for (auto* tile : grid->getAllTiles())
            {
                if (tile)
                {
                    result.push_back(tile);
                }
            }
        }

        return result;
    }

    std::vector<terrain::TerrainTile*> TerrainService::queryVisibleTiles(
        const math::Frustum& frustum,
        const glm::vec3& cameraPosition)
    {
        std::vector<terrain::TerrainTile*> result;

        for (auto& [entityId, grid] : terrainGrids)
        {
            auto visible = grid->getVisibleTiles(frustum);

            if (distanceCullingEnabled_ && maxTerrainDistSq_ > 0.0f)
            {
                for (auto* tile : visible)
                {
                    glm::vec3 tileCenter = (tile->worldBounds.min + tile->worldBounds.max) * 0.5f;
                    glm::vec3 diff = tileCenter - cameraPosition;
                    float distSq = glm::dot(diff, diff);
                    if (distSq <= maxTerrainDistSq_)
                        result.push_back(tile);
                }
            }
            else
            {
                result.insert(result.end(), visible.begin(), visible.end());
            }
        }

        return result;
    }
}
