#include "ShadowSystem.hpp"
#include "../../core/Device.hpp"
#include "../../core/Utilities.hpp"
#include "../../core/ThreadCommandPoolManager.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "print/Log.hpp"
#include "threading/JobSystem.hpp"
#include <algorithm>

namespace render::shadow
{
    ShadowSystem::ShadowSystem(core::Device& device)
        : device(device)
    {
        lastCacheStats = {};
    }

    ShadowSystem::~ShadowSystem()
    {
        cleanup();
    }

    void ShadowSystem::init()
    {
        if (initialized)
        {
            return;
        }

        tilePool = std::make_unique<VSMPhysicalTilePool>(device);
        tilePool->init();

        pageTable = std::make_unique<VSMPageTable>(device);
        pageTable->init();

        gpuDataManager = std::make_unique<ShadowGPUDataManager>(device);
        gpuDataManager->init();

        passRecorder = std::make_unique<ShadowPassRecorder>(device);

        // Initialize feedback pipeline
        feedbackPipeline = std::make_unique<VSMFeedbackPipeline>(device);
        uint32_t maxFeedbackEntries = vsm::MAX_VSM_LIGHTS * vsm::PAGES_PER_SIDE * vsm::PAGES_PER_SIDE;
        feedbackPipeline->init(maxFeedbackEntries);

        // Bind page table buffer to GPU data manager
        gpuDataManager->setPageTableBuffer(pageTable->getBuffer(), pageTable->getBufferSize());

        gpuDataManager->updateShadowTextureDescriptor(tilePool.get());

        // Initialize per-thread command pools for parallel shadow recording
        threadPoolManager = std::make_unique<core::ThreadCommandPoolManager>();
        threadPoolManager->init(device, threading::JobSystem::instance().getThreadCount());

        initialized = true;
    }

    void ShadowSystem::cleanup()
    {
        if (!initialized)
            return;

        const auto& logicalDevice = device.getLogicalDevice();
        logicalDevice.waitIdle();

        if (shadowPassPipeline)
        {
            shadowPassPipeline->cleanup();
            shadowPassPipeline.reset();
        }

        if (terrainShadowPipeline)
        {
            terrainShadowPipeline->cleanup();
            terrainShadowPipeline.reset();
        }

        if (gpuDataManager)
        {
            gpuDataManager->cleanup();
            gpuDataManager.reset();
        }

        if (feedbackPipeline)
        {
            feedbackPipeline->cleanup();
            feedbackPipeline.reset();
        }

        passRecorder.reset();

        if (threadPoolManager)
        {
            threadPoolManager->cleanUp();
            threadPoolManager.reset();
        }

        lightShadowData.clear();
        directionalShadowViews.clear();
        pointShadowViews.clear();
        spotShadowViews.clear();
        pageRenderList.clear();

        if (pageTable)
        {
            pageTable->cleanup();
            pageTable.reset();
        }

        if (tilePool)
        {
            tilePool->cleanup();
            tilePool.reset();
        }

        initialized = false;
    }

    void ShadowSystem::initShadowPass(vk::DescriptorSetLayout perDrawLayout,
                                      vk::DescriptorSetLayout meshletDataLayout,
                                      vk::DescriptorSetLayout vertexDataLayout,
                                      vk::DescriptorSetLayout boneMatrixLayout)
    {
        if (!initialized)
        {
            vfLogError("ShadowSystem::initShadowPass() called before init()");
            return;
        }

        if (shadowPassPipeline)
        {
            return;
        }

        shadowPassPipeline = std::make_unique<ShadowPassPipeline>(device);
        shadowPassPipeline->init(perDrawLayout, meshletDataLayout, vertexDataLayout, boneMatrixLayout,
                                 tilePool->getDepthFormat());
    }

    void ShadowSystem::updateCameraDescriptor(vk::Buffer cameraBuffer, vk::DeviceSize bufferSize)
    {
        if (shadowPassPipeline && shadowPassPipeline->isInitialized())
        {
            shadowPassPipeline->updateCameraDescriptor(cameraBuffer, bufferSize);
        }
    }

