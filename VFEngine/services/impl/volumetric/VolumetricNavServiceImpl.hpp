#pragma once

#include "../../interfaces/volumetric/IVolumetricNavService.hpp"
#include "../../providers/volumetric/IVolumetricNavProvider.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../data/VolumetricNavData.hpp"
#include "VolumetricAgentManager.hpp"
#include "components/VolumetricComponents.hpp"
#include "navigation/volumetric/VolumetricVoxelizer.hpp"
#include "threading/CancellationToken.hpp"
#include <entt/entt.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace services
{
    // VK-1660: volumetric navigation for flying agents - every event of services/events/volumetric/
    // VolumetricNavEvents.hpp. A bake snapshots the volume's world geometry on the main thread
    // (SceneGeometryCollector::collectVolumetric) and voxelizes it on the JobSystem; update() harvests it into
    // the provider. Grids are keyed by the volume entity's UUID, so a reloaded scene and a Play -> Stop restore
    // find them again; a saved grid lives in the .vfVolNav the component's bakedRef names. The flying agents are
    // the VolumetricAgentManager's.
    class VolumetricNavServiceImpl : public IVolumetricNavService
    {
    public:
        explicit VolumetricNavServiceImpl(IVolumetricNavProvider* provider);
        // Cancels the running bakes and waits for them, then unregisters its handlers.
        ~VolumetricNavServiceImpl() override;

        VolumetricNavServiceImpl(const VolumetricNavServiceImpl&) = delete;
        VolumetricNavServiceImpl& operator=(const VolumetricNavServiceImpl&) = delete;

        void registerEventHandlers() override;
        void update(float deltaTime, bool simulate) override;

        // The handlers' bodies (see VolumetricNavEvents.hpp for the semantics).
        VolumetricBakeTicket requestBake(EntityHandle volume);
        VolumetricBakeOutcome getBakeOutcome(uint64_t ticket) const;
        void cancelBake(EntityHandle volume);
        VolumetricSaveResult saveVolume(EntityHandle volume, const std::string& directory);
        int loadSceneVolumes();
        void clearVolume(EntityHandle volume);
        void unload();
        std::optional<VolumetricVolumeStatus> getVolumeStatus(EntityHandle volume) const;
        std::vector<VolumetricVolumeStatus> listVolumes() const;
        std::optional<VolumetricNavVolumeData> getVolumeData(EntityHandle entity) const;
        bool setVolumeData(EntityHandle entity, const VolumetricNavVolumeData& data);
        std::optional<VolumetricAgentData> getAgentData(EntityHandle entity) const;
        bool setAgentData(EntityHandle entity, const VolumetricAgentData& data);
        bool isPlayMode() const { return playMode.load(std::memory_order_relaxed); }

        VolumetricAgentManager& agents() { return agentManager; }

        // Finished bake outcomes kept for GetVolumetricBakeOutcomeQuery, oldest dropped first.
        static constexpr size_t MAX_BAKE_OUTCOMES = 16;

    private:
        struct RunningBake
        {
            uint64_t ticket = 0;
            uint64_t uuid = 0;
            EntityHandle volume;
            threading::CancellationToken::Ptr cancel;
            std::shared_ptr<std::atomic<float>> progress;
            std::future<volumetric::VolumetricBakeResult> future;
            std::chrono::steady_clock::time_point started;
            bool discard = false; // cleared / unloaded meanwhile: its grid is dropped
        };

        struct LoadedVolume
        {
            std::shared_ptr<const volumetric::VoxelGrid> grid;
            std::string source;    // the .vfVolNav it was loaded from or saved to; "" = a bake not saved yet
            int64_t navigable = 0; // counted once
        };

        void unregisterEventHandlers();
        void harvestBakes();
        void pruneVolumes();
        void drawDebug();
        bool debugVisible();
        void recordOutcome(const VolumetricBakeOutcome& outcome, uint64_t uuid);
        void setLoaded(uint64_t uuid, std::shared_ptr<const volumetric::VoxelGrid> grid, std::string source,
                       int64_t navigable);
        // Cancels the volume's running bakes (every bake for uuid 0) and drops their results.
        void discardBakes(uint64_t uuid);
        VolumetricVolumeStatus makeStatus(entt::entity entity, uint64_t uuid,
                                          const components::VolumetricNavVolumeComponent& component) const;

        IVolumetricNavProvider* provider;
        VolumetricAgentManager agentManager;

        // Bakes, outcomes and loaded volumes: commands arrive on the main thread, update() runs in the Volumetric
        // task. Never held while an event is published.
        mutable std::mutex mutex;
        std::vector<RunningBake> bakes;
        std::deque<VolumetricBakeOutcome> outcomes;
        std::unordered_map<uint64_t, LoadedVolume> loaded;               // by volume UUID
        std::unordered_map<uint64_t, VolumetricBakeState> lastBakeState; // by volume UUID, this session
        uint64_t nextTicket = 1;

        std::atomic<bool> playMode{false};
        bool debugToggleMissing = false;
        bool handlersRegistered = false;
        std::vector<::events::ScopedSubscription> subscriptions;
    };
}
