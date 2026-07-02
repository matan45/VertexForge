#include "ShadowSystem.hpp"
#include "ShadowPageOverlap.hpp"
#include "../gpudriven/scene/ShadowBinPacking.hpp" // B1: buildPageBinBase + INVALID_SHADOW_BIN_SLOT
#include "../../core/RenderManager.hpp"
#include "../../core/ThreadCommandPoolManager.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "print/Log.hpp"
#include <algorithm>

namespace render::shadow
{
    // A1: per-page dynamic marking. Previously ONE moving entity marked EVERY page of every
    // non-static VSM light dynamic (up to 144 dynamic-tile copies + re-renders/frame from a single
    // walking unit). Now each dynamic caster's bounds (dynamicCasterBounds, filled by the renderer
    // from resolved GPU object data before beginFrame) are projected into each light view's page
    // grid via the shared ShadowPageOverlap math, marking only the pages actually overlapped.
    void ShadowSystem::determineDynamicPages()
    {
        for (auto& [entityId, data] : lightShadowData)
        {
            if (data.isStatic || !data.usesVSM()) continue;
            uint32_t totalPages = data.vsmPagesX * data.vsmPagesY;
            if (data.vsmPageHasDynamic.size() != totalPages)
                data.vsmPageHasDynamic.assign(totalPages, false);
            else
                std::fill(data.vsmPageHasDynamic.begin(), data.vsmPageHasDynamic.end(), false);
        }

        if (dynamicCasterBounds.empty())
            return;

        for (auto& [entityId, data] : lightShadowData)
        {
            if (data.isStatic || !data.usesVSM()) continue;
            if (data.vsmPagesX == 0 || data.views.empty()) continue;
            if (data.vsmPageHasDynamic.empty()) continue;
            markDynamicPagesForLight(data);
        }
    }

    // Mirror the per-type page layout of build{Clipmap,SingleView,Point}PageRenderList: project
    // each dynamic caster's local AABB by (view VP * modelMatrix) into that view's page block and
    // OR-mark every overlapped page.
    void ShadowSystem::markDynamicPagesForLight(LightShadowData& data)
    {
        auto markPage = [&](uint32_t pageIdx)
        {
            if (pageIdx < data.vsmPageHasDynamic.size())
                data.vsmPageHasDynamic[pageIdx] = true;
        };

        const uint32_t viewCount = static_cast<uint32_t>(data.views.size());

        if (data.type == ShadowMapType::Directional)
        {
            const uint32_t ppl = data.vsmPagesX; // pagesPerLevel (square blocks stacked per level)
            for (uint32_t level = 0; level < viewCount; ++level)
            {
                const glm::mat4& vp = data.views[level].viewProjectionMatrix;
                for (const auto& caster : dynamicCasterBounds)
                {
                    PageRange r = ShadowPageOverlap::overlappedPages(
                        caster.localMin, caster.localMax, vp * caster.modelMatrix, ppl, ppl);
                    if (!r.valid) continue;
                    for (uint32_t fy = r.fy0; fy <= r.fy1; ++fy)
                        for (uint32_t fx = r.fx0; fx <= r.fx1; ++fx)
                            markPage((level * ppl + fy) * ppl + fx);
                }
            }
        }
        else if (data.type == ShadowMapType::PointCube)
        {
            const uint32_t ppf = data.vsmPagesX; // pagesPerFace (square, 6 faces stacked)
            for (uint32_t face = 0; face < ShadowConstants::CUBE_FACE_COUNT && face < viewCount; ++face)
            {
                const glm::mat4& vp = data.views[face].viewProjectionMatrix;
                for (const auto& caster : dynamicCasterBounds)
                {
                    PageRange r = ShadowPageOverlap::overlappedPages(
                        caster.localMin, caster.localMax, vp * caster.modelMatrix, ppf, ppf);
                    if (!r.valid) continue;
                    for (uint32_t fy = r.fy0; fy <= r.fy1; ++fy)
                        for (uint32_t fx = r.fx0; fx <= r.fx1; ++fx)
                            markPage((face * ppf + fy) * ppf + fx);
                }
            }
        }
        else // Spot2D: single view, vsmPagesX x vsmPagesY grid
        {
            const glm::mat4& vp = data.views[0].viewProjectionMatrix;
            for (const auto& caster : dynamicCasterBounds)
            {
                PageRange r = ShadowPageOverlap::overlappedPages(
                    caster.localMin, caster.localMax, vp * caster.modelMatrix,
                    data.vsmPagesX, data.vsmPagesY);
                if (!r.valid) continue;
                for (uint32_t fy = r.fy0; fy <= r.fy1; ++fy)
                    for (uint32_t fx = r.fx0; fx <= r.fx1; ++fx)
                        markPage(fy * data.vsmPagesX + fx);
            }
        }
    }

