#include "ShadowSystem.hpp"
#include "../../core/RenderManager.hpp"
#include "../../core/ThreadCommandPoolManager.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "print/Log.hpp"

namespace render::shadow
{
    void ShadowSystem::determineDynamicPages()
    {
        // Check if any non-static (dynamic) mesh entities exist in the scene
        auto& registry = scene::EntityRegistry::getRegistry();
        bool hasDynamicObjects = false;

        auto view = registry.view<components::TransformComponent, components::MeshComponent>();
        for (auto entity : view)
        {
            const auto& transform = view.get<components::TransformComponent>(entity);
            if (!transform.isStatic)
            {
                hasDynamicObjects = true;
                break;
            }
        }

        // For each dynamic light, mark all pages as having/not-having dynamic objects
        // Phase 1 (simple): if any dynamic objects exist, conservatively mark all pages
        // Phase 2 (future): per-page frustum-AABB testing for precise detection
        for (auto& [entityId, data] : lightShadowData)
        {
            if (data.isStatic || !data.usesVSM())
                continue;

            uint32_t totalPages = data.vsmPagesX * data.vsmPagesY;
            if (data.vsmPageHasDynamic.size() != totalPages)
                data.vsmPageHasDynamic.resize(totalPages, false);

            for (uint32_t i = 0; i < totalPages; ++i)
            {
                data.vsmPageHasDynamic[i] = hasDynamicObjects;
            }
        }
    }

    void ShadowSystem::buildPageRenderList()
    {
        pageRenderList.clear();
        staticPageRenderList.clear();
        dynamicPageRenderList.clear();
        tileCopyList.clear();
        lastCacheStats.totalPages = 0;
        lastCacheStats.renderedPages = 0;
        lastCacheStats.cachedPages = 0;
        lastCacheStats.staticPagesRendered = 0;
        lastCacheStats.dynamicPagesRendered = 0;
        lastCacheStats.tileCopiesThisFrame = 0;
        lastCacheStats.dynamicTilesAllocated = 0;

        for (auto& [entityId, data] : lightShadowData)
        {
            if (!data.settings.enabled || !data.settings.castShadows)
                continue;

            if (data.type == ShadowMapType::PointCube)
                continue;

            if (data.vsmPhysicalTiles.empty())
                continue;

            uint32_t totalPages = data.vsmPagesX * data.vsmPagesY;
            if (data.vsmPageDirty.size() != totalPages)
                data.vsmPageDirty.resize(totalPages, true);

            // Ensure dual-layer vectors are sized for dynamic lights
            if (!data.isStatic)
            {
                if (data.vsmDynamicTiles.size() != totalPages)
                    data.vsmDynamicTiles.resize(totalPages, vsm::INVALID_TILE);
                if (data.vsmPageHasDynamic.size() != totalPages)
                    data.vsmPageHasDynamic.resize(totalPages, false);
                if (data.vsmDynamicTileLastUsedFrame.size() != totalPages)
                    data.vsmDynamicTileLastUsedFrame.resize(totalPages, 0);
            }

            if (data.type == ShadowMapType::DirectionalCSM)
                buildCSMPageRenderList(data);
            else
                buildSingleViewPageRenderList(data);
        }

        lastCacheStats.tileCopiesThisFrame = static_cast<uint32_t>(tileCopyList.size());

        // Count total dynamic tiles allocated across all lights
        for (const auto& [entityId, data] : lightShadowData)
        {
            for (uint32_t tile : data.vsmDynamicTiles)
            {
                if (tile != vsm::INVALID_TILE)
                    ++lastCacheStats.dynamicTilesAllocated;
            }
        }
    }

