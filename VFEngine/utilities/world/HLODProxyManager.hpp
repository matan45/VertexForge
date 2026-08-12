#pragma once

#include "WorldExport.hpp"
#include "HLODTypes.hpp"
#include "HLODSerialization.hpp"
#include "WorldTypes.hpp"
#include "../scene/Entity.hpp"
#include <unordered_map>
#include <future>
#include <vector>

namespace scene { class SceneGraphSystem; }

namespace world
{
#pragma warning(push)
#pragma warning(disable: 4251)
    class VF_WORLD_API HLODProxyManager
    {
    public:
        HLODProxyManager() = default;
        ~HLODProxyManager() = default;
        HLODProxyManager(HLODProxyManager&&) noexcept = default;
        HLODProxyManager& operator=(HLODProxyManager&&) noexcept = default;

        struct ProxyEntry
        {
            HLODCellCoord cellCoord;
            HLODProxyState state = HLODProxyState::Unloaded;
            std::vector<scene::Entity> entities; // one per submesh
            float crossfadeAlpha = 0.0f;         // 0 = fully visible, 1 = fully faded
            float crossfadeTimer = 0.0f;

            // VK-1594: the .vfHLOD path, used both as the MeshComponent's asset ref and as the
            // key the geometry was registered under on the GPU.
            std::string meshKey;
        };

        static constexpr float CROSSFADE_DURATION = 0.5f;

        // VK-1592: the .vfHLOD read happens caller-side, submitted to ResourceLoadScheduler by
        // WorldSectorServiceImpl. It cannot happen here: ResourceLoadScheduler lives in Utilities
        // (a StaticLib), so instance() inside World.dll resolves to a private copy that nothing
        // ever pumps and the profiler never sees. Data arrives already parsed; entity creation
        // still happens in update(), on the caller's thread, through the unchanged pending-load
        // path.
        // VK-1594: meshKey is the .vfHLOD path. The caller has already registered the geometry
        // under it (exe-side, through the ObjectStreaming provider), so the proxy entity can carry
        // a normal MeshComponent resolving to that key and draw through the GPU-driven path.
        void loadProxyFromData(const HLODCellCoord& cellCoord, std::string meshKey, HLODFileData data);
        void unloadProxy(const HLODCellCoord& cellCoord, scene::SceneGraphSystem& sceneGraph);
        void unloadAll(scene::SceneGraphSystem& sceneGraph);

        // Poll async loads, update crossfades, create entities when ready
        void update(scene::SceneGraphSystem& sceneGraph, float deltaTime);

        // Begin crossfade out (proxy fading away because real geometry is loading)
        void beginFadeOut(const HLODCellCoord& cellCoord);

        // Begin crossfade in (proxy appearing because real geometry is unloading)
        void beginFadeIn(const HLODCellCoord& cellCoord);

        bool isProxyLoaded(const HLODCellCoord& cellCoord) const;
        const ProxyEntry* getProxy(const HLODCellCoord& cellCoord) const;

        // VK-1594: cells whose fade-out has run to completion and are now waiting to be destroyed.
        // The caller drives the actual unloadProxy so it can release the cell's GPU registration
        // in the same step; before VK-1594 nothing ever produced the Unloading state because the
        // streamer hard-cut straight to unloadProxy.
        void collectExpiredProxies(std::vector<HLODCellCoord>& out) const;

        // VK-1594: every resident proxy's mesh key, so a bulk unloadAll can also free the GPU
        // registrations. Skips proxies whose bytes never arrived (they have no key yet).
        void collectMeshKeys(std::vector<std::string>& out) const;

    private:
        std::unordered_map<HLODCellCoord, ProxyEntry, HLODCellCoordHash> proxies;

        struct PendingLoad
        {
            HLODCellCoord cellCoord;
            std::future<HLODFileData> future;

            PendingLoad() = default;
            ~PendingLoad() = default;
            PendingLoad(PendingLoad&&) noexcept = default;
            PendingLoad& operator=(PendingLoad&&) noexcept = default;
            PendingLoad(const PendingLoad&) = delete;
            PendingLoad& operator=(const PendingLoad&) = delete;
        };
        std::vector<PendingLoad> pendingLoads;

        void createProxyEntities(ProxyEntry& proxy, const HLODFileData& data,
                                  scene::SceneGraphSystem& sceneGraph);
        void destroyProxyEntities(ProxyEntry& proxy, scene::SceneGraphSystem& sceneGraph);

        HLODProxyManager(const HLODProxyManager&) = delete;
        HLODProxyManager& operator=(const HLODProxyManager&) = delete;
    };
#pragma warning(pop)

} // namespace world
