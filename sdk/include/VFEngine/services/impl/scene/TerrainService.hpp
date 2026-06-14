#pragma once
#include "../../interfaces/terrain/ITerrainService.hpp"
#include "../../providers/terrain/ITerrainRenderProvider.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../events/terrain/TerrainEvents.hpp"
#include "../../events/EventDispatcher.hpp"
#include "terrain/TerrainTypes.hpp"
#include "math/Frustum.hpp"
#include "../../providers/terrain/ITerrainBrushComputeProvider.hpp"
#include "../../providers/physics/IPhysicsProvider.hpp"
#include "terrain/TerrainSerializer.hpp"
#include "terrain/TerrainFileCache.hpp"
#include "terrain/TerrainWorldStreamer.hpp"
#include "world/WorldTypes.hpp"
#include <glm/glm.hpp>
#include <atomic>
#include <future>
#include <memory>
#include <optional>
#include <vector>

namespace scene
{
    class SceneGraphSystem;
}

namespace terrain
{
    class TerrainGrid;
    class TerrainTile;
    struct CaveSDFData;
}

namespace components
{
    struct TerrainColliderDebugData;
    struct TerrainComponent;
}

namespace events::caveBrush
{
    struct CaveTileState;
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

        glm::vec3 rampStartPos{0.0f};
        bool rampStartCaptured = false;

        ITerrainBrushComputeProvider* brushComputeProvider = nullptr;
        IPhysicsProvider* physicsProvider = nullptr;
        std::atomic<bool> saveInProgress{false};
        bool distanceCullingEnabled_ = false;
        float maxTerrainDistSq_ = 0.0f;

        std::unordered_map<uint64_t, std::shared_ptr<terrain::TerrainFileCache>> fileCaches;
        std::unordered_map<uint64_t, std::unique_ptr<terrain::TerrainWorldStreamer>> worldStreamers;

        // Cave brush undo: per-tile SDF + hole-mask captured before the current stroke
        // first modifies that tile; drained into an undo command when the stroke finalizes.
        struct CaveStrokeTileBefore
        {
            std::vector<float> sdf;
            std::vector<uint8_t> holeMask;
        };
        std::unordered_map<terrain::TileCoord, CaveStrokeTileBefore, terrain::TileCoordHash> caveStrokeBefore;
        uint64_t caveStrokeEntityId = 0;
        std::vector<terrain::StreamingAction> streamingActions; // persistent scratch buffer
        std::vector<std::pair<uint64_t, terrain::TileCoord>> pendingPhysicsTiles;

        // Sector-driven terrain streaming (world mode)
        bool worldModeActive = false;
        world::SectorConfig cachedSectorConfig;
        world::SectorStreamingConfig cachedStreamingConfig;
        std::unique_ptr<::events::SubscriptionToken> sectorActivatedSub;
        std::unique_ptr<::events::SubscriptionToken> sectorDeactivatedSub;
        std::unique_ptr<::events::SubscriptionToken> worldLoadedSub;

        struct PendingTileStreamAction
        {
            uint64_t terrainEntityId;
            terrain::TileCoord coord;
            bool isLoad;
        };
        std::vector<PendingTileStreamAction> pendingSectorTileActions;

        void onSectorActivated(const world::SectorCoord& coord, const world::SectorConfig& config);
        void onSectorDeactivated(const world::SectorCoord& coord, const world::SectorConfig& config);
        void processPendingSectorTileActions();
        void activateTilesForLoadedSectors();

        // Async terrain creation state
        struct PendingTerrainCreation
        {
            TerrainCreationData config;
            std::future<std::unique_ptr<terrain::TerrainGrid>> future;
            std::atomic<float> progress{0.0f};
            std::atomic<bool> done{false};
        };
        std::shared_ptr<PendingTerrainCreation> pendingCreation;

