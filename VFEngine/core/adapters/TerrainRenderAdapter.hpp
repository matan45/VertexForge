#pragma once

#include "../../services/providers/ITerrainRenderProvider.hpp"
#include <memory>

namespace services
{
    class TerrainService;
}

namespace core
{
    // Provides access to raw terrain tiles for GPU-driven rendering
    class TerrainRenderAdapter : public services::ITerrainRenderProvider
    {
    private:
        services::TerrainService* terrainService = nullptr;

    public:
        TerrainRenderAdapter() = default;
        ~TerrainRenderAdapter() override = default;

        void setTerrainService(services::TerrainService* service) { terrainService = service; }

        std::vector<terrain::TerrainTile*> getVisibleTiles(
            const math::Frustum& frustum,
            const glm::vec3& cameraPosition) override;

        bool hasActiveTerrain() const override;

        std::string getTerrainMaterialPath() const override;
    };
}
