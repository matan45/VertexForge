#include "HLODProxyManager.hpp"
#include "../scene/SceneGraphSystem.hpp"
#include "../scene/Entity.hpp"
#include "../components/CoreComponents.hpp"
#include "../print/Log.hpp"
#include <algorithm>

namespace world
{
    void HLODProxyManager::loadProxyFromData(const HLODCellCoord& cellCoord, std::string meshKey,
                                             HLODFileData data)
    {
        if (proxies.count(cellCoord) > 0) return;

        ProxyEntry entry;
        entry.cellCoord = cellCoord;
        entry.state = HLODProxyState::Loading;
        entry.meshKey = std::move(meshKey);
        proxies[cellCoord] = std::move(entry);

        // The bytes are already parsed, but they still go through the pending-load queue so
        // update() remains the single place that touches the scene graph: creating entities here
        // would run on whatever thread happened to deliver the data.
        PendingLoad pending;
        pending.cellCoord = cellCoord;
        std::promise<HLODFileData> ready;
        ready.set_value(std::move(data));
        pending.future = ready.get_future();
        pendingLoads.push_back(std::move(pending));
    }

    void HLODProxyManager::unloadProxy(const HLODCellCoord& cellCoord, scene::SceneGraphSystem& sceneGraph)
    {
        auto it = proxies.find(cellCoord);
        if (it == proxies.end()) return;

        destroyProxyEntities(it->second, sceneGraph);
        proxies.erase(it);
    }

    void HLODProxyManager::unloadAll(scene::SceneGraphSystem& sceneGraph)
    {
        for (auto& [coord, proxy] : proxies)
        {
            destroyProxyEntities(proxy, sceneGraph);
        }
        proxies.clear();

        // Wait for any pending loads
        for (auto& pending : pendingLoads)
        {
            if (pending.future.valid())
                pending.future.wait();
        }
        pendingLoads.clear();
    }

    void HLODProxyManager::update(scene::SceneGraphSystem& sceneGraph, float deltaTime)
    {
        // Poll async loads
        for (auto it = pendingLoads.begin(); it != pendingLoads.end();)
        {
            if (it->future.valid() &&
                it->future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                auto data = it->future.get();
                auto proxyIt = proxies.find(it->cellCoord);
                if (proxyIt != proxies.end() && data.header.submeshCount > 0)
                {
                    createProxyEntities(proxyIt->second, data, sceneGraph);

                    // VK-1594: a proxy appears by fading in rather than popping. Alpha starts at
                    // 1 (fully faded) so the first rendered frame is already correct - leaving it
                    // at the default 0 would flash the proxy at full opacity for one frame.
                    proxyIt->second.state = HLODProxyState::FadingIn;
                    proxyIt->second.crossfadeAlpha = 1.0f;
                    proxyIt->second.crossfadeTimer = CROSSFADE_DURATION;

                    vfLogInfo("HLOD proxy loaded: [{},{},T{}]",
                              it->cellCoord.x, it->cellCoord.z, it->cellCoord.tier);
                }
                else if (proxyIt != proxies.end())
                {
                    // Empty or failed load -- remove entry
                    proxies.erase(proxyIt);
                }

                it = pendingLoads.erase(it);
            }
            else
            {
                ++it;
            }
        }

        // Update crossfades
        for (auto& [coord, proxy] : proxies)
        {
            const float previousAlpha = proxy.crossfadeAlpha;

            if (proxy.state == HLODProxyState::FadingIn)
            {
                proxy.crossfadeTimer -= deltaTime;
                if (proxy.crossfadeTimer <= 0.0f)
                {
                    proxy.crossfadeTimer = 0.0f;
                    proxy.crossfadeAlpha = 0.0f;
                    proxy.state = HLODProxyState::Loaded;
                }
                else
                {
                    proxy.crossfadeAlpha = proxy.crossfadeTimer / CROSSFADE_DURATION;
                }
            }
            else if (proxy.state == HLODProxyState::FadingOut)
            {
                proxy.crossfadeTimer -= deltaTime;
                if (proxy.crossfadeTimer <= 0.0f)
                {
                    proxy.crossfadeTimer = 0.0f;
                    proxy.crossfadeAlpha = 1.0f;
                    proxy.state = HLODProxyState::Unloading;
                }
                else
                {
                    proxy.crossfadeAlpha = 1.0f - (proxy.crossfadeTimer / CROSSFADE_DURATION);
                }
            }

            // VK-1594: publish the alpha onto the entity so FramePreparationSystem can forward it
            // to the GPU dither. Written only on change - a proxy sitting at Loaded (alpha 0) for
            // minutes must not dirty its component every frame.
            if (proxy.crossfadeAlpha != previousAlpha)
            {
                for (auto& entity : proxy.entities)
                {
                    // isAlive() for the same reason destroyProxyEntities checks it - the entity
                    // can be destroyed out from under the proxy (scene reload, undo), and
                    // hasComponent on a stale handle is undefined.
                    if (entity.isAlive() && entity.hasComponent<components::HLODProxyComponent>())
                        entity.getComponent<components::HLODProxyComponent>().crossfadeAlpha =
                            proxy.crossfadeAlpha;
                }
            }
        }
    }

