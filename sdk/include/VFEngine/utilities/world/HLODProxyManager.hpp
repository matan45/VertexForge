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
        };

        static constexpr float CROSSFADE_DURATION = 0.5f;

        void loadProxy(const HLODCellCoord& cellCoord, const std::string& hlodFilePath);
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
