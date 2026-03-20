#include "OffScreenController.hpp"
#include "../core/VulkanContext.hpp"
#include "../core/SwapChain.hpp"
#include "../core/AsyncComputeManager.hpp"
#include "../core/ThreadCommandPoolManager.hpp"
#include "../core/Device.hpp"
#include "../render/OffScreenViewPort.hpp"
#include "../render/RenderPassHandler.hpp"
#include "../render/gpudriven/GPUDrivenRenderer.hpp"
#include "../render/shadow/ShadowSystem.hpp"
#include "../render/postprocess/PostProcessPipeline.hpp"
#include "../render/volumetric/VolumetricFogComposite.hpp"
#include "offscreen/CullingStatsCollector.hpp"
#include "offscreen/CameraController.hpp"
#include "offscreen/SceneBVHManager.hpp"
#include "offscreen/LightBVHManager.hpp"
#include "types/RenderSettings.hpp"

namespace controllers
{
    services::CullingDebugStats OffScreenController::getCullingStats() const
    {
        return statsCollector->collect(offScreen->getRenderPassHandler(), bvhManager.get(), lightBvhManager.get());
    }

    void OffScreenController::applyShadowSettings(const types::RenderSettings& settings)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        auto* gpuDriven = renderHandler->getGPUDrivenRenderer();
        if (!gpuDriven) return;

        auto* shadowSystem = gpuDriven->getShadowSystem();
        if (shadowSystem)
            shadowSystem->applyRenderSettings(settings);

        auto* lightBufferManager = gpuDriven->getLightBufferManager();
        if (lightBufferManager)
            lightBufferManager->setShadowIntensity(settings.shadows.shadowIntensity);

        gpuDriven->setFrustumCullingEnabled(settings.culling.frustumCullingEnabled);
        gpuDriven->setOcclusionCullingEnabled(settings.culling.occlusionCullingEnabled);
        gpuDriven->setLODSelectionEnabled(settings.culling.lodSelectionEnabled);
        gpuDriven->setMeshletFrustumCullingEnabled(settings.culling.meshletFrustumCullingEnabled);
        gpuDriven->setMeshletBackfaceCullingEnabled(settings.culling.meshletBackfaceCullingEnabled);
        gpuDriven->setMeshletOcclusionCullingEnabled(settings.culling.meshletOcclusionCullingEnabled);
        gpuDriven->setTerrainFrustumCullingEnabled(settings.culling.terrainFrustumCullingEnabled);
        gpuDriven->setTerrainMeshletCullingEnabled(settings.culling.terrainMeshletCullingEnabled);
        gpuDriven->setLODCrossfadeEnabled(settings.culling.lodCrossfadeEnabled);
        gpuDriven->setGlobalLodBias(settings.culling.globalLodBias);

        gpuDriven->setDistanceCullingEnabled(settings.distanceCulling.enabled);
        using namespace render::gpudriven::ObjectCategory;
        gpuDriven->setCategoryDistance(StaticMesh, settings.distanceCulling.staticMeshDistance);
        gpuDriven->setCategoryDistance(Terrain, settings.distanceCulling.terrainDistance);
        gpuDriven->setCategoryDistance(Foliage, settings.distanceCulling.foliageDistance);
        gpuDriven->setCategoryDistance(VFX, settings.distanceCulling.vfxDistance);
        gpuDriven->setCategoryDistance(Decals, settings.distanceCulling.decalDistance);
        gpuDriven->setCategoryDistance(Billboard, settings.distanceCulling.billboardDistance);
        gpuDriven->setCategoryDistance(Water, settings.distanceCulling.waterDistance);
        gpuDriven->setShadowDistanceMultiplier(settings.distanceCulling.shadowDistanceMultiplier);

        renderHandler->setVFXDistanceCullingEnabled(settings.distanceCulling.enabled);
        renderHandler->setVFXDrawDistance(settings.distanceCulling.vfxDistance);
        renderHandler->setBillboardDistanceCullingEnabled(settings.distanceCulling.enabled);
        renderHandler->setBillboardDrawDistance(settings.distanceCulling.billboardDistance);
        renderHandler->setWaterDistanceCullingEnabled(settings.distanceCulling.enabled);
        renderHandler->setWaterDrawDistance(settings.distanceCulling.waterDistance);
        renderHandler->setTerrainDistanceCullingEnabled(settings.distanceCulling.enabled);
        renderHandler->setTerrainDrawDistance(settings.distanceCulling.terrainDistance);

