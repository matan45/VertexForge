#include "TerrainRenderAdapter.hpp"
#include "../../services/impl/scene/TerrainService.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include "../../services/events/LightBakeEvents.hpp"

namespace core
{
    TerrainRenderAdapter::TerrainRenderAdapter()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        auto bakeToken = dispatcher.subscribe<services::events::lightbake::BakeCompletedNotification>(
            [this](const services::events::lightbake::BakeCompletedNotification&)
            {
                terrainLightmapDirty_.store(true);
            });
        bakeCompleteToken_ = std::make_unique<events::SubscriptionToken>(bakeToken);

        auto loadToken = dispatcher.subscribe<services::events::lightbake::LightmapLoadedNotification>(
            [this](const services::events::lightbake::LightmapLoadedNotification&)
            {
                terrainLightmapDirty_.store(true);
            });
        lightmapLoadedToken_ = std::make_unique<events::SubscriptionToken>(loadToken);
    }

    TerrainRenderAdapter::~TerrainRenderAdapter()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        if (bakeCompleteToken_ && bakeCompleteToken_->isValid())
            dispatcher.unsubscribe(*bakeCompleteToken_);
        if (lightmapLoadedToken_ && lightmapLoadedToken_->isValid())
            dispatcher.unsubscribe(*lightmapLoadedToken_);
    }

    std::vector<terrain::TerrainTile*> TerrainRenderAdapter::getVisibleTiles(
        const math::Frustum& frustum,
        const glm::vec3& cameraPosition)
    {
        if (!terrainService)
        {
            return {};
        }

        return terrainService->getRawVisibleTiles(frustum, cameraPosition);
    }

    std::vector<terrain::TerrainTile*> TerrainRenderAdapter::queryVisibleTiles(
        const math::Frustum& frustum,
        const glm::vec3& cameraPosition)
    {
        if (!terrainService)
        {
            return {};
        }

        return terrainService->queryVisibleTiles(frustum, cameraPosition);
    }

    bool TerrainRenderAdapter::hasActiveTerrain() const
    {
        return terrainService && terrainService->hasActiveTerrain();
    }

    std::string TerrainRenderAdapter::getTerrainMaterialPath() const
    {
        if (!terrainService) return {};
        return terrainService->getTerrainMaterialPath();
    }

    void TerrainRenderAdapter::setDistanceCullingEnabled(bool enabled)
    {
        if (terrainService) terrainService->setDistanceCullingEnabled(enabled);
    }

    void TerrainRenderAdapter::setMaxDrawDistance(float distance)
    {
        if (terrainService) terrainService->setMaxDrawDistance(distance);
    }

    bool TerrainRenderAdapter::ensureTileLODData(terrain::TerrainTile& tile, uint8_t lodLevel)
    {
        if (!terrainService) return false;
        return terrainService->ensureTileLODData(tile, lodLevel);
    }

    void TerrainRenderAdapter::releaseTileRAMData(terrain::TerrainTile& tile)
    {
        if (!terrainService) return;
        terrainService->releaseTileRAMData(tile);
    }

    std::vector<services::TerrainTileLightmapInfo> TerrainRenderAdapter::getTerrainLightmapData() const
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        services::events::lightbake::GetTerrainLightmapDataQuery query;
        auto entries = dispatcher.query(query);

        std::vector<services::TerrainTileLightmapInfo> result;
        result.reserve(entries.size());
        for (const auto& entry : entries)
        {
            services::TerrainTileLightmapInfo info;
            info.coordX = entry.coordX;
            info.coordZ = entry.coordZ;
            info.scaleOffset = entry.scaleOffset;
            info.lightmapPath = entry.lightmapPath;
            result.push_back(std::move(info));
        }
        return result;
    }

    bool TerrainRenderAdapter::consumeTerrainLightmapDirty()
    {
        return terrainLightmapDirty_.exchange(false);
    }
}