    vk::DescriptorSet ShadowSystem::getShadowCameraDescSet() const
    {
        if (shadowPassPipeline && shadowPassPipeline->isInitialized())
        {
            return shadowPassPipeline->getCameraDescriptorSet();
        }
        return {};
    }

    void ShadowSystem::initTerrainShadowPass(vk::DescriptorSetLayout terrainDataLayout,
                                              vk::DescriptorSetLayout terrainMeshletLayout,
                                              vk::DescriptorSetLayout terrainVertexLayout)
    {
        if (!initialized)
        {
            vfLogError("ShadowSystem::initTerrainShadowPass() called before init()");
            return;
        }

        if (!shadowPassPipeline || !shadowPassPipeline->isInitialized())
        {
            vfLogError("ShadowSystem::initTerrainShadowPass() called before initShadowPass()");
            return;
        }

        if (terrainShadowPipeline)
        {
            return;
        }

        terrainShadowPipeline = std::make_unique<TerrainShadowPipeline>(device);
        terrainShadowPipeline->init(terrainDataLayout, terrainMeshletLayout, terrainVertexLayout,
                                     tilePool->getDepthFormat());
    }

    bool ShadowSystem::registerLight(uint32_t entityId, ShadowMapType type, const ShadowSettings& settings)
    {
        if (lightShadowData.contains(entityId))
        {
            auto& data = lightShadowData[entityId];
            data.settings = settings;
            data.settingsDirty = true;
            return true;
        }

        LightShadowData data;
        data.settings = settings;
        data.type = type;
        data.lightEntityId = entityId;
        data.matricesDirty = true;
        data.settingsDirty = true;

        // Check if light entity is static
        auto& registry = scene::EntityRegistry::getRegistry();
        auto entity = static_cast<entt::entity>(entityId);
        if (registry.valid(entity) && registry.all_of<components::TransformComponent>(entity))
        {
            data.isStatic = registry.get<components::TransformComponent>(entity).isStatic;
        }

        if (registry.valid(entity) && registry.all_of<components::ShadowOverrideComponent>(entity))
        {
            const auto& override = registry.get<components::ShadowOverrideComponent>(entity);
            if (override.depthBias >= 0.0f) data.settings.depthBias = override.depthBias;
            if (override.slopeBias >= 0.0f) data.settings.slopeBias = override.slopeBias;
            if (override.normalBias >= 0.0f) data.settings.normalBias = override.normalBias;
        }

        uint32_t viewCount = 1;
        switch (type)
        {
        case ShadowMapType::PointCube:
            viewCount = 6;
            break;
        case ShadowMapType::Spot2D:
            viewCount = 1;
            break;
        case ShadowMapType::Directional:
            // One VSM view per clipmap level (see DirectionalShadowCalculator).
            viewCount = std::max(1u, data.settings.clipmapLevelCount);
            break;
        default:
            break;
        }

        data.views.resize(viewCount);

        // VSM page-based allocation for all light types
        if (!allocateVSMPages(data))
        {
            vfLogError("ShadowSystem: Failed to allocate VSM pages for light {}", entityId);
            return false;
        }

        // Tier 1: route non-static spot lights through the feedback-driven allocation path
        // (allocateStaticLightPages) instead of re-rendering every page eagerly each frame.
        // Tiles were just allocated eagerly above (no first-frame gap); after WARMUP_FRAMES the
        // feedback path evicts pages the screen never samples, so only the spot's visible
        // footprint renders. Point lights stay eager — the VSM feedback shader skips cube faces.
        // Directional clipmaps stay resident to avoid feedback-latency holes while the camera pans.
        if (type == ShadowMapType::Spot2D && !data.isStatic)
            data.feedbackDriven = true;

        if (type == ShadowMapType::PointCube || type == ShadowMapType::Directional)
        {
            // Both lay out their views as a tall page block: cascadeIndex selects the
            // sub-block (cube face for point, clipmap level for directional).
            for (size_t i = 0; i < data.views.size(); ++i)
            {
                auto& view = data.views[i];
                view.type = type;
                view.cascadeIndex = static_cast<uint16_t>(i);
                view.layer = static_cast<uint32_t>(i);
            }
        }

        lightShadowData[entityId] = std::move(data);
        needsUpdate = true;

        return true;
    }

