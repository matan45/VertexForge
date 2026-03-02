#include "TerrainRenderAdapter.hpp"
#include "../../services/impl/scene/TerrainService.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include "../../services/events/LightBakeEvents.hpp"
#include "../../services/events/TerrainEvents.hpp"

namespace core
{
    TerrainRenderAdapter::TerrainRenderAdapter()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        auto bakeToken = dispatcher.subscribe<services::events::lightbake::BakeCompletedNotification>(
            [this](const services::events::lightbake::BakeCompletedNotification&)
            {
                terrainLightmapDirty.store(true);
            });
        bakeCompleteToken = std::make_unique<events::SubscriptionToken>(bakeToken);

        auto loadToken = dispatcher.subscribe<services::events::lightbake::LightmapLoadedNotification>(
            [this](const services::events::lightbake::LightmapLoadedNotification&)
            {
                terrainLightmapDirty.store(true);
            });
        lightmapLoadedToken = std::make_unique<events::SubscriptionToken>(loadToken);

        auto clearToken = dispatcher.subscribe<services::events::lightbake::LightmapClearedNotification>(
            [this](const services::events::lightbake::LightmapClearedNotification&)
            {
                terrainLightmapDirty.store(true);
            });
        lightmapClearedToken = std::make_unique<events::SubscriptionToken>(clearToken);

        auto matToken = dispatcher.subscribe<::events::terrain::TerrainMaterialCompiledNotification>(
            [this](const ::events::terrain::TerrainMaterialCompiledNotification&)
            {
                terrainMaterialDirty_.store(true);
            });
        materialCompiledToken = std::make_unique<events::SubscriptionToken>(matToken);
    }

    TerrainRenderAdapter::~TerrainRenderAdapter()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        if (bakeCompleteToken && bakeCompleteToken->isValid())
            dispatcher.unsubscribe(*bakeCompleteToken);
        if (lightmapLoadedToken && lightmapLoadedToken->isValid())
            dispatcher.unsubscribe(*lightmapLoadedToken);
        if (lightmapClearedToken && lightmapClearedToken->isValid())
            dispatcher.unsubscribe(*lightmapClearedToken);
        if (materialCompiledToken && materialCompiledToken->isValid())
            dispatcher.unsubscribe(*materialCompiledToken);
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
        return terrainLightmapDirty.exchange(false);
    }

    void TerrainRenderAdapter::markTerrainMaterialDirty()
    {
        terrainMaterialDirty_.store(true);
    }

    bool TerrainRenderAdapter::consumeTerrainMaterialDirty()
    {
        return terrainMaterialDirty_.exchange(false);
    }
}
