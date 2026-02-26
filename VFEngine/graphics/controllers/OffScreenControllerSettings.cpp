#include "OffScreenController.hpp"
#include "../core/VulkanContext.hpp"
#include "../render/OffScreenViewPort.hpp"
#include "../render/RenderPassHandler.hpp"
#include "../render/gpudriven/GPUDrivenRenderer.hpp"
#include "../render/shadow/ShadowSystem.hpp"
#include "../render/postprocess/PostProcessPipeline.hpp"
#include "../render/volumetric/VolumetricFogComposite.hpp"
#include "offscreen/CullingStatsCollector.hpp"
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

        gpuDriven->setGlobalLodBias(settings.culling.globalLodBias);
        gpuDriven->setDistanceCullingEnabled(settings.distanceCulling.enabled);
        gpuDriven->setCategoryDistance(0, settings.distanceCulling.staticMeshDistance);
        gpuDriven->setCategoryDistance(1, settings.distanceCulling.terrainDistance);
        gpuDriven->setCategoryDistance(2, settings.distanceCulling.foliageDistance);
        gpuDriven->setCategoryDistance(3, settings.distanceCulling.vfxDistance);
        gpuDriven->setCategoryDistance(4, settings.distanceCulling.decalDistance);
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

        auto* atlasManager = shadowSystem->getAtlasManager();
        if (atlasManager)
        {
            stats.atlasWidth = atlasManager->getAtlasWidth();
            stats.atlasHeight = atlasManager->getAtlasHeight();
            stats.atlasUtilization = atlasManager->getAtlasUtilization();
        }

        stats.activeShadowCasters = shadowSystem->getActiveShadowCasterCount();
        stats.activeShadowViews = shadowSystem->getActiveShadowViewCount();

        stats.directionalLightCount = static_cast<uint32_t>(shadowSystem->getDirectionalShadowViews().size());
        stats.pointLightCount = static_cast<uint32_t>(shadowSystem->getPointShadowViews().size());
        stats.spotLightCount = static_cast<uint32_t>(shadowSystem->getSpotShadowViews().size());

        auto quality = static_cast<types::ShadowQuality>(shadowSystem->getGlobalQuality());
        auto atlasConfig = types::ShadowAtlasConfig::fromQuality(quality);
        stats.pointResolution = atlasConfig.pointResolution;

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

        if (category == 1) // ObjectCategory::Terrain
        {
            renderHandler->setTerrainDrawDistance(distance);
        }
        else if (category == 3) // ObjectCategory::VFX
        {
            renderHandler->setVFXDrawDistance(distance);
        }
        else if (category == 5) // ObjectCategory::Billboard
        {
            renderHandler->setBillboardDrawDistance(distance);
        }
        else if (category == 6) // ObjectCategory::Water
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

    render::RenderPassHandler* OffScreenController::getRenderPassHandler() const
    {
        return offScreen ? offScreen->getRenderPassHandler() : nullptr;
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
}