    public:
        explicit TerrainService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);
        ~TerrainService() override;

        void registerEventHandlers() override;

        EntityHandle createTerrain(const TerrainCreationData& config) override;
        bool beginCreateTerrainAsync(const TerrainCreationData& config);
        TerrainCreationPollResult pollCreateTerrain();
        bool deleteTerrain(EntityHandle terrainEntity) override;
        std::optional<TerrainData> getTerrainData(EntityHandle entity) const override;
        bool hasTerrainComponent(EntityHandle entity) const override;

        bool hasTerrainTileComponent(EntityHandle entity) const;
        std::optional<TerrainTileData> getTerrainTileData(EntityHandle entity) const;

        std::vector<terrain::TerrainTile*> getRawVisibleTiles(
            const math::Frustum& frustum,
            const glm::vec3& cameraPosition);

        std::vector<terrain::TerrainTile*> getAllLoadedTiles();

        std::vector<terrain::TerrainTile*> queryVisibleTiles(
            const math::Frustum& frustum,
            const glm::vec3& cameraPosition);

        bool hasActiveTerrain() const { return !terrainGrids.empty(); }
        std::string getTerrainMaterialPath() const;
        void getTerrainGridWorldBounds(glm::vec2& outMin, glm::vec2& outMax) const;

        void setDistanceCullingEnabled(bool enabled) { distanceCullingEnabled_ = enabled; }
        void setMaxDrawDistance(float distance) { maxTerrainDistSq_ = distance * distance; }

        void applyBrush(const glm::vec3& worldPosition, float deltaTime, bool invert, bool isFirstApplication);
        void applyPaintBrush(const glm::vec3& worldPosition, float deltaTime, bool invert, bool isFirstApplication);
        void applyHoleBrush(const glm::vec3& worldPosition, bool erase);
        void applyCaveBrush(const glm::vec3& worldPosition, float deltaTime, bool invert, bool isFirstApplication);
        void finalizeCaveBrush();

        void setBrushComputeProvider(ITerrainBrushComputeProvider* provider) { brushComputeProvider = provider; }
        void setPhysicsProvider(IPhysicsProvider* provider)
        {
            physicsProvider = provider;
        }

        bool addTerrainCollider(EntityHandle terrainEntity);
        void removeTerrainCollider(EntityHandle terrainEntity);
        bool hasTerrainCollider(EntityHandle terrainEntity) const;

        bool saveWeightMaps(uint64_t terrainEntityId, const std::string& path);
        bool loadWeightMaps(uint64_t terrainEntityId, const std::string& path);

        bool prepareSave(uint64_t terrainEntityId);
        bool prepareSaveIncremental(uint64_t terrainEntityId);
        bool saveTerrain(uint64_t terrainEntityId, const std::string& path);
        bool saveTerrainIncremental(uint64_t terrainEntityId, const std::string& path);
        EntityHandle loadTerrain(const std::string& path);

        bool addTile(EntityHandle terrainEntity, int32_t tileX, int32_t tileZ);
        bool removeTile(EntityHandle terrainEntity, int32_t tileX, int32_t tileZ);

        bool streamInTile(EntityHandle terrainEntity, int32_t tileX, int32_t tileZ);
        bool streamOutTile(EntityHandle terrainEntity, int32_t tileX, int32_t tileZ);
        void commitStreamingChanges(EntityHandle terrainEntity);
        void loadAllTiles(EntityHandle terrainEntity);
        bool ensureTileLODData(terrain::TerrainTile& tile, uint8_t lodLevel);
        void releaseTileRAMData(terrain::TerrainTile& tile);
        TileLoadContextResult prepareTileLoadContext(int32_t coordX, int32_t coordZ);

        ::events::terrain::TerrainGeometryResult getTerrainGeometryForNavmesh();
        ::events::terrain::TerrainBakeGeometryResult getTerrainBakeGeometry();
        ::events::terrain::TerrainHeightfieldResult getTerrainHeightfield();
        ::terrain::TerrainHeightAtResult getTerrainHeightAt(float worldX, float worldZ);

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
                                       const std::string& path,
                                       uint64_t indexTableOffset = 0);
        void loadInitialTiles(terrain::TerrainGrid& grid,
                              const terrain::TerrainFileHeader& header,
                              const std::vector<terrain::TileIndexEntry>& index);
        void initTerrainComponent(components::TerrainComponent& comp,
                                  const terrain::TerrainFileHeader& header,
                                  const std::string& path,
                                  uint32_t activeTileCount);
        void publishTerrainCreated(EntityHandle handle, const terrain::TerrainFileHeader& header);

        void createTileEntity(EntityHandle parentEntity, terrain::TerrainTile* tile, int32_t tileX, int32_t tileZ);
        TerrainTileColliderInfo buildTileColliderInfo(const terrain::TerrainTile& tile,
                                                       EntityHandle terrainEntity,
                                                       std::vector<float>& physicsHeightsOut) const;

        void rebuildModifiedColliders(EntityHandle targetEntity, terrain::TerrainGrid* grid,
                                      const std::vector<terrain::TileCoord>& modifiedTiles);
        void syncHoleBoundaries(terrain::TerrainGrid* grid, const std::vector<terrain::TileCoord>& modifiedTiles);
        void syncBrushBoundaryHeights(terrain::TerrainGrid* grid, const std::vector<terrain::TileCoord>& modifiedTiles);
        void applyRamp(EntityHandle targetEntity, terrain::TerrainGrid* grid,
                       const glm::vec3& startPos, const glm::vec3& endPos,
                       const terrain::BrushParams& params);
        void generateDebugWireframes(EntityHandle terrainEntity, terrain::TerrainGrid* grid);
        static bool applyHoleMaskToHeights(const terrain::TerrainTile& tile, std::vector<float>& physicsHeights);
        static bool isVertexAdjacentToHole(const terrain::TerrainTile& tile, uint32_t vx, uint32_t vz);
        static void generateTileColliderWireframe(const terrain::TerrainTile& tile,
                                                  components::TerrainColliderDebugData& out);
        static void appendCaveWireframe(const terrain::TerrainTile& tile,
                                        components::TerrainColliderDebugData& out);
        void appendCaveColliders(EntityHandle terrainEntity,
                                 const std::vector<terrain::TerrainTile*>& allTiles);

        bool saveVegetation(uint64_t terrainEntityId, const std::string& terrainPath);
        bool loadVegetation(uint64_t terrainEntityId, const std::string& terrainPath);
        static std::string getVegetationDirectory(const std::string& terrainPath);

        void registerVegetationBrushHandlers(::events::EventDispatcher& dispatcher);

        void registerCaveBrushHandlers(::events::EventDispatcher& dispatcher);
        void syncCaveBoundaries(terrain::TerrainGrid* grid, const std::vector<terrain::TileCoord>& modifiedTiles);
        void syncCaveNeighborEdge(terrain::CaveSDFData& sdf, terrain::TerrainTile& neighbor, int axis);
        void restoreCaveState(uint64_t entityId, const std::vector<::events::caveBrush::CaveTileState>& tiles);
        void punchCaveHolesForTile(terrain::TerrainTile& tile);
        void rebuildCaveColliders(EntityHandle entity, terrain::TerrainGrid* grid,
                                  const std::vector<terrain::TileCoord>& caveTiles);
        static CaveTileColliderInfo buildCaveTileColliderInfo(const terrain::TerrainTile& tile,
                                                              std::vector<glm::vec3>& worldPositionsOut);

    };
}