    void ShadowSystem::unregisterLight(uint32_t entityId)
    {
        auto it = lightShadowData.find(entityId);
        if (it == lightShadowData.end())
        {
            vfLogWarning("ShadowSystem: Attempted to unregister unknown light {}", entityId);
            return;
        }

        auto& data = it->second;
        freeVSMPages(data);

        lightShadowData.erase(it);
        needsUpdate = true;
    }

    ShadowSystem::PageDimensions ShadowSystem::allocateSpotPages(LightShadowData& data, uint32_t maxPages)
    {
        uint32_t pages = std::clamp(data.settings.resolution / vsm::PAGE_SIZE, 1u, maxPages);
        data.settings.resolution = pages * vsm::PAGE_SIZE;

        if (!data.views.empty())
        {
            data.views[0].cascadeIndex = 0;
            data.views[0].type = data.type;
        }

        return {pages, pages, true};
    }

    ShadowSystem::PageDimensions ShadowSystem::allocatePointPages(LightShadowData& data, uint32_t /*maxPages*/)
    {
        // Point lights use 1 page per face (6 total) to keep tile usage reasonable
        // Each face covers 90 degrees — higher resolution can be added later
        uint32_t pagesPerFace = 1;
        data.settings.resolution = pagesPerFace * vsm::PAGE_SIZE;

        for (size_t i = 0; i < data.views.size(); ++i)
        {
            data.views[i].cascadeIndex = static_cast<uint16_t>(i);
            data.views[i].type = data.type;
        }

        return {pagesPerFace, pagesPerFace * ShadowConstants::CUBE_FACE_COUNT, true};
    }

    bool ShadowSystem::allocatePhysicalTiles(LightShadowData& data, uint32_t pagesX, uint32_t pagesY)
    {
        uint32_t offset = pageTable->allocateBlock(pagesX, pagesY);
        if (offset == vsm::INVALID_TILE)
            return false;

        uint32_t totalPages = pagesX * pagesY;
        data.vsmPhysicalTiles.resize(totalPages);

        for (uint32_t i = 0; i < totalPages; ++i)
        {
            uint32_t tile = tilePool->allocateTile();
            if (tile == vsm::INVALID_TILE)
            {
                tile = evictLowestPriorityPage(data.shadowPriority);
            }
            if (tile == vsm::INVALID_TILE)
            {
                for (uint32_t j = 0; j < i; ++j)
                    tilePool->freeTile(data.vsmPhysicalTiles[j]);
                pageTable->freeBlock(offset, pagesX, pagesY);
                data.vsmPhysicalTiles.clear();
                vfLogError("ShadowSystem: Ran out of physical tiles, allocated {}/{}", i, totalPages);
                return false;
            }
            data.vsmPhysicalTiles[i] = tile;
            pageTable->mapPage(offset, i % pagesX, i / pagesX, pagesX, tile);
        }

        data.vsmPagesX = pagesX;
        data.vsmPagesY = pagesY;
        data.vsmPageTableOffset = offset;
        data.vsmLightIndex = nextVSMLightIndex++;
        data.vsmPageLastUsedFrame.resize(totalPages, frameCounter + EVICTION_THRESHOLD + 120);
        data.vsmPageDirty.resize(totalPages, true);
        return true;
    }

    void ShadowSystem::ensureEvictionHeap()
    {
        // A2: build the O(all resident pages) heap lazily. beginFrame only marks it dirty; the
        // rebuild happens here, on the first eviction request of the frame (reached only when the
        // physical tile pool is exhausted during light registration). Built with current state,
        // which is fresher than the old unconditional beginFrame-time rebuild.
        if (!evictionHeapDirty)
            return;
        buildEvictionHeap();
        evictionHeapDirty = false;
    }

    void ShadowSystem::buildEvictionHeap()
    {
        evictionHeap.clear();
        for (auto& [entityId, data] : lightShadowData)
        {
            if (data.vsmPhysicalTiles.empty())
                continue;
            for (uint32_t i = 0; i < data.vsmPhysicalTiles.size(); ++i)
            {
                if (data.vsmPhysicalTiles[i] == vsm::INVALID_TILE)
                    continue;
                uint32_t lastUsed = (i < data.vsmPageLastUsedFrame.size())
                    ? data.vsmPageLastUsedFrame[i] : 0;
                evictionHeap.push_back({data.shadowPriority, lastUsed, entityId, i});
            }
        }
        std::make_heap(evictionHeap.begin(), evictionHeap.end(), std::greater<>{});
    }