    void HLODProxyManager::beginFadeOut(const HLODCellCoord& cellCoord)
    {
        auto it = proxies.find(cellCoord);
        if (it == proxies.end()) return;
        if (it->second.state != HLODProxyState::Loaded && it->second.state != HLODProxyState::FadingIn)
            return;

        // Seed the timer from the CURRENT alpha rather than restarting at the full duration, so
        // reversing a fade mid-flight continues from where it is instead of popping. FadingOut
        // reads alpha = 1 - timer/DURATION, hence timer = (1 - alpha) * DURATION.
        it->second.state = HLODProxyState::FadingOut;
        it->second.crossfadeTimer = (1.0f - it->second.crossfadeAlpha) * CROSSFADE_DURATION;
    }

    void HLODProxyManager::beginFadeIn(const HLODCellCoord& cellCoord)
    {
        auto it = proxies.find(cellCoord);
        if (it == proxies.end()) return;
        if (it->second.state != HLODProxyState::Loaded && it->second.state != HLODProxyState::FadingOut)
            return;

        // FadingIn reads alpha = timer/DURATION, hence timer = alpha * DURATION.
        it->second.state = HLODProxyState::FadingIn;
        it->second.crossfadeTimer = it->second.crossfadeAlpha * CROSSFADE_DURATION;
    }

    void HLODProxyManager::collectExpiredProxies(std::vector<HLODCellCoord>& out) const
    {
        for (const auto& [coord, proxy] : proxies)
        {
            if (proxy.state == HLODProxyState::Unloading)
                out.push_back(coord);
        }
    }

    void HLODProxyManager::collectMeshKeys(std::vector<std::string>& out) const
    {
        for (const auto& [coord, proxy] : proxies)
        {
            if (!proxy.meshKey.empty())
                out.push_back(proxy.meshKey);
        }
    }

    bool HLODProxyManager::isProxyLoaded(const HLODCellCoord& cellCoord) const
    {
        auto it = proxies.find(cellCoord);
        return it != proxies.end() && it->second.state != HLODProxyState::Unloaded;
    }

    const HLODProxyManager::ProxyEntry* HLODProxyManager::getProxy(const HLODCellCoord& cellCoord) const
    {
        auto it = proxies.find(cellCoord);
        return (it != proxies.end()) ? &it->second : nullptr;
    }

    void HLODProxyManager::createProxyEntities(ProxyEntry& proxy, const HLODFileData& data,
                                                 scene::SceneGraphSystem& sceneGraph)
    {
        // Create a root entity for the HLOD proxy
        std::string proxyName = "HLOD_" + std::to_string(proxy.cellCoord.x) + "_" +
                                std::to_string(proxy.cellCoord.z) + "_T" +
                                std::to_string(proxy.cellCoord.tier);

        scene::Entity rootEntity(proxyName);

        // Set transform at origin (vertices are already world-space)
        auto& transform = rootEntity.getComponent<components::TransformComponent>();
        transform.position = glm::vec3(0.0f);
        transform.scale = glm::vec3(1.0f);
        transform.isStatic = true;

        // Add HLOD tag component
        rootEntity.addComponent<components::HLODProxyComponent>(
            proxy.cellCoord.x, proxy.cellCoord.z, proxy.cellCoord.tier);

        // VK-1594: the geometry was uploaded into MergedMeshBuffer from memory under this key
        // before we got here, so a plain MeshComponent is all the GPU-driven renderer needs -
        // it enumerates registry.view<MeshComponent>() and resolves meshRef to that same key.
        // Without this the proxy entity was inert and nothing HLOD ever drew.
        rootEntity.addComponent<components::MeshComponent>().meshRef =
            asset::AssetRef::fromPath(proxy.meshKey);

        // Add material component with per-submesh materials
        if (!data.submeshes.empty())
        {
            auto& matComp = rootEntity.addComponent<components::MaterialComponent>();
            if (!data.submeshes[0].materialPath.empty())
            {
                matComp.defaultMaterialRef = asset::AssetRef::fromPath(data.submeshes[0].materialPath);
            }

            // Names MUST match the ones the upload reserved its submesh slots under
            // (ObjectStreamingAdapter::registerHLODMesh), or the per-submesh material lookup
            // silently misses and every submesh falls back to the default material.
            for (size_t i = 0; i < data.submeshes.size(); ++i)
            {
                if (!data.submeshes[i].materialPath.empty())
                {
                    std::string submeshName = "hlod_" + std::to_string(i);
                    matComp.setSubMeshMaterial(submeshName,
                        asset::AssetRef::fromPath(data.submeshes[i].materialPath));
                }
            }
        }

        auto& root = sceneGraph.GetRoot();
        sceneGraph.addChild(root, rootEntity);
        proxy.entities.push_back(rootEntity);
    }

    void HLODProxyManager::destroyProxyEntities(ProxyEntry& proxy, scene::SceneGraphSystem& sceneGraph)
    {
        for (auto& entity : proxy.entities)
        {
            if (entity.isAlive())
            {
                sceneGraph.removeEntity(entity);
            }
        }
        proxy.entities.clear();
    }

} // namespace world