        renderHandler->setWBOITEnabled(settings.transparency.wboitEnabled);

        gpuDriven->setTerrainRenderingEnabled(settings.terrain.enabled);
        gpuDriven->setTerrainLODBias(settings.terrain.lodBias);
        gpuDriven->setTerrainErrorThreshold(settings.terrain.errorThreshold);
        gpuDriven->setTerrainTextureScale(settings.terrain.textureScale);
        gpuDriven->setTerrainShadowLOD(settings.terrain.shadowLOD);
    }

    services::ShadowStats OffScreenController::getShadowStats() const
    {
        services::ShadowStats stats{};

        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return stats;

        auto* gpuDriven = renderHandler->getGPUDrivenRenderer();
        if (!gpuDriven) return stats;

        auto* shadowSystem = gpuDriven->getShadowSystem();
        if (!shadowSystem) return stats;

        stats.atlasWidth = render::shadow::vsm::PHYSICAL_POOL_DIM;
        stats.atlasHeight = render::shadow::vsm::PHYSICAL_POOL_DIM;
        stats.atlasUtilization = shadowSystem->getPoolUtilization();
        stats.activeShadowCasters = shadowSystem->getActiveShadowCasterCount();
        stats.activeShadowViews = shadowSystem->getActiveShadowViewCount();
        stats.directionalLightCount = static_cast<uint32_t>(shadowSystem->getDirectionalShadowViews().size());
        stats.pointLightCount = static_cast<uint32_t>(shadowSystem->getPointShadowViews().size());
        stats.spotLightCount = static_cast<uint32_t>(shadowSystem->getSpotShadowViews().size());
        stats.pointResolution = render::shadow::vsm::PAGE_SIZE;

        auto cacheStats = shadowSystem->getShadowCacheStats();
        stats.totalStaticLights = cacheStats.totalStaticLights;
        stats.cachedShadowMaps = cacheStats.cachedShadowMaps;
        stats.renderedThisFrame = cacheStats.renderedThisFrame;
        stats.skippedThisFrame = cacheStats.skippedThisFrame;
        stats.totalPages = cacheStats.totalPages;
        stats.renderedPages = cacheStats.renderedPages;
        stats.cachedPages = cacheStats.cachedPages;
        stats.staticPagesRendered = cacheStats.staticPagesRendered;
        stats.dynamicPagesRendered = cacheStats.dynamicPagesRendered;
        stats.tileCopiesThisFrame = cacheStats.tileCopiesThisFrame;
        stats.dynamicTilesAllocated = cacheStats.dynamicTilesAllocated;

        return stats;
    }

    services::GPUPipelineStatus OffScreenController::getGPUPipelineStatus() const
    {
        services::GPUPipelineStatus s;
        s.asyncComputeEnabled = false;
        s.parallelShadowRecording = false;
        s.parallelSceneRecording = false;
        s.asyncComputeQueueFamily = 0;
        s.shadowRecordingUs = 0.0f;
        s.shadowTileCount = 0;
        s.shadowThreadsUsed = 0;
        s.sceneRecordingUs = 0.0f;
        s.sceneSecondaryCount = 0;
        s.workerThreadCount = 0;

        if (asyncComputeManager)
        {
            s.asyncComputeEnabled = asyncComputeManager->isEnabled();
            if (s.asyncComputeEnabled)
                s.asyncComputeQueueFamily = device.getQueueFamilyIndices().asyncComputeFamily.value_or(0);
        }

        if (sceneThreadPoolManager)
        {
            uint32_t tc = sceneThreadPoolManager->getThreadCount();
            s.parallelSceneRecording = tc > 1;
            s.workerThreadCount = tc;
        }

        auto* rh = offScreen ? offScreen->getRenderPassHandler() : nullptr;
        if (rh)
        {
            s.sceneRecordingUs = rh->getLastSceneRecordingUs();
            s.sceneSecondaryCount = rh->getLastSceneSecondaryCount();

            if (rh->isGPUDrivenRendererInitialized())
            {
                auto* gpu = rh->getGPUDrivenRenderer();
                if (gpu)
                {
                    auto* shadow = gpu->getShadowSystem();
                    if (shadow)
                    {
                        auto ss = shadow->getShadowRecordingStats();
                        s.parallelShadowRecording = ss.usedParallel;
                        s.shadowRecordingUs = ss.recordingUs;
                        s.shadowTileCount = ss.tileCount;
                        s.shadowThreadsUsed = ss.threadsUsed;
                    }
                }
            }
        }

        return s;
    }

    void OffScreenController::applyPostProcessSettings(const postprocess::PostProcessSettings& settings)
    {
        currentPostProcessSettings = settings;

        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        auto* gpuRenderer = renderHandler->getGPUDrivenRenderer();

        if (gpuRenderer && renderHandler->isGPUDrivenRendererInitialized())
        {
            if (settings.volumetricFog.enabled && !gpuRenderer->getVolumetricPipeline())
            {
                gpuRenderer->initVolumetricFog(settings.volumetricFog.quality);
                activeVolumetricQuality = settings.volumetricFog.quality;
            }
            else if (settings.volumetricFog.enabled && gpuRenderer->getVolumetricPipeline()
                     && settings.volumetricFog.quality != activeVolumetricQuality)
            {
                device.getLogicalDevice().waitIdle();
                renderHandler->resetVolumetricFogComposite();
                gpuRenderer->initVolumetricFog(settings.volumetricFog.quality);
                activeVolumetricQuality = settings.volumetricFog.quality;
            }

            if (gpuRenderer->getVolumetricPipeline())
            {
                gpuRenderer->updateVolumetricSettings(settings.volumetricFog);
                gpuRenderer->setVolumetricFogEnabled(settings.volumetricFog.enabled);

                if (!renderHandler->getVolumetricFogComposite())
                    renderHandler->initVolumetricFogComposite(gpuRenderer->getVolumetricPipeline());

                auto* composite = renderHandler->getVolumetricFogComposite();
                if (composite)
                    composite->setIntensity(settings.volumetricFog.intensity);
            }
        }

        if (cameraController)
        {
            cameraController->setTAAEnabled(settings.enabled && settings.taa.enabled);
            auto extent = swapChain.getSwapchainExtent();
            cameraController->setViewportExtent(extent.width, extent.height);
        }

        auto* pipeline = renderHandler->getPostProcessPipeline();
        if (pipeline)
            pipeline->applySettings(settings);
    }

    postprocess::PostProcessSettings OffScreenController::getPostProcessSettings() const
    {
        return currentPostProcessSettings;
    }

    void OffScreenController::setPostProcessEnabled(bool enabled)
    {
        currentPostProcessSettings.enabled = enabled;

        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        auto* pipeline = renderHandler->getPostProcessPipeline();
        if (pipeline)
            pipeline->applySettings(currentPostProcessSettings);
    }

    bool OffScreenController::isPostProcessEnabled() const
    {
        return currentPostProcessSettings.enabled;
    }

    void OffScreenController::setViewMode(uint32_t mode)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler) renderHandler->setViewMode(mode);
    }

    uint32_t OffScreenController::getViewMode() const
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        return renderHandler ? renderHandler->getViewMode() : 0;
    }

    void OffScreenController::setFrustumCullingEnabled(bool enabled)
    {
        auto* rh = offScreen->getRenderPassHandler();
        if (rh) rh->setFrustumCullingEnabled(enabled);
    }

    void OffScreenController::setLODSelectionEnabled(bool enabled)
    {
        auto* rh = offScreen->getRenderPassHandler();
        if (rh) rh->setLODSelectionEnabled(enabled);
    }

    void OffScreenController::setLODCrossfadeEnabled(bool enabled)
    {
        auto* rh = offScreen->getRenderPassHandler();
        if (rh) rh->setLODCrossfadeEnabled(enabled);
    }

    void OffScreenController::setMeshletFrustumCullingEnabled(bool enabled)
    {
        auto* rh = offScreen->getRenderPassHandler();
        if (rh) rh->setMeshletFrustumCullingEnabled(enabled);
    }

    void OffScreenController::setMeshletBackfaceCullingEnabled(bool enabled)
    {
        auto* rh = offScreen->getRenderPassHandler();
        if (rh) rh->setMeshletBackfaceCullingEnabled(enabled);
    }

    void OffScreenController::setMeshletOcclusionCullingEnabled(bool enabled)
    {
        auto* rh = offScreen->getRenderPassHandler();
        if (rh) rh->setMeshletOcclusionCullingEnabled(enabled);
    }

    void OffScreenController::setDistanceCullingEnabled(bool enabled)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        auto* gpuDriven = renderHandler->getGPUDrivenRenderer();
        if (gpuDriven) gpuDriven->setDistanceCullingEnabled(enabled);

        renderHandler->setVFXDistanceCullingEnabled(enabled);
        renderHandler->setBillboardDistanceCullingEnabled(enabled);
        renderHandler->setWaterDistanceCullingEnabled(enabled);
        renderHandler->setTerrainDistanceCullingEnabled(enabled);
    }

    void OffScreenController::setCategoryDistance(uint32_t category, float distance)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        auto* gpuDriven = renderHandler->getGPUDrivenRenderer();
        if (gpuDriven) gpuDriven->setCategoryDistance(category, distance);

        if (category == render::gpudriven::ObjectCategory::Terrain)
            renderHandler->setTerrainDrawDistance(distance);
        else if (category == render::gpudriven::ObjectCategory::VFX)
            renderHandler->setVFXDrawDistance(distance);
        else if (category == render::gpudriven::ObjectCategory::Billboard)
            renderHandler->setBillboardDrawDistance(distance);
        else if (category == render::gpudriven::ObjectCategory::Water)
            renderHandler->setWaterDrawDistance(distance);
    }

    void OffScreenController::setShadowDistanceMultiplier(float multiplier)
    {
        auto* rh = offScreen->getRenderPassHandler();
        if (!rh) return;
        auto* gpuDriven = rh->getGPUDrivenRenderer();
        if (gpuDriven) gpuDriven->setShadowDistanceMultiplier(multiplier);
    }

    void OffScreenController::setGlobalLodBias(float bias)
    {
        auto* rh = offScreen->getRenderPassHandler();
        if (rh) rh->setGlobalLodBias(bias);
    }

    void OffScreenController::setTerrainFrustumCullingEnabled(bool enabled)
    {
        auto* rh = offScreen->getRenderPassHandler();
        if (rh) rh->setTerrainFrustumCullingEnabled(enabled);
    }

    void OffScreenController::setTerrainMeshletCullingEnabled(bool enabled)
    {
        auto* rh = offScreen->getRenderPassHandler();
        if (rh) rh->setTerrainMeshletCullingEnabled(enabled);
    }

    void OffScreenController::setWBOITEnabled(bool enabled)
    {
        auto* rh = offScreen->getRenderPassHandler();
        if (rh) rh->setWBOITEnabled(enabled);
    }

    void OffScreenController::setTerrainRenderingEnabled(bool enabled)
    {
        auto* rh = offScreen->getRenderPassHandler();
        if (rh) rh->setTerrainRenderingEnabled(enabled);
    }

    void OffScreenController::setBillboardRenderingEnabled(bool enabled)
    {
        auto* rh = offScreen->getRenderPassHandler();
        if (rh) rh->setBillboardRenderingEnabled(enabled);
    }

    void OffScreenController::setDecalRenderingEnabled(bool enabled)
    {
        auto* rh = offScreen->getRenderPassHandler();
        if (rh) rh->setDecalRenderingEnabled(enabled);
    }

    void OffScreenController::setDecalDrawList(const std::vector<services::DecalRenderData>& decals)
    {
        auto* rh = offScreen->getRenderPassHandler();
        if (rh) rh->setDecalDrawList(decals);
    }

    void OffScreenController::setTerrainLODBias(float bias)
    {
        auto* rh = offScreen->getRenderPassHandler();
        if (rh) rh->setTerrainLODBias(bias);
    }

    void OffScreenController::setTerrainErrorThreshold(float threshold)
    {
        auto* rh = offScreen->getRenderPassHandler();
        if (rh) rh->setTerrainErrorThreshold(threshold);
    }

    void OffScreenController::setTerrainTextureScale(float scale)
    {
        auto* rh = offScreen->getRenderPassHandler();
        if (rh) rh->setTerrainTextureScale(scale);
    }

    void OffScreenController::setTerrainShadowLOD(uint32_t lod)
    {
        auto* rh = offScreen->getRenderPassHandler();
        if (rh) rh->setTerrainShadowLOD(lod);
    }

    void OffScreenController::setTerrainSVTEnabled(bool enabled)
    {
        auto* rh = offScreen->getRenderPassHandler();
        if (rh) rh->setTerrainSVTEnabled(enabled);
    }

    void OffScreenController::setVFXRuntimeProvider(services::IVFXRuntimeProvider* provider)
    {
        auto* rh = offScreen->getRenderPassHandler();
        if (rh) rh->setVFXRuntimeProvider(provider);
    }

    void OffScreenController::setTerrainRenderProvider(services::ITerrainRenderProvider* provider)
    {
        auto* rh = offScreen->getRenderPassHandler();
        if (rh) rh->setTerrainRenderProvider(provider);
    }

    void OffScreenController::setWaterRenderProvider(services::IWaterRenderProvider* provider)
    {
        auto* rh = offScreen->getRenderPassHandler();
        if (rh) rh->setWaterRenderProvider(provider);
    }

    void OffScreenController::setGrassRenderProvider(services::IGrassRenderProvider* provider)
    {
        auto* rh = offScreen->getRenderPassHandler();
        if (rh) rh->setGrassRenderProvider(provider);
    }

    void OffScreenController::setVegetationRenderProvider(services::IVegetationRenderProvider* provider)
    {
        auto* rh = offScreen->getRenderPassHandler();
        if (rh) rh->setVegetationRenderProvider(provider);
    }

    render::RenderPassHandler* OffScreenController::getRenderPassHandler() const
    {
        return offScreen ? offScreen->getRenderPassHandler() : nullptr;
    }

    plugin::RenderHookHandle OffScreenController::registerRenderHook(
        plugin::RenderPassHookPoint hookPoint,
        plugin::RenderHookCallback callback)
    {
        auto* handler = offScreen ? offScreen->getRenderPassHandler() : nullptr;
        if (!handler) return {};
        return handler->registerRenderHook(hookPoint, std::move(callback));
    }

    void OffScreenController::unregisterRenderHook(plugin::RenderHookHandle handle)
    {
        auto* handler = offScreen ? offScreen->getRenderPassHandler() : nullptr;
        if (handler)
            handler->unregisterRenderHook(handle);
    }

    void OffScreenController::addTerrainFrustum(const glm::mat4& viewProjection, const glm::vec3& cameraPos)
    {
        auto* handler = offScreen ? offScreen->getRenderPassHandler() : nullptr;
        if (handler)
        {
            math::Frustum frustum;
            frustum.extractFromMatrix(viewProjection);
            handler->addTerrainFrustum(frustum, cameraPos);
        }
    }

    void OffScreenController::clearAdditionalTerrainFrustums()
    {
        auto* handler = offScreen ? offScreen->getRenderPassHandler() : nullptr;
        if (handler)
            handler->clearAdditionalTerrainFrustums();
    }

    void OffScreenController::addWaterFrustum(const glm::mat4& viewProjection, const glm::vec3& cameraPos)
    {
        auto* handler = offScreen ? offScreen->getRenderPassHandler() : nullptr;
        if (handler)
        {
            math::Frustum frustum;
            frustum.extractFromMatrix(viewProjection);
            handler->addWaterFrustum(frustum, cameraPos);
        }
    }

    void OffScreenController::clearAdditionalWaterFrustums()
    {
        auto* handler = offScreen ? offScreen->getRenderPassHandler() : nullptr;
        if (handler)
            handler->clearAdditionalWaterFrustums();
    }
}