    uint32_t ShadowSystem::evictLowestPriorityPage(float requestingPriority)
    {
        ensureEvictionHeap();
        float threshold = requestingPriority * 0.5f;

        while (!evictionHeap.empty())
        {
            std::pop_heap(evictionHeap.begin(), evictionHeap.end(), std::greater<>{});
            auto candidate = evictionHeap.back();
            evictionHeap.pop_back();

            if (candidate.priority >= threshold)
                return vsm::INVALID_TILE;

            auto it = lightShadowData.find(candidate.entityId);
            if (it == lightShadowData.end())
                continue;
            auto& data = it->second;
            if (candidate.pageIdx >= data.vsmPhysicalTiles.size())
                continue;
            if (data.vsmPhysicalTiles[candidate.pageIdx] == vsm::INVALID_TILE)
                continue;

            uint32_t tile = data.vsmPhysicalTiles[candidate.pageIdx];
            data.vsmPhysicalTiles[candidate.pageIdx] = vsm::INVALID_TILE;

            uint32_t px = candidate.pageIdx % data.vsmPagesX;
            uint32_t py = candidate.pageIdx / data.vsmPagesX;
            pageTable->unmapPage(data.vsmPageTableOffset, px, py, data.vsmPagesX);
            return tile;
        }
        return vsm::INVALID_TILE;
    }

    uint32_t ShadowSystem::clipmapPagesPerLevel() const
    {
        switch (globalQuality)
        {
        case ShadowQuality::Low:    return 2;
        case ShadowQuality::Medium: return 4;
        case ShadowQuality::High:   return 6;  // 6x6 pages/level (was 8x8) — ~44% fewer pages, marginal quality cost
        case ShadowQuality::Ultra:  return 8;  // Ultra keeps the full 8x8 for users who want max resolution
        default:                    return 4;
        }
    }

    bool ShadowSystem::allocateDirectionalBlock(LightShadowData& data)
    {
        // A directional clipmap is laid out exactly like a point light's 6 cube faces, but
        // with one "face" per clipmap level: a tall block of pagesPerLevel x (pagesPerLevel
        // * levelCount) pages in the virtual page table. The linear page index across the
        // whole block equals (level * pagesPerLevel^2 + localIndex), which matches both the
        // per-level GPU page-table offsets and the feedback buffer indexing.
        uint32_t levelCount = std::max(1u, static_cast<uint32_t>(data.views.size()));
        uint32_t pagesPerLevel = clipmapPagesPerLevel();
        data.settings.resolution = pagesPerLevel * vsm::PAGE_SIZE;

        uint32_t pagesX = pagesPerLevel;
        uint32_t pagesY = pagesPerLevel * levelCount;

        uint32_t offset = pageTable->allocateBlock(pagesX, pagesY);
        if (offset == vsm::INVALID_TILE)
            return false;

        uint32_t totalPages = pagesX * pagesY;

        // Reserve tracking arrays. Physical tiles for the coarser levels are filled in on
        // demand by screen-space feedback (allocateStaticLightPages path).
        data.vsmPagesX = pagesX;
        data.vsmPagesY = pagesY;
        data.vsmPageTableOffset = offset;
        data.vsmLightIndex = nextVSMLightIndex++;
        data.vsmPhysicalTiles.assign(totalPages, vsm::INVALID_TILE);
        data.vsmPageLastUsedFrame.assign(totalPages, frameCounter + EVICTION_THRESHOLD + 120);
        data.vsmPageDirty.assign(totalPages, true);
        data.feedbackDriven = false;

        // Keep the full directional clipmap resident. The current clipmap sizes are small
        // relative to the physical tile pool, and this avoids one-frame missing pages while
        // the play camera pans.
        for (uint32_t i = 0; i < totalPages; ++i)
        {
            uint32_t tile = tilePool->allocateTile();
            if (tile == vsm::INVALID_TILE)
                tile = evictLowestPriorityPage(data.shadowPriority);
            if (tile == vsm::INVALID_TILE)
                break; // best-effort; the eager non-feedback path will retry later
            data.vsmPhysicalTiles[i] = tile;
            pageTable->mapPage(offset, i % pagesPerLevel, i / pagesPerLevel, pagesPerLevel, tile);
        }
        return true;
    }

