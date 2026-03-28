#include "HLODProxyManager.hpp"
#include "../scene/SceneGraphSystem.hpp"
#include "../scene/Entity.hpp"
#include "../components/CoreComponents.hpp"
#include "../print/Log.hpp"
#include <algorithm>

namespace world
{
    void HLODProxyManager::loadProxy(const HLODCellCoord& cellCoord, const std::string& hlodFilePath)
    {
        if (proxies.count(cellCoord) > 0) return;

        ProxyEntry entry;
        entry.cellCoord = cellCoord;
        entry.state = HLODProxyState::Loading;
        proxies[cellCoord] = std::move(entry);

        // Async load the .vfHLOD file
        PendingLoad pending;
        pending.cellCoord = cellCoord;
        pending.future = std::async(std::launch::async, [hlodFilePath]() -> HLODFileData
        {
            HLODFileData data;
            HLODSerialization::load(hlodFilePath, data);
            return data;
        });
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
                    proxyIt->second.state = HLODProxyState::Loaded;
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
        }
    }

    void HLODProxyManager::beginFadeOut(const HLODCellCoord& cellCoord)
    {
        auto it = proxies.find(cellCoord);
        if (it == proxies.end()) return;
        if (it->second.state != HLODProxyState::Loaded && it->second.state != HLODProxyState::FadingIn)
            return;

        it->second.state = HLODProxyState::FadingOut;
        it->second.crossfadeTimer = CROSSFADE_DURATION;
    }

    void HLODProxyManager::beginFadeIn(const HLODCellCoord& cellCoord)
    {
        auto it = proxies.find(cellCoord);
        if (it == proxies.end()) return;
        if (it->second.state != HLODProxyState::Loaded && it->second.state != HLODProxyState::FadingOut)
            return;

        it->second.state = HLODProxyState::FadingIn;
        it->second.crossfadeTimer = CROSSFADE_DURATION;
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

        // Add mesh component pointing to the .vfHLOD file
        // The rendering system treats this like a regular mesh entity
        // Note: The .vfHLOD path must be accessible via the resource system
        // For now, we store the file data and the rendering pipeline picks it up

        // Add material component with per-submesh materials
        if (!data.submeshes.empty())
        {
            auto& matComp = rootEntity.addComponent<components::MaterialComponent>();
            if (!data.submeshes[0].materialPath.empty())
            {
                matComp.defaultMaterialRef = asset::AssetRef::fromPath(data.submeshes[0].materialPath);
            }

            for (size_t i = 1; i < data.submeshes.size(); ++i)
            {
                if (!data.submeshes[i].materialPath.empty())
                {
                    std::string submeshName = "hlod_submesh_" + std::to_string(i);
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
