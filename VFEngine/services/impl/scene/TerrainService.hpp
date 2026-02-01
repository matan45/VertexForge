#pragma once
#include "../../interfaces/ITerrainService.hpp"
#include "../../data/EntityHandle.hpp"
#include <memory>

namespace scene
{
    class SceneGraphSystem;
}

namespace events
{
    class EventDispatcher;
}

namespace terrain
{
    class TerrainGrid;
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

    public:
        explicit TerrainService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);
        ~TerrainService() override;

        void registerEventHandlers() override;

        EntityHandle createTerrain(const TerrainCreationData& config) override;
        bool deleteTerrain(EntityHandle terrainEntity) override;
        std::optional<TerrainData> getTerrainData(EntityHandle entity) const override;
        bool hasTerrainComponent(EntityHandle entity) const override;

    private:
        // Create child entities for each tile in the grid
        void createTileEntities(EntityHandle parentEntity, terrain::TerrainGrid& grid);
    };
}