    bool ShadowSystem::allocateVSMPages(LightShadowData& data)
    {
        if (!tilePool || !pageTable)
            return false;

        uint32_t maxSpotPages = 1;
        switch (globalQuality)
        {
        case ShadowQuality::Medium: maxSpotPages = 2; break;
        case ShadowQuality::High:
        case ShadowQuality::Ultra:  maxSpotPages = 4; break;
        default: break;
        }

        if (data.type == ShadowMapType::Directional)
            return allocateDirectionalBlock(data);

        PageDimensions dims;
        switch (data.type)
        {
        case ShadowMapType::Spot2D:            dims = allocateSpotPages(data, maxSpotPages); break;
        case ShadowMapType::PointCube:         dims = allocatePointPages(data, maxSpotPages); break;
        default: return false;
        }

        if (!dims.valid)
            return false;

        return allocatePhysicalTiles(data, dims.x, dims.y);
    }

    void ShadowSystem::freeVSMPages(LightShadowData& data)
    {
        if (!tilePool || !pageTable)
            return;

        // Free static layer physical tiles
        for (uint32_t tile : data.vsmPhysicalTiles)
        {
            if (tile != vsm::INVALID_TILE)
                tilePool->freeTile(tile);
        }
        data.vsmPhysicalTiles.clear();

        // Free dynamic layer tiles
        for (uint32_t tile : data.vsmDynamicTiles)
        {
            if (tile != vsm::INVALID_TILE)
                tilePool->freeTile(tile);
        }
        data.vsmDynamicTiles.clear();
        data.vsmPageHasDynamic.clear();
        data.vsmDynamicTileLastUsedFrame.clear();

        // Free page table block
        if (data.vsmPagesX > 0 && data.vsmPagesY > 0)
        {
            pageTable->freeBlock(data.vsmPageTableOffset, data.vsmPagesX, data.vsmPagesY);
        }

        data.vsmPagesX = 0;
        data.vsmPagesY = 0;
        data.vsmPageTableOffset = 0;
    }

    void ShadowSystem::invalidateStaticShadow(uint32_t entityId)
    {
        auto it = lightShadowData.find(entityId);
        if (it != lightShadowData.end())
        {
            it->second.invalidateCache();
            needsUpdate = true;
        }
    }

    void ShadowSystem::invalidateAllStaticShadows()
    {
        for (auto& [entityId, data] : lightShadowData)
        {
            if (data.isStatic)
            {
                data.invalidateCache();
            }
        }
        needsUpdate = true;
    }

