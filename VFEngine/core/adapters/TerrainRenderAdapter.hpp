#pragma once

#include "../../services/providers/ITerrainRenderProvider.hpp"
#include <memory>

namespace services
{
    class TerrainService;
}

namespace core
{
    // Adapter that implements ITerrainRenderProvider by wrapping TerrainService.
    // Provides access to raw terrain tiles for GPU-driven rendering.
    class TerrainRenderAdapter : public services::ITerrainRenderProvider
    {
    private:
        services::TerrainService* terrainService = nullptr;

    public:
        TerrainRenderAdapter() = default;
        ~TerrainRenderAdapter() override = default;

        // Late binding - called after TerrainService is created
        void setTerrainService(services::TerrainService* service) { terrainService = service; }

        // ITerrainRenderProvider implementation
        std::vector<terrain::TerrainTile*> getVisibleTiles(
            const math::Frustum& frustum,
            const glm::vec3& cameraPosition) override;

        void updateLODs(const glm::vec3& cameraPosition) override;

        bool hasActiveTerrain() const override;

        size_t getTotalTileCount() const override;
    };
}
