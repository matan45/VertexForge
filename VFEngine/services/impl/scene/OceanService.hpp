#pragma once

#include "../../interfaces/terrain/IOceanService.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/OceanData.hpp"
#include "../../events/terrain/OceanEvents.hpp"
#include "../../events/terrain/TerrainEvents.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../../utilities/world/WorldTypes.hpp"
#include "../../../utilities/terrain/TerrainTypes.hpp"
#include "../../../utilities/water/SeaState.hpp"
#include "../../../utilities/water/ShoreDepthField.hpp"
#include "../../../utilities/water/RippleSimMath.hpp"
#include <glm/glm.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace components
{
    struct OceanComponent;
}

namespace scene
{
    class SceneGraphSystem;
}

namespace water
{
    class WaterTileGrid;
}

namespace services
{
    class IPhysicsProvider;

    class OceanService : public IOceanService
    {
    private:
        std::shared_ptr<scene::SceneGraphSystem> sceneGraph;
        IPhysicsProvider* physicsProvider = nullptr;

        EntityHandle oceanEntity;

        OceanFFTConfigData oceanConfig;
        uint32_t oceanConfigVersion = 0;

        std::function<float(const glm::vec2&)> oceanHeightSampler;

        std::unordered_set<EntityHandle, EntityHandle::Hash> entitiesInWater;

        // Enter/exit transitions recorded during updateBuoyancy (physics worker task) and
        // published from flushWaterEvents on the main thread — script callbacks must not
        // run on the worker.
        struct WaterTransition
        {
            EntityHandle entity;
            glm::vec3 position{0.0f};
            float verticalSpeed = 0.0f;
            float submersion = 0.0f;
            bool entered = false;
        };
        std::vector<WaterTransition> pendingWaterTransitions;

        // Manual sea-state transition (SetOceanSeaStateCommand)
        bool seaStateTransitionActive = false;
        float seaStateTransitionElapsed = 0.0f;
        float seaStateTransitionDuration = 0.0f;
        float seaStateStartBeaufort = 3.0f;
        float seaStateTargetBeaufort = 3.0f;
        water::SeaState seaStateTransitionStart;
        water::SeaState seaStateTransitionTarget;

        // Last weather-driven values applied (quantized) — avoids config-version churn
        float lastAppliedBeaufort = -1.0f;
        float lastAppliedWindDirection = -10000.0f;

        std::unique_ptr<::events::SubscriptionToken> entityDeletedSubscription;
        std::unique_ptr<::events::SubscriptionToken> sceneClearedSubscription;

        // Sector-driven water tile streaming (world mode)
        bool worldModeActive = false;
        world::SectorConfig cachedSectorConfig;
        std::unique_ptr<water::WaterTileGrid> waterTileGrid;
        std::unique_ptr<::events::SubscriptionToken> sectorActivatedSub;
        std::unique_ptr<::events::SubscriptionToken> sectorDeactivatedSub;
        std::unique_ptr<::events::SubscriptionToken> worldLoadedSub;

        struct PendingWaterTileAction
        {
            terrain::TileCoord coord;
            bool isLoad;
        };
        std::mutex pendingActionsMutex;
        std::vector<PendingWaterTileAction> pendingSectorTileActions;

        // VK-1605: shore depth field. The terrain snapshot is a full copy of every loaded tile's
        // height data, so it is taken ONCE per rebake and released the moment the bake completes -
        // it stays resident for the ~8 frames the time-sliced bake takes, not permanently.
        water::ShoreDepthField shoreDepthField;
        ::events::terrain::TerrainHeightfieldResult terrainSnapshot;
        water::TerrainHeightGrid terrainGrid;
        bool shoreFieldRebakeRequested = true;
        bool shoreFieldHadTerrain = false;   // did the last rebake find a heightfield to sample?
        float shoreFieldWaterHeight = 0.0f;  // waterHeight the current field was baked against
        std::unique_ptr<::events::SubscriptionToken> terrainChangedSub;
        std::unique_ptr<::events::SubscriptionToken> terrainLoadedSub;

        // VK-1606: water impulses waiting to reach the ripple sim. THREE threads write here — the
        // AddWaterImpulseCommand handler and the wake emitters on the main thread, and the
        // auto-wakes inside updateBuoyancy on the physics worker — while the render thread drains
        // it. One mutex, drain by swap.
        mutable std::mutex impulseMutex;
        std::vector<water::WaterImpulse> pendingImpulses;

        // Per-entity distance throttles: the last world XZ at which that entity emitted a wake.
        // TWO maps, deliberately, because they have different owning threads — emitterWakeTrail is
        // touched only by updateWakeEmitters (main thread) and buoyancyWakeTrail only by
        // updateBuoyancy (physics worker). Sharing one map would be a plain data race; only the
        // impulse queue they both feed is mutex-guarded.
        std::unordered_map<EntityHandle, glm::vec2, EntityHandle::Hash> emitterWakeTrail;
        std::unordered_map<EntityHandle, glm::vec2, EntityHandle::Hash> buoyancyWakeTrail;

