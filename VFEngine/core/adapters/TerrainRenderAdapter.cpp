#include "TerrainRenderAdapter.hpp"
#include "../../services/impl/scene/TerrainService.hpp"

namespace core
{
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
}
