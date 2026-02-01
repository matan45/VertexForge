#pragma once
#include "../../interfaces/ITerrainService.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../events/TerrainEvents.hpp"
#include "math/Frustum.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

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

        // Collect visible terrain tiles info
        std::vector<::events::terrain::TerrainTileInfo> collectVisibleTiles(
            const math::Frustum& frustum,
            const glm::vec3& cameraPosition);

        // Update LODs for all terrain grids
        void updateAllTerrainLODs(const glm::vec3& cameraPosition);

    private:
        // Create child entities for each tile in the grid
        void createTileEntities(EntityHandle parentEntity, terrain::TerrainGrid& grid);
    };
}