    public:
        explicit OceanService(std::shared_ptr<scene::SceneGraphSystem> sceneGraph);
        ~OceanService() override;

        void setPhysicsProvider(IPhysicsProvider* provider) { physicsProvider = provider; }
        IPhysicsProvider* getPhysicsProvider() const { return physicsProvider; }
        void setOceanHeightSampler(std::function<float(const glm::vec2&)> sampler) { oceanHeightSampler = std::move(sampler); }

        void registerEventHandlers() override;

        EntityHandle createOcean(const OceanCreationData& config) override;
        bool deleteOcean(EntityHandle oceanEntity) override;
        std::optional<OceanData> getOceanData(EntityHandle entity) const override;
        bool hasOceanComponent(EntityHandle entity) const override;

        bool hasActiveOcean() const { return oceanEntity.isValid(); }

        bool isPositionInOcean(const glm::vec3& worldPos) const;
        float getOceanHeightAt(const glm::vec2& worldXZ) const;

        OceanVisualSettings getOceanVisualSettings() const;
        // VK-1604: per-entity overload behind GetOceanVisualSettingsQuery.
        std::optional<OceanVisualSettings> getOceanVisualSettings(EntityHandle entity) const;
        float getBaseWaterHeight() const;

        bool isOceanFFTEnabled() const { return oceanConfig.enabled; }
        OceanFFTConfigData getOceanFFTConfig() const { return oceanConfig; }
        uint32_t getOceanFFTConfigVersion() const { return oceanConfigVersion; }

        bool saveOcean(EntityHandle oceanEntity, const std::string& path);
        EntityHandle loadOcean(const std::string& path);

        void update(float deltaTime);
        void setSeaState(float beaufort, float transitionSeconds);
        float getSeaState() const;

        void updateBuoyancy();
        void flushWaterEvents();
        void clearBuoyancyTracking();
        bool isEntityInWater(EntityHandle entity) const { return entitiesInWater.contains(entity); }

        void rebuildOceanFromComponents();

        // Water tile streaming
        bool isWorldModeActive() const { return worldModeActive; }
        const water::WaterTileGrid* getWaterTileGrid() const;
        void processPendingSectorTileActions();

        // VK-1605: shore depth field. updateShoreDepthField is the per-frame tick (starts a rebake
        // when the camera has drifted far enough, otherwise advances the current one by a fixed
        // number of rows); getWaterDepthAt is the script-facing sampler.
        void updateShoreDepthField(const glm::vec2& cameraXZ);
        const water::ShoreDepthField* getShoreDepthField() const { return &shoreDepthField; }
        float getWaterDepthAt(const glm::vec2& worldXZ) const;
        ShoreDepthFieldStatus getShoreDepthFieldStatus() const;

        // VK-1606: interactive ripples. queueWaterImpulse is callable from any thread;
        // drainWaterImpulses is called once per frame from the render side (through
        // IOceanRenderProvider) and hands the batch to the GPU sim.
        void queueWaterImpulse(const water::WaterImpulse& impulse);
        std::vector<water::WaterImpulse> drainWaterImpulses();
        void setRippleSimEnabled(bool enabled);
        bool isRippleSimEnabled() const;

    private:
        void registerOceanCoreHandlers(::events::EventDispatcher& dispatcher);
        void registerOceanQueryHandlers(::events::EventDispatcher& dispatcher);
        void registerOceanFFTHandlers(::events::EventDispatcher& dispatcher);

        void onEntityDeleted(EntityHandle entity);
        void onSceneCleared();

        // VK-1604: the single component -> OceanVisualSettings copy. Both getOceanVisualSettings
        // overloads go through it, so there is exactly one place to extend when a visual field
        // is added.
        static OceanVisualSettings visualSettingsFromComponent(const components::OceanComponent& comp);

        // Sea state helpers
        void applySeaState(const water::SeaState& state);
        water::SeaState seaStateFromComponentBands() const;
        void updateWeatherDrivenSeaState();
        void updateManualSeaStateTransition(float deltaTime);

        // VK-1605: take a fresh terrain heightfield snapshot and start a bake centred on cameraXZ.
        void beginShoreFieldRebake(const glm::vec2& cameraXZ);

        // VK-1606: WaterWakeEmitterComponent tick (main thread, from update()). Speed comes from the
        // change in world position rather than from a rigid body, so scripted movers, navmesh agents
        // and character controllers all emit wakes without needing physics.
        void updateWakeEmitters(float deltaTime);

        // Sector-driven water tile streaming
        void onSectorActivated(const world::SectorCoord& coord, const world::SectorConfig& config);
        void onSectorDeactivated(const world::SectorCoord& coord, const world::SectorConfig& config);
        void activateWaterTilesForLoadedSectors();
    };
}
