#include "OffScreenController.hpp"
#include "../core/VulkanContext.hpp"
#include "../core/SwapChain.hpp"
#include "../render/OffScreenViewPort.hpp"
#include "../render/RenderPassHandler.hpp"
#include "../render/gpudriven/GPUDrivenRenderer.hpp"
#include "../render/shadow/ShadowSystem.hpp"
#include "../render/postprocess/PostProcessPipeline.hpp"
#include "../render/volumetric/VolumetricFogComposite.hpp"
#include "../render/gi/RadianceCascadeManager.hpp"
#include "../render/gi/SSGIPipeline.hpp"
#include "atmosphere/AtmosphereSettings.hpp"
#include "cloud/CloudSettings.hpp"
#include "../render/gi/GIDebugRenderer.hpp"
#include "offscreen/CullingStatsCollector.hpp"
#include "scene/EntityRegistry.hpp"
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
        if (!renderHandler)
            return;

        auto* gpuDriven = renderHandler->getGPUDrivenRenderer();
        if (!gpuDriven)
            return;

        auto* shadowSystem = gpuDriven->getShadowSystem();
        if (shadowSystem)
        {
            shadowSystem->applyRenderSettings(settings);
        }

        auto* lightBufferManager = gpuDriven->getLightBufferManager();
        if (lightBufferManager)
        {
            lightBufferManager->setShadowIntensity(settings.shadows.shadowIntensity);
        }

        gpuDriven->setFrustumCullingEnabled(settings.culling.frustumCullingEnabled);
        gpuDriven->setOcclusionCullingEnabled(settings.culling.occlusionCullingEnabled);
        gpuDriven->setLODSelectionEnabled(settings.culling.lodSelectionEnabled);
        gpuDriven->setMeshletFrustumCullingEnabled(settings.culling.meshletFrustumCullingEnabled);
        gpuDriven->setMeshletBackfaceCullingEnabled(settings.culling.meshletBackfaceCullingEnabled);
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
        if (!renderHandler)
            return stats;

        auto* gpuDriven = renderHandler->getGPUDrivenRenderer();
        if (!gpuDriven)
            return stats;

        auto* shadowSystem = gpuDriven->getShadowSystem();
        if (!shadowSystem)
            return stats;

        stats.atlasWidth = render::shadow::vsm::PHYSICAL_POOL_DIM;
        stats.atlasHeight = render::shadow::vsm::PHYSICAL_POOL_DIM;
        stats.atlasUtilization = shadowSystem->getPoolUtilization();

        stats.activeShadowCasters = shadowSystem->getActiveShadowCasterCount();
        stats.activeShadowViews = shadowSystem->getActiveShadowViewCount();

        stats.directionalLightCount = static_cast<uint32_t>(shadowSystem->getDirectionalShadowViews().size());
        stats.pointLightCount = static_cast<uint32_t>(shadowSystem->getPointShadowViews().size());
        stats.spotLightCount = static_cast<uint32_t>(shadowSystem->getSpotShadowViews().size());

        stats.pointResolution = render::shadow::vsm::PAGE_SIZE;

        // Shadow cache stats
        auto cacheStats = shadowSystem->getShadowCacheStats();
        stats.totalStaticLights = cacheStats.totalStaticLights;
        stats.cachedShadowMaps = cacheStats.cachedShadowMaps;
        stats.renderedThisFrame = cacheStats.renderedThisFrame;
        stats.skippedThisFrame = cacheStats.skippedThisFrame;
        stats.totalPages = cacheStats.totalPages;
        stats.renderedPages = cacheStats.renderedPages;
        stats.cachedPages = cacheStats.cachedPages;

        return stats;
    }

    void OffScreenController::applyPostProcessSettings(const postprocess::PostProcessSettings& settings)
    {
        currentPostProcessSettings = settings;

        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler)
            return;

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
                {
                    renderHandler->initVolumetricFogComposite(gpuRenderer->getVolumetricPipeline());
                }

                auto* composite = renderHandler->getVolumetricFogComposite();
                if (composite)
                {
                    composite->setIntensity(settings.volumetricFog.intensity);
                }
            }
        }

        // Forward TAA enabled state to CameraController for jitter
        if (cameraController)
        {
            cameraController->setTAAEnabled(settings.enabled && settings.taa.enabled);
            auto extent = swapChain.getSwapchainExtent();
            cameraController->setViewportExtent(extent.width, extent.height);
        }

        auto* pipeline = renderHandler->getPostProcessPipeline();
        if (pipeline)
        {
            pipeline->applySettings(settings);
        }
    }

    postprocess::PostProcessSettings OffScreenController::getPostProcessSettings() const
    {
        return currentPostProcessSettings;
    }

    void OffScreenController::setPostProcessEnabled(bool enabled)
    {
        currentPostProcessSettings.enabled = enabled;

        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler)
            return;

        auto* pipeline = renderHandler->getPostProcessPipeline();
        if (pipeline)
        {
            pipeline->applySettings(currentPostProcessSettings);
        }
    }

    bool OffScreenController::isPostProcessEnabled() const
    {
        return currentPostProcessSettings.enabled;
    }

    void OffScreenController::setViewMode(uint32_t mode)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setViewMode(mode);
        }
    }

    uint32_t OffScreenController::getViewMode() const
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            return renderHandler->getViewMode();
        }
        return 0;
    }

    void OffScreenController::setFrustumCullingEnabled(bool enabled)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setFrustumCullingEnabled(enabled);
        }
    }

    void OffScreenController::setLODSelectionEnabled(bool enabled)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setLODSelectionEnabled(enabled);
        }
    }

    void OffScreenController::setMeshletFrustumCullingEnabled(bool enabled)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setMeshletFrustumCullingEnabled(enabled);
        }
    }

    void OffScreenController::setMeshletBackfaceCullingEnabled(bool enabled)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setMeshletBackfaceCullingEnabled(enabled);
        }
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
        {
            renderHandler->setTerrainDrawDistance(distance);
        }
        else if (category == render::gpudriven::ObjectCategory::VFX)
        {
            renderHandler->setVFXDrawDistance(distance);
        }
        else if (category == render::gpudriven::ObjectCategory::Billboard)
        {
            renderHandler->setBillboardDrawDistance(distance);
        }
        else if (category == render::gpudriven::ObjectCategory::Water)
        {
            renderHandler->setWaterDrawDistance(distance);
        }
    }

    void OffScreenController::setShadowDistanceMultiplier(float multiplier)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            auto* gpuDriven = renderHandler->getGPUDrivenRenderer();
            if (gpuDriven) gpuDriven->setShadowDistanceMultiplier(multiplier);
        }
    }

    void OffScreenController::setGlobalLodBias(float bias)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setGlobalLodBias(bias);
        }
    }

    void OffScreenController::setTerrainFrustumCullingEnabled(bool enabled)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setTerrainFrustumCullingEnabled(enabled);
        }
    }

    void OffScreenController::setTerrainMeshletCullingEnabled(bool enabled)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setTerrainMeshletCullingEnabled(enabled);
        }
    }

    void OffScreenController::setWBOITEnabled(bool enabled)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setWBOITEnabled(enabled);
        }
    }

    void OffScreenController::setTerrainRenderingEnabled(bool enabled)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setTerrainRenderingEnabled(enabled);
        }
    }

    void OffScreenController::setBillboardRenderingEnabled(bool enabled)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setBillboardRenderingEnabled(enabled);
        }
    }

    void OffScreenController::setDecalRenderingEnabled(bool enabled)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setDecalRenderingEnabled(enabled);
        }
    }

    void OffScreenController::setDecalDrawList(const std::vector<services::DecalRenderData>& decals)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setDecalDrawList(decals);
        }
    }

    void OffScreenController::setTerrainLODBias(float bias)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setTerrainLODBias(bias);
        }
    }

    void OffScreenController::setTerrainErrorThreshold(float threshold)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setTerrainErrorThreshold(threshold);
        }
    }

    void OffScreenController::setTerrainTextureScale(float scale)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setTerrainTextureScale(scale);
        }
    }

    void OffScreenController::setTerrainShadowLOD(uint32_t lod)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setTerrainShadowLOD(lod);
        }
    }

    void OffScreenController::setVFXRuntimeProvider(services::IVFXRuntimeProvider* provider)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setVFXRuntimeProvider(provider);
        }
    }

    void OffScreenController::setTerrainRenderProvider(services::ITerrainRenderProvider* provider)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setTerrainRenderProvider(provider);
        }
    }

    void OffScreenController::setWaterRenderProvider(services::IWaterRenderProvider* provider)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setWaterRenderProvider(provider);
        }
    }

    void OffScreenController::setGrassRenderProvider(services::IGrassRenderProvider* provider)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setGrassRenderProvider(provider);
        }
    }

    void OffScreenController::setVegetationRenderProvider(services::IVegetationRenderProvider* provider)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setVegetationRenderProvider(provider);
        }
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
        {
            handler->unregisterRenderHook(handle);
        }
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
        {
            handler->clearAdditionalTerrainFrustums();
        }
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
        {
            handler->clearAdditionalWaterFrustums();
        }
    }

    // ── Atmosphere Settings ─────────────────────────────────

    void OffScreenController::applyAtmosphereSettings(const render::atmosphere::AtmosphereSettings& settings)
    {
        currentAtmosphereSettings = settings;

        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        renderHandler->applyAtmosphereSettings(settings);
    }

    render::atmosphere::AtmosphereSettings OffScreenController::getAtmosphereSettings() const
    {
        return currentAtmosphereSettings;
    }

    // ── Cloud Settings ──────────────────────────────────────

    void OffScreenController::applyCloudSettings(const render::cloud::CloudSettings& settings)
    {
        currentCloudSettings = settings;

        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        renderHandler->applyCloudSettings(settings);
    }

    render::cloud::CloudSettings OffScreenController::getCloudSettings() const
    {
        return currentCloudSettings;
    }

    // ── GI Settings ──────────────────────────────────────────

    void OffScreenController::applyGISettings(const render::gi::GISettings& settings)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (gpu)
        {
            gpu->applyGISettings(settings);
        }

        // SSGI pipeline lifecycle: init/reset based on GI+SSGI enabled state
        if (settings.enabled && settings.ssgiEnabled)
        {
            renderHandler->initSSGI();
            if (auto* ssgi = renderHandler->getSSGIPipeline())
            {
                ssgi->updateSettings(settings);
            }
        }
        else
        {
            renderHandler->resetSSGI();
        }
    }

    render::gi::GISettings OffScreenController::getGISettings() const
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return {};

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (gpu)
        {
            return gpu->getGISettings();
        }
        return {};
    }

    render::gi::GIDebugStats OffScreenController::getGIDebugStats() const
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return {};

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (gpu && gpu->getGICascadeManager())
        {
            return gpu->getGICascadeManager()->getDebugStats();
        }
        return {};
    }

    void OffScreenController::setGIShowProbes(bool show)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (gpu && gpu->getGIDebugRenderer())
        {
            gpu->getGIDebugRenderer()->setShowProbes(show);
        }
    }

    void OffScreenController::setGIShowCascadeBounds(bool show)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (gpu && gpu->getGIDebugRenderer())
        {
            gpu->getGIDebugRenderer()->setShowCascadeBounds(show);
        }
    }

    void OffScreenController::setGIShowProbeValidity(bool show)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (gpu && gpu->getGIDebugRenderer())
        {
            gpu->getGIDebugRenderer()->setShowProbeValidity(show);
        }
    }

    // ── Light Streaming Settings ──────────────────────────────

    void OffScreenController::setLightStreamingConfig(const render::lighting::LightStreamingConfig& config)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (gpu && gpu->getLightStreamManager())
        {
            gpu->getLightStreamManager()->setConfig(config);
        }
    }

    render::lighting::LightStreamingConfig OffScreenController::getLightStreamingConfig() const
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return {};

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (gpu && gpu->getLightStreamManager())
        {
            return gpu->getLightStreamManager()->getConfig();
        }
        return {};
    }

    render::lighting::LightStreamingStats OffScreenController::getLightStreamingStats() const
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return {};

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (gpu && gpu->getLightStreamManager())
        {
            return gpu->getLightStreamManager()->getStats();
        }
        return {};
    }

    void OffScreenController::registerSectorLights(uint32_t sectorId, const std::vector<uint32_t>& lightEntityIds)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (gpu && gpu->getLightStreamManager())
        {
            gpu->getLightStreamManager()->registerSectorLights(sectorId, lightEntityIds);
        }
    }

    void OffScreenController::unregisterSectorLights(uint32_t sectorId)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (gpu && gpu->getLightStreamManager())
        {
            gpu->getLightStreamManager()->unregisterSectorLights(sectorId);
        }
    }

    void OffScreenController::setObjectStreamingEnabled(bool enabled)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (gpu)
        {
            gpu->setObjectStreamingEnabled(enabled);
        }
    }

    void OffScreenController::setObjectStreamingConfig(const render::gpudriven::ObjectStreamConfig& config)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (gpu && gpu->getObjectStreamManager())
        {
            gpu->getObjectStreamManager()->setConfig(config);
        }
    }

    render::gpudriven::ObjectStreamConfig OffScreenController::getObjectStreamingConfig() const
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return {};

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (gpu && gpu->getObjectStreamManager())
        {
            return gpu->getObjectStreamManager()->getConfig();
        }
        return {};
    }

    render::gpudriven::ObjectStreamingStats OffScreenController::getObjectStreamingStats() const
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return {};

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (gpu && gpu->getObjectStreamManager())
        {
            return gpu->getObjectStreamManager()->getStats();
        }
        return {};
    }

    void OffScreenController::registerSectorObjects(
        uint32_t sectorId,
        const std::vector<std::pair<uint64_t, entt::entity>>& entities)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (gpu && gpu->getObjectStreamManager())
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            gpu->getObjectStreamManager()->registerSectorObjects(sectorId, entities, registry);
        }
    }

    void OffScreenController::unregisterSectorObjects(uint32_t sectorId)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        auto* gpu = renderHandler->getGPUDrivenRenderer();
        if (gpu && gpu->getObjectStreamManager())
        {
            gpu->getObjectStreamManager()->unregisterSectorObjects(sectorId);
        }
    }

}
