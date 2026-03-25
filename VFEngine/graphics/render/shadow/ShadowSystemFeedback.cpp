#include "ShadowSystem.hpp"
#include "../../core/RenderManager.hpp"
#include "../../core/ThreadCommandPoolManager.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "print/Log.hpp"

namespace render::shadow
{
    // Phase 1 (conservative): marks ALL pages as having dynamic content if any dynamic entity exists.
    // This over-allocates dynamic tiles but is correct. Phase 2 optimization: per-page frustum-AABB
    // testing to only mark pages where dynamic objects actually overlap the page's shadow frustum.
    void ShadowSystem::determineDynamicPages()
    {
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

        for (auto& [entityId, data] : lightShadowData)
        {
            if (data.isStatic || !data.usesVSM()) continue;
            uint32_t totalPages = data.vsmPagesX * data.vsmPagesY;
            if (data.vsmPageHasDynamic.size() != totalPages)
                data.vsmPageHasDynamic.resize(totalPages, false);
            for (uint32_t i = 0; i < totalPages; ++i)
                data.vsmPageHasDynamic[i] = hasDynamicObjects;
        }
    }

    void ShadowSystem::buildPageRenderList()
    {
        pageRenderList.clear();
        staticPageRenderList.clear();
        dynamicPageRenderList.clear();
        tileCopyList.clear();
        lastCacheStats = {};
        for (auto& [entityId, data] : lightShadowData)
        {
            if (!data.settings.enabled || !data.settings.castShadows)
                continue;
            if (data.vsmPhysicalTiles.empty())
                continue;

            uint32_t totalPages = data.vsmPagesX * data.vsmPagesY;
            if (data.vsmPageDirty.size() != totalPages)
                data.vsmPageDirty.resize(totalPages, true);

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
            else if (data.type == ShadowMapType::DirectionalClipmap)
                buildClipmapPageRenderList(data);
            else if (data.type == ShadowMapType::PointCube)
                buildPointPageRenderList(data);
            else
                buildSingleViewPageRenderList(data);
        }

        lastCacheStats.tileCopiesThisFrame = static_cast<uint32_t>(tileCopyList.size());
        for (const auto& [entityId, data] : lightShadowData)
            for (uint32_t tile : data.vsmDynamicTiles)
                if (tile != vsm::INVALID_TILE)
                    ++lastCacheStats.dynamicTilesAllocated;
    }

    void ShadowSystem::addStaticLightPage(LightShadowData& data, uint32_t pageIdx,
                                            const glm::mat4& cropVP, const ShadowView& view,
                                            bool isDirty, bool forceRender)
    {
        uint32_t physTile = data.vsmPhysicalTiles[pageIdx];
        if (!isDirty && !data.vsmPageDirty[pageIdx] && !forceRender)
        {
            ++lastCacheStats.cachedPages;
            return;
        }

        PageRenderEntry entry;
        entry.physicalTileIndex = physTile;
        entry.cropViewProjection = cropVP;
        entry.depthBias = view.depthBias;
        entry.slopeBias = view.slopeBias;
        entry.normalBias = view.normalBias;
        entry.layer = ShadowLayer::All;
        pageRenderList.push_back(entry);
        ++lastCacheStats.renderedPages;
        if (!forceRender)
            data.vsmPageDirty[pageIdx] = false;
    }

    void ShadowSystem::allocateDynamicTile(LightShadowData& data, uint32_t pageIdx)
    {
        if (data.vsmDynamicTiles[pageIdx] == vsm::INVALID_TILE)
        {
            uint32_t dynTile = tilePool->allocateTile();
            if (dynTile != vsm::INVALID_TILE)
                data.vsmDynamicTiles[pageIdx] = dynTile;
        }
    }

    void ShadowSystem::freeDynamicTileIfExpired(LightShadowData& data, uint32_t pageIdx, uint32_t physTile)
    {
        constexpr uint32_t DYNAMIC_TILE_COOLDOWN = 30;
        if (data.vsmDynamicTiles[pageIdx] == vsm::INVALID_TILE)
            return;

        if (frameCounter - data.vsmDynamicTileLastUsedFrame[pageIdx] > DYNAMIC_TILE_COOLDOWN)
        {
            tilePool->freeTile(data.vsmDynamicTiles[pageIdx]);
            data.vsmDynamicTiles[pageIdx] = vsm::INVALID_TILE;
            uint32_t px = pageIdx % data.vsmPagesX;
            uint32_t py = pageIdx / data.vsmPagesX;
            pageTable->mapPage(data.vsmPageTableOffset, px, py, data.vsmPagesX, physTile);
        }
    }

