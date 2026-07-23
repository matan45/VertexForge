#pragma once

#include "../../services/providers/terrain/ITerrainRenderProvider.hpp"
#include <memory>
#include <atomic>

namespace events
{
    struct SubscriptionToken;
}

namespace services
{
    class TerrainService;
}

namespace core
{
    class TerrainRenderAdapter : public services::ITerrainRenderProvider
    {
    private:
        services::TerrainService* terrainService = nullptr;
        std::atomic<bool> terrainMaterialDirty{false};
        std::unique_ptr<events::SubscriptionToken> materialCompiledToken;

    public:
        explicit TerrainRenderAdapter();
        ~TerrainRenderAdapter() override;

        void setTerrainService(services::TerrainService* service) { terrainService = service; }

        std::vector<terrain::TerrainTile*> getVisibleTiles(
            const math::Frustum& frustum,
            const glm::vec3& cameraPosition) override;

        std::vector<terrain::TerrainTile*> queryVisibleTiles(
            const math::Frustum& frustum,
            const glm::vec3& cameraPosition) override;

        std::vector<terrain::TerrainTile*> getAllLoadedTiles() override;

        bool hasActiveTerrain() const override;

        std::string getTerrainMaterialPath() const override;
        void getTerrainGridWorldBounds(glm::vec2& outMin, glm::vec2& outMax) const override;

        const std::vector<foliage::FoliageType>& getFoliagePalette() const override;

        void setDistanceCullingEnabled(bool enabled) override;
        void setMaxDrawDistance(float distance) override;

        bool ensureTileLODData(terrain::TerrainTile& tile, uint8_t lodLevel) override;
        void releaseTileRAMData(terrain::TerrainTile& tile) override;

        services::TileLoadContextResult prepareTileLoadContext(int32_t coordX, int32_t coordZ) override;

        void markTerrainMaterialDirty() override;
        bool consumeTerrainMaterialDirty() override;
    };
}