    void ShadowSystem::addPageToRenderLists(LightShadowData& data, uint32_t pageIdx,
                                              const glm::mat4& cropViewProjection,
                                              const ShadowView& view, bool isDirty)
    {
        uint32_t physTile = data.vsmPhysicalTiles[pageIdx];
        if (physTile == vsm::INVALID_TILE)
            return;

        ++lastCacheStats.totalPages;

        // renderedFrameCount < 3: keep re-rendering to ensure GPU draw data is ready after scene load
        bool forceRender = data.renderedFrameCount < 3;

        if (data.isStatic)
        {
            // Static light: single-layer path (all objects, legacy behavior)
            if (!isDirty && !data.vsmPageDirty[pageIdx] && !forceRender)
            {
                ++lastCacheStats.cachedPages;
                return;
            }

            PageRenderEntry entry;
            entry.physicalTileIndex = physTile;
            entry.cropViewProjection = cropViewProjection;
            entry.depthBias = view.depthBias;
            entry.slopeBias = view.slopeBias;
            entry.normalBias = view.normalBias;
            entry.layer = ShadowLayer::All;
            pageRenderList.push_back(entry);
            ++lastCacheStats.renderedPages;
            if (!forceRender)
                data.vsmPageDirty[pageIdx] = false;
        }
        else
        {
            // Dynamic light: dual-layer path

            // Static layer: render ALL objects (cached until dirty)
            // renderedFrameCount < 3: keep re-rendering for first few frames to ensure GPU draw data is ready
            bool forceRender = data.renderedFrameCount < 3;
            if (isDirty || data.vsmPageDirty[pageIdx] || forceRender)
            {
                PageRenderEntry staticEntry;
                staticEntry.physicalTileIndex = physTile;
                staticEntry.cropViewProjection = cropViewProjection;
                staticEntry.depthBias = view.depthBias;
                staticEntry.slopeBias = view.slopeBias;
                staticEntry.normalBias = view.normalBias;
                staticEntry.layer = ShadowLayer::All; // render all objects into static tile
                staticPageRenderList.push_back(staticEntry);
                ++lastCacheStats.renderedPages;
                ++lastCacheStats.staticPagesRendered;
                if (!forceRender)
                    data.vsmPageDirty[pageIdx] = false;
            }
            else
            {
                ++lastCacheStats.cachedPages;
            }

            // Dynamic layer: if page has dynamic objects, allocate tile + copy + render
            if (data.vsmPageHasDynamic[pageIdx])
            {
                // Allocate dynamic tile if not yet allocated
                if (data.vsmDynamicTiles[pageIdx] == vsm::INVALID_TILE)
                {
                    uint32_t dynTile = tilePool->allocateTile();
                    if (dynTile != vsm::INVALID_TILE)
                    {
                        data.vsmDynamicTiles[pageIdx] = dynTile;
                    }
                }

                uint32_t dynTile = data.vsmDynamicTiles[pageIdx];
                if (dynTile != vsm::INVALID_TILE)
                {
                    // Copy static tile -> dynamic tile
                    tileCopyList.push_back({physTile, dynTile});

                    // Render dynamic objects on top
                    PageRenderEntry dynEntry;
                    dynEntry.physicalTileIndex = dynTile;
                    dynEntry.cropViewProjection = cropViewProjection;
                    dynEntry.depthBias = view.depthBias;
                    dynEntry.slopeBias = view.slopeBias;
                    dynEntry.normalBias = view.normalBias;
                    dynEntry.layer = ShadowLayer::Dynamic;
                    dynamicPageRenderList.push_back(dynEntry);
                    ++lastCacheStats.dynamicPagesRendered;

                    // Page table points to composited (dynamic) tile
                    uint32_t px = pageIdx % data.vsmPagesX;
                    uint32_t py = pageIdx / data.vsmPagesX;
                    pageTable->mapPage(data.vsmPageTableOffset, px, py, data.vsmPagesX, dynTile);

                    data.vsmDynamicTileLastUsedFrame[pageIdx] = frameCounter;
                }
            }
            else
            {
                // No dynamic objects: page table points to static tile
                // Free dynamic tile if it was allocated and cooldown expired
                constexpr uint32_t DYNAMIC_TILE_COOLDOWN = 30;
                if (data.vsmDynamicTiles[pageIdx] != vsm::INVALID_TILE)
                {
                    if (frameCounter - data.vsmDynamicTileLastUsedFrame[pageIdx] > DYNAMIC_TILE_COOLDOWN)
                    {
                        tilePool->freeTile(data.vsmDynamicTiles[pageIdx]);
                        data.vsmDynamicTiles[pageIdx] = vsm::INVALID_TILE;

                        // Remap page table back to static tile
                        uint32_t px = pageIdx % data.vsmPagesX;
                        uint32_t py = pageIdx / data.vsmPagesX;
                        pageTable->mapPage(data.vsmPageTableOffset, px, py, data.vsmPagesX, physTile);
                    }
                }
            }
        }
    }

