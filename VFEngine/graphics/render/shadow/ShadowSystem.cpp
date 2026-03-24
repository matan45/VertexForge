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

        resourcePool = std::make_unique<ShadowResourcePool>(device);
        resourcePool->init();

        gpuDataManager = std::make_unique<ShadowGPUDataManager>(device);
        gpuDataManager->init();

        passRecorder = std::make_unique<ShadowPassRecorder>(device);

        // Initialize feedback pipeline
        feedbackPipeline = std::make_unique<VSMFeedbackPipeline>(device);
        uint32_t maxFeedbackEntries = vsm::MAX_VSM_LIGHTS * vsm::PAGES_PER_SIDE * vsm::PAGES_PER_SIDE;
        feedbackPipeline->init(maxFeedbackEntries);

        // Bind page table buffer to GPU data manager
        gpuDataManager->setPageTableBuffer(pageTable->getBuffer(), pageTable->getBufferSize());

        gpuDataManager->updateShadowTextureDescriptor(tilePool.get(), resourcePool.get(), lightShadowData);

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

        if (resourcePool)
        {
            resourcePool->cleanup();
            resourcePool.reset();
        }

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
        case ShadowMapType::Directional2D:
            viewCount = 1;
            break;
        default:
            break;
        }

        data.views.resize(viewCount);

        // Allocate resources based on type
        if (type == ShadowMapType::PointCube)
        {
            // Point lights still use cubemaps
            if (!resourcePool || !resourcePool->isInitialized())
                return false;

            ShadowResourceHandle handle = resourcePool->allocateCube(settings.resolution);
            if (!handle.isValid())
            {
                vfLogError("ShadowSystem: Failed to allocate point cube map {}x{}", settings.resolution, settings.resolution);
                return false;
            }

            data.resourceHandle = handle;
            for (size_t i = 0; i < data.views.size(); ++i)
            {
                auto& view = data.views[i];
                view.type = ShadowMapType::PointCube;
                view.layer = static_cast<uint32_t>(i);
            }
        }
        else
        {
            // VSM page-based allocation for directional and spot lights
            if (!allocateVSMPages(data))
            {
                vfLogError("ShadowSystem: Failed to allocate VSM pages for light {}", entityId);
                return false;
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

        if (data.type == ShadowMapType::PointCube)
        {
            if (data.resourceHandle.isValid() && resourcePool)
            {
                resourcePool->free(data.resourceHandle);
            }
        }
        else
        {
            freeVSMPages(data);
        }

        lightShadowData.erase(it);
        needsUpdate = true;
    }

    bool ShadowSystem::allocateVSMPages(LightShadowData& data)
    {
        if (!tilePool || !pageTable)
            return false;

        // Determine page grid size based on light type
        // Limit pages per cascade to balance quality vs draw call count
        // Each page = 1 physical tile (128x128). More pages = better quality but more draws.
        // Page counts: balance quality vs draw call count
        // Each page = 1 full indirect draw. Total draws = sum of all pages across all lights.
        static constexpr uint32_t MAX_DIR_PAGES = 4;  // 4×4 = 512×512 per cascade, 64 draws for 4 cascades
        static constexpr uint32_t MAX_SPOT_PAGES = 1;  // 1×1 = 128×128 per spot, 1 draw per spot

        uint32_t pagesX, pagesY;
        if (data.type == ShadowMapType::DirectionalCSM)
        {
            uint32_t pagesPerCascade = std::clamp(data.settings.resolution / vsm::PAGE_SIZE, 1u, MAX_DIR_PAGES);

            // Update resolution to match actual rendered size (fixes texel snapping)
            data.settings.resolution = pagesPerCascade * vsm::PAGE_SIZE;

            for (size_t i = 0; i < data.views.size(); ++i)
            {
                auto& view = data.views[i];
                view.cascadeIndex = static_cast<uint16_t>(i);
                view.type = data.type;
            }

            // pagesX = per-cascade width, pagesY = all cascades stacked vertically
            pagesX = pagesPerCascade;
            pagesY = pagesPerCascade * data.settings.cascadeCount;
        }
        else if (data.type == ShadowMapType::DirectionalClipmap)
        {
            // Tiered page density: near levels get more pages, far levels get fewer
            // Level 0-3: 4x4=16 pages, Level 4-7: 2x2=4 pages, Level 8+: 1x1=1 page
            uint32_t levelCount = data.settings.clipmapLevelCount;
            data.clipmapLevelPagesPerSide.resize(levelCount);
            data.clipmapLevelPageOffsets.resize(levelCount);
            data.clipmapLastSnapPositions.resize(levelCount, glm::vec2(0.0f));
            data.clipmapScrollOffset.resize(levelCount, glm::ivec2(0));
            data.clipmapPageGridOrigin.resize(levelCount, glm::vec2(0.0f));
            data.clipmapRenderVP.resize(levelCount, glm::mat4(1.0f));
            data.clipmapUVOffset.resize(levelCount, glm::vec2(0.0f));

            uint32_t totalPages = 0;
            for (uint32_t i = 0; i < levelCount; ++i)
            {
                uint32_t pps;
                if (i < 4)       pps = MAX_DIR_PAGES;  // 4x4
                else if (i < 8)  pps = 2;              // 2x2
                else             pps = 1;              // 1x1

                data.clipmapLevelPagesPerSide[i] = pps;
                data.clipmapLevelPageOffsets[i] = totalPages;
                totalPages += pps * pps;
            }

            for (size_t i = 0; i < data.views.size(); ++i)
            {
                auto& view = data.views[i];
                view.cascadeIndex = static_cast<uint16_t>(i);
                view.type = data.type;
            }

            // Allocate as a flat page table block (totalPages entries wide, 1 tall)
            // We use pagesX=totalPages, pagesY=1 as a flat layout
            pagesX = totalPages;
            pagesY = 1;
        }
        else if (data.type == ShadowMapType::Spot2D || data.type == ShadowMapType::Directional2D)
        {
            uint32_t pages = std::clamp(data.settings.resolution / vsm::PAGE_SIZE, 1u, MAX_SPOT_PAGES);
            data.settings.resolution = pages * vsm::PAGE_SIZE;
            pagesX = pages;
            pagesY = pages;

            if (!data.views.empty())
            {
                data.views[0].cascadeIndex = 0;
                data.views[0].type = data.type;
            }
        }
        else
        {
            return false;
        }

        // Allocate page table block
        uint32_t offset = pageTable->allocateBlock(pagesX, pagesY);
        if (offset == vsm::INVALID_TILE)
            return false;

        // Allocate physical tiles for all pages (brute-force Phase 1)
        uint32_t totalPages = pagesX * pagesY;
        data.vsmPhysicalTiles.resize(totalPages);

        for (uint32_t i = 0; i < totalPages; ++i)
        {
            uint32_t tile = tilePool->allocateTile();
            if (tile == vsm::INVALID_TILE)
            {
                // Rollback
                for (uint32_t j = 0; j < i; ++j)
                    tilePool->freeTile(data.vsmPhysicalTiles[j]);
                pageTable->freeBlock(offset, pagesX, pagesY);
                data.vsmPhysicalTiles.clear();
                vfLogError("ShadowSystem: Ran out of physical tiles, allocated {}/{}", i, totalPages);
                return false;
            }
            data.vsmPhysicalTiles[i] = tile;

            // Map in page table
            uint32_t pageX = i % pagesX;
            uint32_t pageY = i / pagesX;
            pageTable->mapPage(offset, pageX, pageY, pagesX, tile);
        }

        data.vsmPagesX = pagesX;
        data.vsmPagesY = pagesY;
        data.vsmPageTableOffset = offset;
        data.vsmLightIndex = nextVSMLightIndex++;
        // Initialize with a future frame so pages survive warmup/eviction
        data.vsmPageLastUsedFrame.resize(totalPages, frameCounter + EVICTION_THRESHOLD + 120);
        data.vsmPageDirty.resize(totalPages, true); // all pages dirty initially

        return true;
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
    }

    void ShadowSystem::setDeletionQueue(core::DeferredDeletionQueue* queue)
    {
        if (resourcePool)
        {
            resourcePool->setDeletionQueue(queue);
        }
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
                data.isStatic = registry.get<components::TransformComponent>(entity).isStatic;
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

        for (const auto& [entityId, data] : lightShadowData)
        {
            if (!data.settings.enabled || !data.settings.castShadows)
                continue;

            if (data.isDirectionalType())
            {
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
                    debugInfos.push_back(info);
                }
            }
        }

        for (const auto& [entityId, data] : lightShadowData)
        {
            if (!data.settings.enabled || !data.settings.castShadows)
                continue;

            if (data.type == ShadowMapType::PointCube)
            {
                if (!data.views.empty())
                {
                    const auto& view = data.views[0];
                    ShadowDebugInfo info;
                    info.type = data.type;
                    info.cascadeIndex = 0;
                    info.entityId = entityId;
                    info.viewProjectionMatrix = view.viewProjectionMatrix;
                    info.lightPosition = glm::vec3(view.lightPosition);
                    info.lightDirection = glm::vec3(0.0f, -1.0f, 0.0f);
                    info.nearPlane = view.nearPlane;
                    info.farPlane = view.farPlane;
                    debugInfos.push_back(info);
                }
            }
        }

        for (const auto& [entityId, data] : lightShadowData)
        {
            if (!data.settings.enabled || !data.settings.castShadows)
                continue;

            if (data.type == ShadowMapType::Spot2D)
            {
                if (!data.views.empty())
                {
                    const auto& view = data.views[0];
                    ShadowDebugInfo info;
                    info.type = data.type;
                    info.cascadeIndex = 0;
                    info.entityId = entityId;
                    info.viewProjectionMatrix = view.viewProjectionMatrix;
                    info.lightPosition = glm::vec3(view.lightPosition);
                    info.lightDirection = glm::vec3(view.lightDirection);
                    info.nearPlane = view.nearPlane;
                    info.farPlane = view.farPlane;
                    debugInfos.push_back(info);
                }
            }
        }

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
