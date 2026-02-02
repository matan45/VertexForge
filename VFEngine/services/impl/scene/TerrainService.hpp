#pragma once
#include "../../interfaces/ITerrainService.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../events/TerrainEvents.hpp"
#include "../../events/EventDispatcher.hpp"
#include "math/Frustum.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace scene
{
    class SceneGraphSystem;
}

namespace terrain
{
    class TerrainGrid;
    class TerrainTile;
}

namespace services
{
    class TerrainService : public ITerrainService
    {
    private:
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;

        // Active terrain grids (owned by this service)
        // Key: terrain parent entity handle value
        std::unordered_map<uint64_t, std::unique_ptr<terrain::TerrainGrid>> terrainGrids;

        // Subscription for entity deletion events
        std::unique_ptr<::events::SubscriptionToken> entityDeletedSubscription;

    public:
        explicit TerrainService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);
        ~TerrainService() override;

        void registerEventHandlers() override;

        EntityHandle createTerrain(const TerrainCreationData& config) override;
        bool deleteTerrain(EntityHandle terrainEntity) override;
        std::optional<TerrainData> getTerrainData(EntityHandle entity) const override;
        bool hasTerrainComponent(EntityHandle entity) const override;

        // Terrain tile queries
        bool hasTerrainTileComponent(EntityHandle entity) const;
        std::optional<TerrainTileData> getTerrainTileData(EntityHandle entity) const;

        // Collect visible terrain tiles info
        std::vector<::events::terrain::TerrainTileInfo> collectVisibleTiles(
            const math::Frustum& frustum,
            const glm::vec3& cameraPosition);

        // Update LODs for all terrain grids
        void updateAllTerrainLODs(const glm::vec3& cameraPosition);

        // Get raw terrain tiles for GPU rendering (used by TerrainRenderAdapter)
        std::vector<terrain::TerrainTile*> getRawVisibleTiles(
            const math::Frustum& frustum,
            const glm::vec3& cameraPosition);

        // Check if any terrain grids exist
        bool hasActiveTerrain() const { return !terrainGrids.empty(); }

        // Get total tile count across all terrains
        size_t getTotalTileCount() const;

        // Sync ECS components from TerrainTile data
        void syncTileComponents(EntityHandle terrainEntity);

        // Mark tiles as GPU resident (called after GPU upload)
        void setTilesGPUResident(EntityHandle terrainEntity, bool resident);

        // Get terrain grid for an entity (internal use)
        terrain::TerrainGrid* getTerrainGrid(uint64_t entityId);

    private:
        // Create child entities for each tile in the grid
        void createTileEntities(EntityHandle parentEntity, terrain::TerrainGrid& grid);

        // Handle entity deletion - clean up if it's a terrain
        void onEntityDeleted(EntityHandle entity);
    };
}