    void ShadowSystem::buildCSMPageRenderList(LightShadowData& data)
    {
        uint32_t pagesPerCascade = data.vsmPagesX;
        uint32_t cascadeCount = data.settings.cascadeCount;
        bool csmDirty = cameraMovedThisFrame;

        for (uint32_t cascade = 0; cascade < cascadeCount && cascade < data.views.size(); ++cascade)
        {
            const auto& view = data.views[cascade];
            if (view.cached)
                continue;

            for (uint32_t py = 0; py < pagesPerCascade; ++py)
            {
                for (uint32_t px = 0; px < pagesPerCascade; ++px)
                {
                    uint32_t pageIdx = (cascade * pagesPerCascade + py) * pagesPerCascade + px;
                    if (pageIdx >= data.vsmPhysicalTiles.size())
                        continue;

                    glm::mat4 cropMatrix = vsm::computePageCropMatrix(px, py, pagesPerCascade, pagesPerCascade);
                    addPageToRenderLists(data, pageIdx, cropMatrix * view.viewProjectionMatrix, view, csmDirty);
                }
            }
        }
    }

    void ShadowSystem::buildSingleViewPageRenderList(LightShadowData& data)
    {
        if (data.views.empty())
            return;

        const auto& view = data.views[0];
        if (view.cached)
            return;

        // Detect light movement: if VP matrix changed, mark all pages dirty
        bool lightMoved = (view.viewProjectionMatrix != data.lastViewProjection);
        if (lightMoved)
        {
            data.lastViewProjection = view.viewProjectionMatrix;
            for (size_t i = 0; i < data.vsmPageDirty.size(); ++i)
                data.vsmPageDirty[i] = true;
        }

        for (uint32_t py = 0; py < data.vsmPagesY; ++py)
        {
            for (uint32_t px = 0; px < data.vsmPagesX; ++px)
            {
                uint32_t pageIdx = py * data.vsmPagesX + px;
                if (pageIdx >= data.vsmPhysicalTiles.size())
                    continue;

                glm::mat4 cropMatrix = vsm::computePageCropMatrix(px, py, data.vsmPagesX, data.vsmPagesY);
                addPageToRenderLists(data, pageIdx, cropMatrix * view.viewProjectionMatrix, view, false);
            }
        }
    }

    void ShadowSystem::applyRenderSettings(const types::RenderSettings& settings)
    {
        if (!initialized)
        {
            vfLogWarning("ShadowSystem::applyRenderSettings() called when not initialized");
            return;
        }

        const auto& shadowSettings = settings.shadows;

        shadowsEnabled = shadowSettings.enabled;
        globalDepthBias = shadowSettings.shadowBias;
        globalSlopeBias = shadowSettings.slopeBias;
        globalNormalBias = shadowSettings.normalBias;
        globalCascadeCount = shadowSettings.cascadeCount;
        globalCascadeSplitMode = shadowSettings.cascadeSplitMode;

        if (!shadowSettings.enabled || shadowSettings.quality == types::ShadowQuality::Off)
            return;

        globalQuality = static_cast<ShadowQuality>(shadowSettings.quality);

        for (auto& [entityId, data] : lightShadowData)
        {
            data.settings.depthBias = shadowSettings.shadowBias;
            data.settings.slopeBias = shadowSettings.slopeBias;
            data.settings.normalBias = shadowSettings.normalBias;

            if (data.type == ShadowMapType::DirectionalCSM &&
                data.settings.cascadeCount != shadowSettings.cascadeCount)
            {
                if (data.usesVSM())
                    freeVSMPages(data);

                data.settings.cascadeCount = shadowSettings.cascadeCount;
                data.views.resize(shadowSettings.cascadeCount);

                if (data.usesVSM())
                    allocateVSMPages(data);
            }

            data.settingsDirty = true;
        }

        poolFirstUse = true;
        needsUpdate = true;
        globalSoftShadows = shadowSettings.softShadows;
    }

    void ShadowSystem::notifySceneChanged()
    {
        for (auto& [entityId, data] : lightShadowData)
        {
            for (size_t i = 0; i < data.vsmPageDirty.size(); ++i)
                data.vsmPageDirty[i] = true;

            data.shadowCached = false;
            data.renderedFrameCount = 0;
            for (auto& view : data.views)
                view.cached = false;
        }
        needsUpdate = true;
    }

