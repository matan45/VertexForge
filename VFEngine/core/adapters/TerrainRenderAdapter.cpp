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

    bool TerrainRenderAdapter::hasActiveTerrain() const
    {
        return terrainService && terrainService->hasActiveTerrain();
    }

    std::string TerrainRenderAdapter::getTerrainMaterialPath() const
    {
        if (!terrainService) return {};
        return terrainService->getTerrainMaterialPath();
    }
}