    void ShadowSystem::updateStaticFlags()
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        for (auto& [entityId, data] : lightShadowData)
        {
            auto entity = static_cast<entt::entity>(entityId);
            bool wasStatic = data.isStatic;
            if (registry.valid(entity) && registry.all_of<components::TransformComponent>(entity))
            {
                // Point lights and directional clipmaps are always non-static in the shadow
                // system: point lights use a 6-face layout incompatible with the static page
                // path, and the directional clipmap re-centers on the (moving) camera every
                // frame and must capture dynamic casters via the dual-layer path.
                bool entityStatic = registry.get<components::TransformComponent>(entity).isStatic;
                data.isStatic = (data.type != ShadowMapType::PointCube &&
                                 data.type != ShadowMapType::Directional) && entityStatic;
            }
            else
            {
                data.isStatic = false;
            }

            if (wasStatic != data.isStatic)
            {
                data.invalidateCache();
            }
        }
    }

    ShadowSystem::ShadowCacheStats ShadowSystem::getShadowCacheStats() const
    {
        return lastCacheStats;
    }

    std::vector<ShadowSystem::PerLightStats> ShadowSystem::getPerLightStats() const
    {
        std::vector<PerLightStats> result;
        result.reserve(lightShadowData.size());

        for (const auto& [entityId, data] : lightShadowData)
        {
            PerLightStats info;
            info.entityId = entityId;

            switch (data.type)
            {
            case ShadowMapType::Spot2D:
                info.type = 1;
                break;
            case ShadowMapType::PointCube:
                info.type = 2;
                break;
            case ShadowMapType::Directional:
                info.type = 0;
                break;
            default:
                info.type = 3;
                break;
            }

            info.pagesAllocated = static_cast<uint32_t>(data.vsmPhysicalTiles.size());
            uint32_t dirty = 0;
            uint32_t cached = 0;
            for (size_t i = 0; i < data.vsmPageDirty.size(); ++i)
            {
                if (data.vsmPageDirty[i])
                    ++dirty;
                else
                    ++cached;
            }
            info.pagesDirty = dirty;
            info.pagesCached = cached;
            result.push_back(info);
        }
        return result;
    }

    int32_t ShadowSystem::getShadowViewIndex(uint32_t entityId) const
    {
        if (!shadowsEnabled)
            return -1;

        auto it = entityToShadowIndex.find(entityId);
        if (it != entityToShadowIndex.end())
            return it->second;

        return -1;
    }

    vk::DescriptorSetLayout ShadowSystem::getShadowDataLayout() const
    {
        return gpuDataManager ? gpuDataManager->getShadowDataLayout() : nullptr;
    }

    vk::DescriptorSet ShadowSystem::getShadowDataDescSet() const
    {
        return gpuDataManager ? gpuDataManager->getShadowDataDescSet() : nullptr;
    }

    vk::DescriptorSetLayout ShadowSystem::getShadowTextureLayout() const
    {
        return gpuDataManager ? gpuDataManager->getShadowTextureLayout() : nullptr;
    }

    vk::DescriptorSet ShadowSystem::getShadowTextureDescSet() const
    {
        return gpuDataManager ? gpuDataManager->getShadowTextureDescSet() : nullptr;
    }

    uint32_t ShadowSystem::getActiveShadowCasterCount() const
    {
        uint32_t count = 0;
        for (const auto& [entityId, data] : lightShadowData)
        {
            if (data.settings.enabled && data.settings.castShadows)
                ++count;
        }
        return count;
    }

    uint32_t ShadowSystem::getActiveShadowViewCount() const
    {
        return static_cast<uint32_t>(
            directionalShadowViews.size() +
            pointShadowViews.size() +
            spotShadowViews.size()
        );
    }

    float ShadowSystem::getPoolUtilization() const
    {
        return tilePool ? tilePool->getUtilization() : 0.0f;
    }

    void ShadowSystem::addSingleViewDebugInfo(std::vector<ShadowDebugInfo>& infos, ShadowMapType type) const
    {
        for (const auto& [entityId, data] : lightShadowData)
        {
            if (!data.settings.enabled || !data.settings.castShadows || data.type != type)
                continue;
            if (data.views.empty())
                continue;

            const auto& view = data.views[0];
            ShadowDebugInfo info;
            info.type = data.type;
            info.cascadeIndex = 0;
            info.entityId = entityId;
            info.viewProjectionMatrix = view.viewProjectionMatrix;
            info.lightPosition = glm::vec3(view.lightPosition);
            info.lightDirection = (type == ShadowMapType::PointCube)
                ? glm::vec3(0.0f, -1.0f, 0.0f)
                : glm::vec3(view.lightDirection);
            info.nearPlane = view.nearPlane;
            info.farPlane = view.farPlane;
            infos.push_back(info);
        }
    }

    std::vector<ShadowDebugInfo> ShadowSystem::getShadowDebugInfo() const
    {
        std::vector<ShadowDebugInfo> debugInfos;
        if (!shadowsEnabled)
            return debugInfos;

        debugInfos.reserve(
            directionalShadowViews.size() +
            pointShadowViews.size() +
            spotShadowViews.size()
        );

        addSingleViewDebugInfo(debugInfos, ShadowMapType::Directional);
        addSingleViewDebugInfo(debugInfos, ShadowMapType::PointCube);
        addSingleViewDebugInfo(debugInfos, ShadowMapType::Spot2D);
        return debugInfos;
    }

    ShadowRecordingStats ShadowSystem::getShadowRecordingStats() const
    {
        if (passRecorder)
        {
            return passRecorder->getLastStats();
        }
        return {};
    }
}