    void ShadowSystem::addDualLayerPage(LightShadowData& data, uint32_t pageIdx,
                                          const glm::mat4& cropVP, const ShadowView& view,
                                          bool isDirty, bool forceRender)
    {
        uint32_t physTile = data.vsmPhysicalTiles[pageIdx];
        if (isDirty || data.vsmPageDirty[pageIdx] || forceRender)
        {
            PageRenderEntry staticEntry;
            staticEntry.physicalTileIndex = physTile;
            staticEntry.cropViewProjection = cropVP;
            staticEntry.depthBias = view.depthBias;
            staticEntry.slopeBias = view.slopeBias;
            staticEntry.normalBias = view.normalBias;
            staticEntry.layer = ShadowLayer::Static;
            staticPageRenderList.push_back(staticEntry);
            ++lastCacheStats.renderedPages;
            ++lastCacheStats.staticPagesRendered;
            if (!forceRender) data.vsmPageDirty[pageIdx] = false;
        }
        else { ++lastCacheStats.cachedPages; }

        if (!data.vsmPageHasDynamic[pageIdx])
        {
            freeDynamicTileIfExpired(data, pageIdx, physTile);
            return;
        }

        allocateDynamicTile(data, pageIdx);
        uint32_t dynTile = data.vsmDynamicTiles[pageIdx];
        if (dynTile == vsm::INVALID_TILE) return;

        tileCopyList.push_back({physTile, dynTile});
        PageRenderEntry dynEntry;
        dynEntry.physicalTileIndex = dynTile;
        dynEntry.cropViewProjection = cropVP;
        dynEntry.depthBias = view.depthBias;
        dynEntry.slopeBias = view.slopeBias;
        dynEntry.normalBias = view.normalBias;
        dynEntry.layer = ShadowLayer::Dynamic;
        dynamicPageRenderList.push_back(dynEntry);
        ++lastCacheStats.dynamicPagesRendered;
        uint32_t px = pageIdx % data.vsmPagesX;
        uint32_t py = pageIdx / data.vsmPagesX;
        pageTable->mapPage(data.vsmPageTableOffset, px, py, data.vsmPagesX, dynTile);
        data.vsmDynamicTileLastUsedFrame[pageIdx] = frameCounter;
    }

    void ShadowSystem::addPageToRenderLists(LightShadowData& data, uint32_t pageIdx,
                                              const glm::mat4& cropViewProjection,
                                              const ShadowView& view, bool isDirty)
    {
        uint32_t physTile = data.vsmPhysicalTiles[pageIdx];
        if (physTile == vsm::INVALID_TILE) return;
        ++lastCacheStats.totalPages;
        bool forceRender = data.renderedFrameCount < 3;

        if (data.isStatic)
            addStaticLightPage(data, pageIdx, cropViewProjection, view, isDirty, forceRender);
        else
            addDualLayerPage(data, pageIdx, cropViewProjection, view, isDirty, forceRender);
    }

