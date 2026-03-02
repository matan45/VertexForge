#pragma once

#include "../../services/providers/ITerrainRenderProvider.hpp"
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
        std::atomic<bool> terrainLightmapDirty{false};
        std::atomic<bool> terrainMaterialDirty_{false};
        std::unique_ptr<events::SubscriptionToken> bakeCompleteToken;
        std::unique_ptr<events::SubscriptionToken> lightmapLoadedToken;
        std::unique_ptr<events::SubscriptionToken> lightmapClearedToken;
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

        bool hasActiveTerrain() const override;

        std::string getTerrainMaterialPath() const override;

        void setDistanceCullingEnabled(bool enabled) override;
        void setMaxDrawDistance(float distance) override;

        bool ensureTileLODData(terrain::TerrainTile& tile, uint8_t lodLevel) override;
        void releaseTileRAMData(terrain::TerrainTile& tile) override;

        std::vector<services::TerrainTileLightmapInfo> getTerrainLightmapData() const override;
        bool consumeTerrainLightmapDirty() override;

        void markTerrainMaterialDirty() override;
        bool consumeTerrainMaterialDirty() override;
    };
}