    void ShadowSystem::dispatchFeedback(vk::CommandBuffer cmd, vk::ImageView depthView,
                                         const glm::mat4& invViewProjection,
                                         uint32_t screenWidth, uint32_t screenHeight)
    {
        if (!feedbackEnabled || !feedbackPipeline || !feedbackPipeline->isInitialized() || !gpuDataManager)
            return;

        feedbackPipeline->clearFeedbackBuffer(cmd);

        uint32_t vsmLightCount = static_cast<uint32_t>(
            directionalShadowViews.size() + spotShadowViews.size()
        );

        uint32_t totalViews = static_cast<uint32_t>(
            directionalShadowViews.size() + pointShadowViews.size() + spotShadowViews.size()
        );

        if (totalViews == 0)
            return;

        vk::DeviceSize shadowDataSize = sizeof(vsm::GPUVSMLight) * totalViews;

        feedbackPipeline->dispatch(cmd, depthView,
            gpuDataManager->getShadowDataBuffer(), shadowDataSize,
            totalViews, invViewProjection, screenWidth, screenHeight);
    }

    void ShadowSystem::copyFeedbackToStaging(vk::CommandBuffer cmd)
    {
        if (!feedbackPipeline || !feedbackPipeline->isInitialized())
            return;
        feedbackPipeline->copyResultsToStaging(cmd);
    }

    void ShadowSystem::markFeedbackReady()
    {
        if (feedbackPipeline)
            feedbackPipeline->markResultsReady();
    }

    void ShadowSystem::readBackFeedback()
    {
        if (!feedbackPipeline || !feedbackEnabled)
            return;

        if (feedbackPipeline->getReadbackState() != FeedbackReadbackState::Ready)
            return;

        uint32_t usedEntries = 0;
        for (const auto& [entityId, data] : lightShadowData)
        {
            if (data.usesVSM())
            {
                uint32_t end = data.vsmPageTableOffset + data.vsmPagesX * data.vsmPagesY;
                if (end > usedEntries)
                    usedEntries = end;
            }
        }

        if (usedEntries == 0)
            return;

        prevFrameFeedback = feedbackPipeline->readbackResults(usedEntries);
        feedbackHasResults = !prevFrameFeedback.empty();
    }

    void ShadowSystem::applyFeedbackAllocations()
    {
        if (!tilePool || !pageTable)
            return;

        // Non-static lights: brute-force allocate all pages (no feedback needed)
        for (auto& [entityId, data] : lightShadowData)
        {
            if (!data.usesVSM() || data.isStatic)
                continue;

            uint32_t totalPages = data.vsmPagesX * data.vsmPagesY;
            if (totalPages == 0)
                continue;

            if (data.vsmPhysicalTiles.size() != totalPages)
                data.vsmPhysicalTiles.resize(totalPages, vsm::INVALID_TILE);
            if (data.vsmPageLastUsedFrame.size() != totalPages)
                data.vsmPageLastUsedFrame.resize(totalPages, 0);
            if (data.vsmPageDirty.size() != totalPages)
                data.vsmPageDirty.resize(totalPages, true);

            for (uint32_t i = 0; i < totalPages; ++i)
            {
                if (data.vsmPhysicalTiles[i] == vsm::INVALID_TILE)
                {
                    uint32_t tile = tilePool->allocateTile();
                    if (tile != vsm::INVALID_TILE)
                    {
                        data.vsmPhysicalTiles[i] = tile;
                        uint32_t px = i % data.vsmPagesX;
                        uint32_t py = i / data.vsmPagesX;
                        pageTable->mapPage(data.vsmPageTableOffset, px, py, data.vsmPagesX, tile);
                        data.vsmPageDirty[i] = true;
                    }
                }
            }
        }

        // Static lights: feedback-driven allocation (requires results)
        if (!feedbackEnabled || !feedbackHasResults || prevFrameFeedback.empty())
            return;

        static constexpr uint32_t WARMUP_FRAMES = 120;
        bool allowEviction = frameCounter > WARMUP_FRAMES;

        for (auto& [entityId, data] : lightShadowData)
        {
            if (!data.usesVSM() || !data.isStatic)
                continue;

            uint32_t totalPages = data.vsmPagesX * data.vsmPagesY;
            if (totalPages == 0)
                continue;

            if (data.vsmPhysicalTiles.size() != totalPages)
                data.vsmPhysicalTiles.resize(totalPages, vsm::INVALID_TILE);
            if (data.vsmPageLastUsedFrame.size() != totalPages)
                data.vsmPageLastUsedFrame.resize(totalPages, 0);

            for (uint32_t i = 0; i < totalPages; ++i)
            {
                uint32_t feedbackIdx = data.vsmPageTableOffset + i;
                if (feedbackIdx >= prevFrameFeedback.size())
                    continue;

                bool pageNeeded = prevFrameFeedback[feedbackIdx] > 0;

                if (pageNeeded)
                {
                    data.vsmPageLastUsedFrame[i] = frameCounter;

                    if (data.vsmPhysicalTiles[i] == vsm::INVALID_TILE)
                    {
                        uint32_t tile = tilePool->allocateTile();
                        if (tile != vsm::INVALID_TILE)
                        {
                            data.vsmPhysicalTiles[i] = tile;
                            uint32_t px = i % data.vsmPagesX;
                            uint32_t py = i / data.vsmPagesX;
                            pageTable->mapPage(data.vsmPageTableOffset, px, py, data.vsmPagesX, tile);
                        }
                    }
                }
                else
                {
                    if (allowEviction &&
                        data.vsmPhysicalTiles[i] != vsm::INVALID_TILE &&
                        frameCounter - data.vsmPageLastUsedFrame[i] > EVICTION_THRESHOLD)
                    {
                        tilePool->freeTile(data.vsmPhysicalTiles[i]);
                        uint32_t px = i % data.vsmPagesX;
                        uint32_t py = i / data.vsmPagesX;
                        pageTable->unmapPage(data.vsmPageTableOffset, px, py, data.vsmPagesX);
                        data.vsmPhysicalTiles[i] = vsm::INVALID_TILE;
                    }
                }
            }
        }
    }