    void ShadowSystem::buildCSMPageRenderList(LightShadowData& data)
    {
        uint32_t pagesPerCascade = data.vsmPagesX;
        bool csmDirty = cameraMovedThisFrame;
        for (uint32_t cascade = 0; cascade < data.settings.cascadeCount && cascade < data.views.size(); ++cascade)
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

    void ShadowSystem::remapClipmapPageTable(LightShadowData& data, uint32_t basePageIdx,
                                               uint32_t pagesPerSide, const glm::ivec2& scroll)
    {
        uint32_t ptOffset = data.vsmPageTableOffset + basePageIdx;
        for (uint32_t py = 0; py < pagesPerSide; ++py)
        {
            for (uint32_t px = 0; px < pagesPerSide; ++px)
            {
                uint32_t sx = (px + scroll.x) % pagesPerSide;
                uint32_t sy = (py + scroll.y) % pagesPerSide;
                uint32_t storageIdx = basePageIdx + sy * pagesPerSide + sx;

                if (storageIdx < data.vsmPhysicalTiles.size())
                {
                    uint32_t physTile = data.vsmPhysicalTiles[storageIdx];
                    if (physTile != vsm::INVALID_TILE)
                        pageTable->mapPage(ptOffset, px, py, pagesPerSide, physTile);
                    else
                        pageTable->unmapPage(ptOffset, px, py, pagesPerSide);
                }
            }
        }
    }

    void ShadowSystem::buildClipmapPageRenderList(LightShadowData& data)
    {
        uint32_t levelCount = std::min(static_cast<uint32_t>(data.views.size()),
                                        static_cast<uint32_t>(data.clipmapLevelPagesPerSide.size()));

        for (uint32_t level = 0; level < levelCount; ++level)
        {
            const auto& view = data.views[level];
            if (view.cached)
                continue;

            uint32_t pagesPerSide = data.clipmapLevelPagesPerSide[level];
            uint32_t basePageIdx = data.clipmapLevelPageOffsets[level];

            glm::ivec2 scroll(0);
            if (level < data.clipmapScrollOffset.size())
                scroll = data.clipmapScrollOffset[level];

            remapClipmapPageTable(data, basePageIdx, pagesPerSide, scroll);

            glm::mat4 renderVP = (level < data.clipmapRenderVP.size())
                ? data.clipmapRenderVP[level] : view.viewProjectionMatrix;

            for (uint32_t py = 0; py < pagesPerSide; ++py)
            {
                for (uint32_t px = 0; px < pagesPerSide; ++px)
                {
                    uint32_t sx = (px + scroll.x) % pagesPerSide;
                    uint32_t sy = (py + scroll.y) % pagesPerSide;
                    uint32_t storageIdx = basePageIdx + sy * pagesPerSide + sx;
                    if (storageIdx >= data.vsmPhysicalTiles.size())
                        continue;

                    glm::mat4 cropMatrix = vsm::computePageCropMatrix(px, py, pagesPerSide, pagesPerSide);
                    addPageToRenderLists(data, storageIdx, cropMatrix * renderVP, view, false);
                }
            }
        }
    }

    void ShadowSystem::buildSingleViewPageRenderList(LightShadowData& data)
    {
        if (data.views.empty())
            return;

        const auto& view = data.views[0];
        if (view.cached) return;
        bool lightMoved = matrixChanged(view.viewProjectionMatrix, data.lastViewProjection);
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

    void ShadowSystem::buildPointPageRenderList(LightShadowData& data)
    {
        if (data.views.size() != ShadowConstants::CUBE_FACE_COUNT)
            return;

        // Point lights have 6 faces laid out as pagesPerFace x (6 * pagesPerFace)
        uint32_t pagesPerFace = data.vsmPagesX;
        uint32_t faceHeight = pagesPerFace;

        bool lightMoved = matrixChanged(data.views[0].viewProjectionMatrix, data.lastViewProjection);
        if (lightMoved)
        {
            data.lastViewProjection = data.views[0].viewProjectionMatrix;
            for (size_t i = 0; i < data.vsmPageDirty.size(); ++i)
                data.vsmPageDirty[i] = true;
        }

        for (uint32_t face = 0; face < ShadowConstants::CUBE_FACE_COUNT; ++face)
        {
            const auto& view = data.views[face];

            for (uint32_t fy = 0; fy < faceHeight; ++fy)
            {
                for (uint32_t fx = 0; fx < pagesPerFace; ++fx)
                {
                    uint32_t pageIdx = (face * faceHeight + fy) * pagesPerFace + fx;
                    if (pageIdx >= data.vsmPhysicalTiles.size())
                        continue;

                    // Crop within this face's page region
                    glm::mat4 cropMatrix = vsm::computePageCropMatrix(fx, fy, pagesPerFace, faceHeight);
                    addPageToRenderLists(data, pageIdx, cropMatrix * view.viewProjectionMatrix, view, false);
                }
            }
        }
    }

    void ShadowSystem::handleDirectionalModeSwitch(LightShadowData& data,
                                                     const types::ShadowSettings& shadowSettings,
                                                     types::DirectionalShadowMode newMode)
    {
        if (data.usesVSM())
            freeVSMPages(data);

        if (newMode == types::DirectionalShadowMode::Clipmap)
        {
            data.type = ShadowMapType::DirectionalClipmap;
            data.settings.clipmapLevelCount = shadowSettings.clipmapLevelCount;
            data.settings.clipmapBaseExtent = shadowSettings.clipmapBaseExtent;
            data.views.resize(shadowSettings.clipmapLevelCount);
            for (size_t i = 0; i < data.views.size(); ++i)
            {
                data.views[i].cascadeIndex = static_cast<uint16_t>(i);
                data.views[i].type = ShadowMapType::DirectionalClipmap;
            }
        }
        else
        {
            data.type = ShadowMapType::DirectionalCSM;
            data.settings.cascadeCount = shadowSettings.cascadeCount;
            data.views.resize(shadowSettings.cascadeCount);
            for (size_t i = 0; i < data.views.size(); ++i)
            {
                data.views[i].cascadeIndex = static_cast<uint16_t>(i);
                data.views[i].type = ShadowMapType::DirectionalCSM;
            }
        }

        if (data.usesVSM())
            allocateVSMPages(data);
        data.matricesDirty = true;
    }

    void ShadowSystem::updateDirectionalSettings(LightShadowData& data,
                                                   const types::ShadowSettings& shadowSettings,
                                                   bool modeChanged, types::DirectionalShadowMode newMode)
    {
        if (data.isDirectionalType() && modeChanged)
        {
            handleDirectionalModeSwitch(data, shadowSettings, newMode);
        }
        else if (data.type == ShadowMapType::DirectionalCSM &&
            data.settings.cascadeCount != shadowSettings.cascadeCount)
        {
            if (data.usesVSM())
                freeVSMPages(data);
            data.settings.cascadeCount = shadowSettings.cascadeCount;
            data.views.resize(shadowSettings.cascadeCount);
            if (data.usesVSM())
                allocateVSMPages(data);
        }
        else if (data.type == ShadowMapType::DirectionalClipmap)
        {
            bool clipmapChanged = (data.settings.clipmapLevelCount != shadowSettings.clipmapLevelCount ||
                                   data.settings.clipmapBaseExtent != shadowSettings.clipmapBaseExtent);
            if (clipmapChanged)
            {
                handleDirectionalModeSwitch(data, shadowSettings,
                    types::DirectionalShadowMode::Clipmap);
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
        globalClipmapLevelCount = shadowSettings.clipmapLevelCount;
        globalClipmapBaseExtent = shadowSettings.clipmapBaseExtent;

        types::DirectionalShadowMode newMode = shadowSettings.directionalMode;
        bool modeChanged = (newMode != globalDirectionalMode);
        globalDirectionalMode = newMode;

        if (!shadowSettings.enabled || shadowSettings.quality == types::ShadowQuality::Off)
            return;
        globalQuality = static_cast<ShadowQuality>(shadowSettings.quality);
        for (auto& [entityId, data] : lightShadowData)
        {
            data.settings.depthBias = shadowSettings.shadowBias;
            data.settings.slopeBias = shadowSettings.slopeBias;
            data.settings.normalBias = shadowSettings.normalBias;
            updateDirectionalSettings(data, shadowSettings, modeChanged, newMode);
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
        uint32_t totalViews = static_cast<uint32_t>(
            directionalShadowViews.size() + pointShadowViews.size() + spotShadowViews.size());
        if (totalViews == 0) return;
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
            if (data.usesVSM())
                usedEntries = std::max(usedEntries, data.vsmPageTableOffset + data.vsmPagesX * data.vsmPagesY);

        if (usedEntries == 0)
            return;

        prevFrameFeedback = feedbackPipeline->readbackResults(usedEntries);
        feedbackHasResults = !prevFrameFeedback.empty();
    }

    void ShadowSystem::allocateNonStaticLightPages(LightShadowData& data)
    {
        uint32_t totalPages = data.vsmPagesX * data.vsmPagesY;
        if (totalPages == 0) return;

        if (data.vsmPhysicalTiles.size() != totalPages) data.vsmPhysicalTiles.resize(totalPages, vsm::INVALID_TILE);
        if (data.vsmPageLastUsedFrame.size() != totalPages) data.vsmPageLastUsedFrame.resize(totalPages, 0);
        if (data.vsmPageDirty.size() != totalPages) data.vsmPageDirty.resize(totalPages, true);

        for (uint32_t i = 0; i < totalPages; ++i)
        {
            if (data.vsmPhysicalTiles[i] != vsm::INVALID_TILE) continue;
            uint32_t tile = tilePool->allocateTile();
            if (tile == vsm::INVALID_TILE) continue;
            data.vsmPhysicalTiles[i] = tile;
            uint32_t px = i % data.vsmPagesX;
            uint32_t py = i / data.vsmPagesX;
            pageTable->mapPage(data.vsmPageTableOffset, px, py, data.vsmPagesX, tile);
            data.vsmPageDirty[i] = true;
        }
    }

    void ShadowSystem::allocateStaticLightPages(LightShadowData& data, bool allowEviction)
    {
        uint32_t totalPages = data.vsmPagesX * data.vsmPagesY;
        if (totalPages == 0) return;

        if (data.vsmPhysicalTiles.size() != totalPages) data.vsmPhysicalTiles.resize(totalPages, vsm::INVALID_TILE);
        if (data.vsmPageLastUsedFrame.size() != totalPages) data.vsmPageLastUsedFrame.resize(totalPages, 0);

        for (uint32_t i = 0; i < totalPages; ++i)
        {
            uint32_t feedbackIdx = data.vsmPageTableOffset + i;
            if (feedbackIdx >= prevFrameFeedback.size()) continue;

            if (prevFrameFeedback[feedbackIdx] > 0)
            {
                data.vsmPageLastUsedFrame[i] = frameCounter;
                if (data.vsmPhysicalTiles[i] == vsm::INVALID_TILE)
                {
                    uint32_t tile = tilePool->allocateTile();
                    if (tile != vsm::INVALID_TILE)
                    {
                        data.vsmPhysicalTiles[i] = tile;
                        pageTable->mapPage(data.vsmPageTableOffset, i % data.vsmPagesX,
                                           i / data.vsmPagesX, data.vsmPagesX, tile);
                    }
                }
            }
            else if (allowEviction &&
                     data.vsmPhysicalTiles[i] != vsm::INVALID_TILE &&
                     frameCounter - data.vsmPageLastUsedFrame[i] > EVICTION_THRESHOLD)
            {
                tilePool->freeTile(data.vsmPhysicalTiles[i]);
                pageTable->unmapPage(data.vsmPageTableOffset, i % data.vsmPagesX,
                                     i / data.vsmPagesX, data.vsmPagesX);
                data.vsmPhysicalTiles[i] = vsm::INVALID_TILE;
            }
        }
    }

    void ShadowSystem::applyFeedbackAllocations()
    {
        if (!tilePool || !pageTable) return;

        // Non-static lights allocate ALL pages eagerly (bypasses feedback).
        // This ensures tiles are ready on the first frame after scene load,
        // before feedback results are available (feedback has 1-frame latency).
        for (auto& [entityId, data] : lightShadowData)
            if (data.usesVSM() && !data.isStatic)
                allocateNonStaticLightPages(data);

        if (!feedbackEnabled || !feedbackHasResults || prevFrameFeedback.empty())
            return;

        static constexpr uint32_t WARMUP_FRAMES = 120;
        bool allowEviction = frameCounter > WARMUP_FRAMES;
        for (auto& [entityId, data] : lightShadowData)
            if (data.usesVSM() && data.isStatic)
                allocateStaticLightPages(data, allowEviction);
    }

    void ShadowSystem::uploadToGPU(vk::CommandBuffer cmd)
    {
        if (!initialized || !shadowsEnabled || !gpuDataManager) return;

        gpuDataManager->buildGPUShadowData(directionalShadowViews, pointShadowViews, spotShadowViews,
                                            lightShadowData);
        gpuDataManager->uploadToGPU(cmd);
        if (pageTable) pageTable->uploadToGPU(cmd);
        gpuDataManager->updateShadowTextureDescriptor(tilePool.get());
        needsUpdate = false;
    }

    void ShadowSystem::recordShadowPass(vk::CommandBuffer cmd,
                                         const ShadowPassParams& params,
                                         const TerrainShadowPassParams* terrainParams)
    {
        if (!passRecorder)
            return;

        ShadowPassContext ctx{
            params, terrainParams,
            tilePool.get(),
            shadowPassPipeline.get(), terrainShadowPipeline.get(),
            pageRenderList, staticPageRenderList, dynamicPageRenderList, tileCopyList,
            lightShadowData, shadowsEnabled, poolFirstUse
        };

        constexpr uint32_t PARALLEL_TILE_THRESHOLD = 5;
        uint32_t frameIndex = core::RenderManager::getImageIndex();
        uint32_t totalPages = static_cast<uint32_t>(
            pageRenderList.size() + staticPageRenderList.size() +
            dynamicPageRenderList.size());

        if (threadPoolManager && threadPoolManager->getThreadCount() > 1 &&
            totalPages >= PARALLEL_TILE_THRESHOLD)
        {
            threadPoolManager->resetFrame(frameIndex);
            passRecorder->recordShadowPassParallel(
                cmd, ctx, threadPoolManager.get(), frameIndex);
        }
        else
        {
            passRecorder->recordShadowPass(cmd, ctx);
        }

        if (shadowsEnabled)
            poolFirstUse = false;
    }
}