    void ShadowSystem::buildPageRenderList()
    {
        pageRenderList.clear();
        staticPageRenderList.clear();
        dynamicPageRenderList.clear();
        tileCopyList.clear();
        lastCacheStats = {};
        binActiveThisFrame = false; // B1: recomputed per frame by buildClipmapPageRenderList
        binBuiltThisFrame = false;
        for (auto& [entityId, data] : lightShadowData)
        {
            if (!data.settings.enabled || !data.settings.castShadows)
                continue;
            if (data.vsmPhysicalTiles.empty())
                continue;

            // When RT directional shadows override the clipmap full-screen, nothing samples the
            // directional clipmap this frame — skip rendering its pages entirely. Tiles stay
            // resident (we only skip the render list, not registration/allocation), so toggling
            // RT off resumes the clipmap within a frame. See directionalRTOverrideActive.
            if (directionalRTOverrideActive && data.type == ShadowMapType::Directional)
                continue;

            uint32_t totalPages = data.vsmPagesX * data.vsmPagesY;
            if (data.vsmPageDirty.size() != totalPages)
                data.vsmPageDirty.resize(totalPages, true);

            bool needsDynamicArrays = !data.isStatic || data.type == ShadowMapType::PointCube;
            if (needsDynamicArrays)
            {
                if (data.vsmDynamicTiles.size() != totalPages)
                    data.vsmDynamicTiles.resize(totalPages, vsm::INVALID_TILE);
                if (data.vsmPageHasDynamic.size() != totalPages)
                    data.vsmPageHasDynamic.resize(totalPages, false);
                if (data.vsmDynamicTileLastUsedFrame.size() != totalPages)
                    data.vsmDynamicTileLastUsedFrame.resize(totalPages, 0);
            }

            if (data.type == ShadowMapType::PointCube)
                buildPointPageRenderList(data);
            else if (data.type == ShadowMapType::Directional)
                buildClipmapPageRenderList(data);
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

        // This page has no dynamic caster this frame (A1). Point the page table back at the static
        // tile IMMEDIATELY so the page shows the fresh static shadow instead of the stale dynamic
        // tile (which still holds the caster's last position) during the cooldown window. Under the
        // old mark-every-page scheme this branch only ran when the scene had zero dynamic objects,
        // so the eager remap was unnecessary; with per-page marking a page a caster just left needs
        // it every frame until the dynamic tile is actually freed below.
        uint32_t px = pageIdx % data.vsmPagesX;
        uint32_t py = pageIdx / data.vsmPagesX;
        pageTable->mapPage(data.vsmPageTableOffset, px, py, data.vsmPagesX, physTile);

        if (frameCounter - data.vsmDynamicTileLastUsedFrame[pageIdx] > DYNAMIC_TILE_COOLDOWN)
        {
            tilePool->freeTile(data.vsmDynamicTiles[pageIdx]);
            data.vsmDynamicTiles[pageIdx] = vsm::INVALID_TILE;
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

        if (data.isStatic && data.type != ShadowMapType::PointCube)
            addStaticLightPage(data, pageIdx, cropViewProjection, view, isDirty, forceRender);
        else
            addDualLayerPage(data, pageIdx, cropViewProjection, view, isDirty, forceRender);
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

                    glm::mat4 cropMatrix = vsm::computePageCropMatrix(fx, fy, pagesPerFace, faceHeight);
                    addPageToRenderLists(data, pageIdx, cropMatrix * view.viewProjectionMatrix, view, lightMoved);
                }
            }
        }
    }

    void ShadowSystem::setShadowCullEnabled(bool enabled)
    {
        if (shadowCullEnabled == enabled)
            return;
        shadowCullEnabled = enabled;
        // Toggling changes how directional pages render (single-layer bin vs dual-layer) and what
        // the page table points at; invalidate every page so the next frame re-renders + re-maps.
        notifySceneChanged();
    }

    bool ShadowSystem::addBinPage(LightShadowData& data, uint32_t pageIdx, uint32_t pageLinear,
                                  const glm::mat4& cropVP, const ShadowView& view, bool isDirty,
                                  std::vector<uint32_t>& outPageLinears,
                                  std::vector<size_t>& outEntryIndices)
    {
        uint32_t physTile = data.vsmPhysicalTiles[pageIdx];
        if (physTile == vsm::INVALID_TILE)
            return false;
        ++lastCacheStats.totalPages;

        // Bin pages render into the STATIC tile; keep the page table pointed there so a page that
        // was previously dual-layer (mapped to a dynamic tile) samples the fresh bin render.
        uint32_t px = pageIdx % data.vsmPagesX;
        uint32_t py = pageIdx / data.vsmPagesX;
        pageTable->mapPage(data.vsmPageTableOffset, px, py, data.vsmPagesX, physTile);

        bool forceRender = data.renderedFrameCount < 3;
        if (!isDirty && !data.vsmPageDirty[pageIdx] && !forceRender)
        {
            ++lastCacheStats.cachedPages;
            return false;
        }

        PageRenderEntry entry;
        entry.physicalTileIndex = physTile;
        entry.cropViewProjection = cropVP;
        entry.depthBias = view.depthBias;
        entry.slopeBias = view.slopeBias;
        entry.normalBias = view.normalBias;
        entry.layer = ShadowLayer::All;
        // entry.binSlot stays INVALID_BIN_SLOT until back-filled after buildPageBinBase.
        outEntryIndices.push_back(pageRenderList.size());
        pageRenderList.push_back(entry);
        outPageLinears.push_back(pageLinear);
        ++lastCacheStats.renderedPages;
        if (!forceRender)
            data.vsmPageDirty[pageIdx] = false;
        return true;
    }

    void ShadowSystem::buildClipmapPageRenderList(LightShadowData& data)
    {
        // A directional clipmap is laid out like point-light cube faces, one "face" per
        // clipmap level: a tall block of pagesPerLevel x (pagesPerLevel * levelCount). The
        // linear page index level*pagesPerLevel^2 + fy*pagesPerLevel + fx matches both the
        // per-level GPU page-table offsets and the feedback indexing.
        uint32_t levelCount = static_cast<uint32_t>(data.views.size());
        if (levelCount == 0)
            return;

        uint32_t pagesPerLevel = data.vsmPagesX;
        if (pagesPerLevel == 0)
            return;

        // The clipmap re-centers (and a level's VP changes) whenever the camera crosses that
        // level's texel boundary or the light rotates. Each level snaps to its OWN texel grid:
        // level 0 has the smallest texels and scrolls almost every frame while panning, but the
        // coarse levels (2x the extent each, so far larger texels) move rarely. Invalidate ONLY
        // the levels whose VP actually changed — invalidating all levels on any level-0 move
        // re-rendered the 5 coarse levels (the bulk of the ~pagesPerLevel^2 * levelCount pages)
        // every frame for nothing (the directional-shadow draw-call blow-up).
        if (data.lastViewProjectionPerLevel.size() != levelCount)
            data.lastViewProjectionPerLevel.assign(levelCount, glm::mat4(0.0f));

        const uint32_t pagesPerLevelSq = pagesPerLevel * pagesPerLevel;
        std::vector<bool> levelMoved(levelCount, false);
        for (uint32_t level = 0; level < levelCount; ++level)
        {
            if (matrixChanged(data.views[level].viewProjectionMatrix, data.lastViewProjectionPerLevel[level]))
            {
                data.lastViewProjectionPerLevel[level] = data.views[level].viewProjectionMatrix;
                levelMoved[level] = true;
                uint32_t base = level * pagesPerLevelSq;
                uint32_t end = std::min<uint32_t>(base + pagesPerLevelSq,
                                                  static_cast<uint32_t>(data.vsmPageDirty.size()));
                for (uint32_t p = base; p < end; ++p)
                    data.vsmPageDirty[p] = true;
            }
        }

        // B1: the first directional light this frame renders as single-layer "All" bin pages
        // (per-page GPU cull). Additional directional lights (rare) fall back to the legacy path.
        const bool binMode = shadowCullEnabled && !binBuiltThisFrame;
        std::vector<uint32_t> binPageLinears;
        std::vector<size_t> binEntryIndices;

        for (uint32_t level = 0; level < levelCount; ++level)
        {
            const auto& view = data.views[level];

            for (uint32_t fy = 0; fy < pagesPerLevel; ++fy)
            {
                for (uint32_t fx = 0; fx < pagesPerLevel; ++fx)
                {
                    uint32_t pageIdx = (level * pagesPerLevel + fy) * pagesPerLevel + fx;
                    if (pageIdx >= data.vsmPhysicalTiles.size())
                        continue;

                    glm::mat4 cropMatrix = vsm::computePageCropMatrix(fx, fy, pagesPerLevel, pagesPerLevel);
                    glm::mat4 cropVP = cropMatrix * view.viewProjectionMatrix;
                    if (binMode)
                        addBinPage(data, pageIdx, pageIdx, cropVP, view, levelMoved[level],
                                   binPageLinears, binEntryIndices);
                    else
                        addPageToRenderLists(data, pageIdx, cropVP, view, levelMoved[level]);
                }
            }
        }

        if (binMode)
        {
            binBuiltThisFrame = true;
            binPagesPerLevel = pagesPerLevel;
            binLevelCount = levelCount;
            const uint32_t totalPages = data.vsmPagesX * data.vsmPagesY;

            std::vector<gpudriven::ShadowBinPageRequest> requests;
            requests.reserve(binPageLinears.size());
            for (uint32_t pl : binPageLinears)
                requests.push_back({pl});

            std::vector<uint32_t> assigned;
            uint32_t used = gpudriven::buildPageBinBase(requests, totalPages, binPageBase, &assigned);
            for (size_t i = 0; i < binEntryIndices.size(); ++i)
                pageRenderList[binEntryIndices[i]].binSlot = assigned[i];
            binActiveThisFrame = used > 0;
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

        // Directional clipmap tuning (read before the early-out so it always tracks settings).
        clipmapLevelCount = std::max(1u, shadowSettings.clipmapLevelCount);
        clipmapBaseExtent = shadowSettings.clipmapBaseExtent;
        clipmapDepthRange = shadowSettings.clipmapDepthRange;

        if (!shadowSettings.enabled || shadowSettings.quality == types::ShadowQuality::Off)
            return;
        globalQuality = static_cast<ShadowQuality>(shadowSettings.quality);

        // A quality change alters clipmapPagesPerLevel(), which the directional crop
        // matrices read live every frame. The directional page-table block, however, was
        // laid out with the registration-time pagesPerLevel (data.vsmPagesX). If they no
        // longer agree the crop matrices no longer match the page grid -> garbled shadows.
        // Re-lay-out any directional block whose width drifted from the new quality.
        const uint32_t newPagesPerLevel = clipmapPagesPerLevel();
        for (auto& [entityId, data] : lightShadowData)
        {
            data.settings.depthBias = shadowSettings.shadowBias;
            data.settings.slopeBias = shadowSettings.slopeBias;
            data.settings.normalBias = shadowSettings.normalBias;
            data.settingsDirty = true;

            if (data.type == ShadowMapType::Directional && data.vsmPagesX != newPagesPerLevel)
            {
                freeVSMPages(data);
                allocateDirectionalBlock(data);
                data.matricesDirty = true;
                for (auto& view : data.views)
                    view.cached = false;
            }
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
            // A3: cap new-page allocations per frame. Remaining pages are retried next frame
            // (this runs every frame and skips already-allocated tiles), which smooths the
            // camera-cut / scene-load spike instead of allocating a whole clipmap at once.
            if (newPagesAllocatedThisFrame >= MAX_NEW_PAGES_PER_FRAME) break;
            uint32_t tile = tilePool->allocateTile();
            if (tile == vsm::INVALID_TILE) continue;
            ++newPagesAllocatedThisFrame;
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
                // A3: cap new-page allocations per frame; still-requested pages are re-served
                // next frame (feedback keeps them > 0), so streaming self-amortizes. Guard only
                // the allocate sub-branch so the eviction path below still runs for other pages.
                if (data.vsmPhysicalTiles[i] == vsm::INVALID_TILE &&
                    newPagesAllocatedThisFrame < MAX_NEW_PAGES_PER_FRAME)
                {
                    uint32_t tile = tilePool->allocateTile();
                    if (tile != vsm::INVALID_TILE)
                    {
                        ++newPagesAllocatedThisFrame;
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
        // Directional clipmaps are the exception: too large to allocate eagerly, so they
        // go through the feedback path below (level-0 pages were seeded at registration).
        for (auto& [entityId, data] : lightShadowData)
            if (data.usesVSM() && !data.isStatic && !data.feedbackDriven)
                allocateNonStaticLightPages(data);

        if (!feedbackEnabled || !feedbackHasResults || prevFrameFeedback.empty())
            return;

        static constexpr uint32_t WARMUP_FRAMES = 120;
        bool allowEviction = frameCounter > WARMUP_FRAMES;
        for (auto& [entityId, data] : lightShadowData)
            if (data.usesVSM() && (data.isStatic || data.feedbackDriven))
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
