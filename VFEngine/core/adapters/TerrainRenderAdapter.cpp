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

    void TerrainRenderAdapter::updateLODs(const glm::vec3& cameraPosition)
    {
        if (terrainService)
        {
            terrainService->updateAllTerrainLODs(cameraPosition);
        }
    }

    bool TerrainRenderAdapter::hasActiveTerrain() const
    {
        return terrainService && terrainService->hasActiveTerrain();
    }

    size_t TerrainRenderAdapter::getTotalTileCount() const
    {
        return terrainService ? terrainService->getTotalTileCount() : 0;
    }
}
