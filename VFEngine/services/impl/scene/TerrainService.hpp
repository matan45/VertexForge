#pragma once
#include "../../interfaces/ITerrainService.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../events/TerrainEvents.hpp"
#include "../../events/EventDispatcher.hpp"
#include "terrain/TerrainTypes.hpp"
#include "math/Frustum.hpp"
#include "../../providers/ITerrainBrushComputeProvider.hpp"
#include "../../providers/IPhysicsProvider.hpp"
#include "terrain/TerrainSerializer.hpp"
#include "terrain/TerrainFileCache.hpp"
#include <glm/glm.hpp>
#include <atomic>
#include <future>
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

        std::unordered_map<uint64_t, std::unique_ptr<terrain::TerrainGrid>> terrainGrids;

        std::unique_ptr<::events::SubscriptionToken> entityDeletedSubscription;
        std::unique_ptr<::events::SubscriptionToken> sceneClearedSubscription;

        float flattenTargetHeight = 0.0f;
        bool flattenTargetCaptured = false;

        ITerrainBrushComputeProvider* brushComputeProvider = nullptr;
        IPhysicsProvider* physicsProvider = nullptr;
        std::atomic<bool> saveInProgress{false};

        std::unordered_map<uint64_t, std::shared_ptr<terrain::TerrainFileCache>> fileCaches;

        struct PendingTerrainLoad {
            std::future<bool> ioFuture;
            terrain::TerrainFileHeader header;
            std::vector<terrain::TileIndexEntry> index;
            std::string path;
        };
        std::unique_ptr<PendingTerrainLoad> pendingLoad;

    public:
        explicit TerrainService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);
        ~TerrainService() override;

        void registerEventHandlers() override;

        EntityHandle createTerrain(const TerrainCreationData& config) override;
        bool deleteTerrain(EntityHandle terrainEntity) override;
        std::optional<TerrainData> getTerrainData(EntityHandle entity) const override;
        bool hasTerrainComponent(EntityHandle entity) const override;

        bool hasTerrainTileComponent(EntityHandle entity) const;
        std::optional<TerrainTileData> getTerrainTileData(EntityHandle entity) const;

        std::vector<terrain::TerrainTile*> getRawVisibleTiles(
            const math::Frustum& frustum,
            const glm::vec3& cameraPosition);

        bool hasActiveTerrain() const { return !terrainGrids.empty(); }
        std::string getTerrainMaterialPath() const;

        void applyBrush(const glm::vec3& worldPosition, float deltaTime, bool invert, bool isFirstApplication);
        void applyPaintBrush(const glm::vec3& worldPosition, float deltaTime, bool invert, bool isFirstApplication);

        void setBrushComputeProvider(ITerrainBrushComputeProvider* provider) { brushComputeProvider = provider; }
        void setPhysicsProvider(IPhysicsProvider* provider) { physicsProvider = provider; }

        bool addTerrainCollider(EntityHandle terrainEntity);
        void removeTerrainCollider(EntityHandle terrainEntity);
        bool hasTerrainCollider(EntityHandle terrainEntity) const;

        bool saveWeightMaps(uint64_t terrainEntityId, const std::string& path);
        bool loadWeightMaps(uint64_t terrainEntityId, const std::string& path);

        bool saveTerrain(uint64_t terrainEntityId, const std::string& path);
        EntityHandle loadTerrain(const std::string& path);

        bool ensureTileLODData(terrain::TerrainTile& tile, uint8_t lodLevel);
        void releaseTileRAMData(terrain::TerrainTile& tile);

        ::events::terrain::TerrainGeometryResult getTerrainGeometryForNavmesh() const;

    private:
        void registerTerrainCoreHandlers(::events::EventDispatcher& dispatcher);
        void registerBrushHandlers(::events::EventDispatcher& dispatcher);
        void registerTerrainDataHandlers(::events::EventDispatcher& dispatcher);
        void registerAsyncLoadHandlers(::events::EventDispatcher& dispatcher);

        void createTileEntities(EntityHandle parentEntity, terrain::TerrainGrid& grid);
        void remapTerrainEntities();
        void onEntityDeleted(EntityHandle entity);
        void onSceneCleared();
        void syncWeightMapLayerCount(uint64_t terrainEntityId, const std::string& materialPath);
        EntityHandle finishLoadTerrain(terrain::TerrainFileHeader& header,
                                       std::vector<terrain::TileIndexEntry>& index,
                                       const std::string& path);

        void rebuildModifiedColliders(EntityHandle targetEntity, terrain::TerrainGrid* grid,
                                      const std::vector<terrain::TileCoord>& modifiedTiles);
        void generateDebugWireframes(EntityHandle terrainEntity, terrain::TerrainGrid* grid);
        uint16_t getOverlayMask() const;
    };
}
