#include "ShadowSystem.hpp"
#include "../../core/Device.hpp"
#include "../../core/Utilities.hpp"
#include "../../core/ThreadCommandPoolManager.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "print/Log.hpp"
#include "threading/JobSystem.hpp"

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

        shadowPassPipeline->createFramebuffer(
            tilePool->getPoolImageView(),
            vsm::PHYSICAL_POOL_DIM,
            vsm::PHYSICAL_POOL_DIM
        );
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
                                     tilePool->getRenderPass());
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
        case ShadowMapType::DirectionalCSM:
            viewCount = settings.cascadeCount;
            break;
        case ShadowMapType::PointCube:
            viewCount = 6;
            break;
        case ShadowMapType::DirectionalClipmap:
            viewCount = settings.clipmapLevelCount;
            break;
        case ShadowMapType::Spot2D:
            viewCount = 1;
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

        if (type == ShadowMapType::PointCube)
        {
            for (size_t i = 0; i < data.views.size(); ++i)
            {
                auto& view = data.views[i];
                view.type = ShadowMapType::PointCube;
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

    ShadowSystem::PageDimensions ShadowSystem::allocateCSMPages(LightShadowData& data)
    {
        static constexpr uint32_t MAX_DIR_PAGES = 4;
        uint32_t pagesPerCascade = std::clamp(data.settings.resolution / vsm::PAGE_SIZE, 1u, MAX_DIR_PAGES);
        data.settings.resolution = pagesPerCascade * vsm::PAGE_SIZE;

        for (size_t i = 0; i < data.views.size(); ++i)
        {
            data.views[i].cascadeIndex = static_cast<uint16_t>(i);
            data.views[i].type = data.type;
        }

        return {pagesPerCascade, pagesPerCascade * data.settings.cascadeCount, true};
    }

    ShadowSystem::PageDimensions ShadowSystem::allocateClipmapPages(LightShadowData& data)
    {
        static constexpr uint32_t MAX_DIR_PAGES = 4;
        uint32_t levelCount = data.settings.clipmapLevelCount;
        data.clipmapLevelPagesPerSide.resize(levelCount);
        data.clipmapLevelPageOffsets.resize(levelCount);
        data.clipmapLastSnapPositions.resize(levelCount, glm::vec2(0.0f));
        data.clipmapScrollOffset.resize(levelCount, glm::ivec2(0));
        data.clipmapPageGridOrigin.resize(levelCount, glm::vec2(0.0f));
        data.clipmapRenderVP.resize(levelCount, glm::mat4(1.0f));
        data.clipmapUVOffset.resize(levelCount, glm::vec2(0.0f));
        data.clipmapLevelInitialized.resize(levelCount, false);

        uint32_t totalPages = 0;
        for (uint32_t i = 0; i < levelCount; ++i)
        {
            uint32_t pps;
            if (i < 4)       pps = MAX_DIR_PAGES;
            else if (i < 8)  pps = 2;
            else             pps = 1;

            data.clipmapLevelPagesPerSide[i] = pps;
            data.clipmapLevelPageOffsets[i] = totalPages;
            totalPages += pps * pps;
        }

        for (size_t i = 0; i < data.views.size(); ++i)
        {
            data.views[i].cascadeIndex = static_cast<uint16_t>(i);
            data.views[i].type = data.type;
        }

        return {totalPages, 1, true};
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

    uint32_t ShadowSystem::evictLowestPriorityPage(float requestingPriority)
    {
        float lowestPriority = requestingPriority * 0.5f;
        LightShadowData* victimData = nullptr;
        uint32_t victimPageIdx = 0;
        uint32_t oldestFrame = UINT32_MAX;

        for (auto& [entityId, data] : lightShadowData)
        {
            if (data.shadowPriority >= lowestPriority)
                continue;
            if (data.vsmPhysicalTiles.empty())
                continue;

            for (uint32_t i = 0; i < data.vsmPhysicalTiles.size(); ++i)
            {
                if (data.vsmPhysicalTiles[i] == vsm::INVALID_TILE)
                    continue;
                uint32_t lastUsed = (i < data.vsmPageLastUsedFrame.size())
                    ? data.vsmPageLastUsedFrame[i] : 0;
                if (lastUsed < oldestFrame)
                {
                    oldestFrame = lastUsed;
                    victimData = &data;
                    victimPageIdx = i;
                }
            }
        }

        if (!victimData)
            return vsm::INVALID_TILE;

        uint32_t tile = victimData->vsmPhysicalTiles[victimPageIdx];
        victimData->vsmPhysicalTiles[victimPageIdx] = vsm::INVALID_TILE;

        uint32_t px = victimPageIdx % victimData->vsmPagesX;
        uint32_t py = victimPageIdx / victimData->vsmPagesX;
        pageTable->unmapPage(victimData->vsmPageTableOffset, px, py, victimData->vsmPagesX);

        return tile;
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

        PageDimensions dims;
        switch (data.type)
        {
        case ShadowMapType::DirectionalCSM:    dims = allocateCSMPages(data); break;
        case ShadowMapType::DirectionalClipmap: dims = allocateClipmapPages(data); break;
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

        // Clear clipmap tracking
        data.clipmapLastSnapPositions.clear();
        data.clipmapLevelPageOffsets.clear();
        data.clipmapLevelPagesPerSide.clear();
        data.clipmapScrollOffset.clear();
        data.clipmapPageGridOrigin.clear();
        data.clipmapRenderVP.clear();
        data.clipmapUVOffset.clear();
        data.clipmapLevelInitialized.clear();
    }

    void ShadowSystem::setDeletionQueue(core::DeferredDeletionQueue* queue)
    {
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
                // Point lights always non-static in shadow system — their 6-face VSM
                // layout doesn't work with the static page rendering path
                bool entityStatic = registry.get<components::TransformComponent>(entity).isStatic;
                data.isStatic = (data.type != ShadowMapType::PointCube) && entityStatic;
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
            case ShadowMapType::DirectionalCSM:
            case ShadowMapType::DirectionalClipmap:
                info.type = 0;
                break;
            case ShadowMapType::Spot2D:
                info.type = 1;
                break;
            case ShadowMapType::PointCube:
                info.type = 2;
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

    void ShadowSystem::addDirectionalDebugInfos(std::vector<ShadowDebugInfo>& infos) const
    {
        for (const auto& [entityId, data] : lightShadowData)
        {
            if (!data.settings.enabled || !data.settings.castShadows || !data.isDirectionalType())
                continue;

            for (size_t i = 0; i < data.views.size(); ++i)
            {
                const auto& view = data.views[i];
                ShadowDebugInfo info;
                info.type = data.type;
                info.cascadeIndex = static_cast<uint32_t>(i);
                info.entityId = entityId;
                info.viewProjectionMatrix = view.viewProjectionMatrix;
                info.lightPosition = glm::vec3(view.lightPosition);
                info.lightDirection = glm::vec3(view.lightDirection);
                info.nearPlane = view.nearPlane;
                info.farPlane = view.farPlane;
                infos.push_back(info);
            }
        }
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

        addDirectionalDebugInfos(debugInfos);
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