    void ShadowSystem::uploadToGPU(vk::CommandBuffer cmd)
    {
        if (!initialized || !shadowsEnabled || !gpuDataManager)
            return;

        std::unordered_map<uint32_t, uint32_t> entityToCubeIndex;
        uint32_t cubeIdx = 0;
        for (const auto& [entityId, data] : lightShadowData)
        {
            if (data.type == ShadowMapType::PointCube &&
                data.settings.enabled && data.settings.castShadows &&
                data.resourceHandle.isValid())
            {
                ShadowCubeMap* cube = resourcePool ? resourcePool->getCube(data.resourceHandle) : nullptr;
                if (cube && cube->isInitialized())
                    entityToCubeIndex[entityId] = cubeIdx++;
            }
        }

        gpuDataManager->buildGPUShadowData(
            directionalShadowViews, pointShadowViews, spotShadowViews,
            lightShadowData, entityToCubeIndex);

        gpuDataManager->uploadToGPU(cmd);

        if (pageTable)
            pageTable->uploadToGPU(cmd);

        gpuDataManager->updateShadowTextureDescriptor(tilePool.get(), resourcePool.get(), lightShadowData);

        needsUpdate = false;
    }

    void ShadowSystem::recordShadowPass(vk::CommandBuffer cmd,
                                         const ShadowPassParams& params,
                                         const TerrainShadowPassParams* terrainParams)
    {
        if (!passRecorder)
            return;

        constexpr uint32_t PARALLEL_TILE_THRESHOLD = 5;
        uint32_t frameIndex = core::RenderManager::getImageIndex();

        uint32_t totalPages = static_cast<uint32_t>(pageRenderList.size() + staticPageRenderList.size() + dynamicPageRenderList.size());
        if (threadPoolManager && threadPoolManager->getThreadCount() > 1 &&
            totalPages >= PARALLEL_TILE_THRESHOLD)
        {
            threadPoolManager->resetFrame(frameIndex);
            passRecorder->recordShadowPassParallel(cmd, params, terrainParams,
                tilePool.get(), resourcePool.get(),
                shadowPassPipeline.get(), terrainShadowPipeline.get(),
                pageRenderList, staticPageRenderList, dynamicPageRenderList, tileCopyList,
                lightShadowData, shadowsEnabled, poolFirstUse,
                threadPoolManager.get(), frameIndex);
        }
        else
        {
            passRecorder->recordShadowPass(cmd, params, terrainParams,
                tilePool.get(), resourcePool.get(),
                shadowPassPipeline.get(), terrainShadowPipeline.get(),
                pageRenderList, staticPageRenderList, dynamicPageRenderList, tileCopyList,
                lightShadowData, shadowsEnabled, poolFirstUse);
        }

        if (shadowsEnabled)
            poolFirstUse = false;
    }
}
