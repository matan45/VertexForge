#include "RenderPassHandler.hpp"
#include "gpudriven/GPUDrivenRenderer.hpp"
#include "vegetation/VegetationTypes.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/VegetationComponents.hpp"
#include "../core/Device.hpp"
#include "../core/SwapChain.hpp"
#include "mesh/StaticMeshPipeline.hpp"
#include "billboard/BillboardPipeline.hpp"
#include "text/TextPipeline.hpp"
#include "ui/UIRenderPipeline.hpp"
#include "ui/UITextPipeline.hpp"
#include "DebugRenderer.hpp"
#include "gpudriven/GPUDrivenRenderer.hpp"
#include "gpudriven/terrain/TerrainRaycastPipeline.hpp"
#include "postprocess/PostProcessPipeline.hpp"
#include "volumetric/VolumetricFogComposite.hpp"
#include "gi/SSGIPipeline.hpp"
#include "transparency/WBOITPipeline.hpp"
#include "decal/DecalPipeline.hpp"
#include "atmosphere/AtmospherePipeline.hpp"
#include "cloud/CloudPipeline.hpp"
#include "occlusion/CameraOcclusionManager.hpp"
#include "IBL.hpp"
#include "material/MaterialTextureCache.hpp"
#include "../../services/providers/terrain/ITerrainRenderProvider.hpp"
#include "gpudriven/terrain/TerrainStreamManager.hpp"
#include "../../services/providers/terrain/IWaterRenderProvider.hpp"
#include "../../services/providers/vegetation/IGrassRenderProvider.hpp"
#include "../../services/providers/vegetation/IVegetationRenderProvider.hpp"
#include "../../services/providers/vfx/IVFXRuntimeProvider.hpp"
#include "terrain/TerrainTile.hpp"
#include "material/MaterialTypes.hpp"

namespace render
{
    void RenderPassHandler::setDeletionQueue(core::DeferredDeletionQueue* queue)
    {
        if (gpuDrivenRenderer && gpuDrivenRendererInitialized) gpuDrivenRenderer->setDeletionQueue(queue);
        if (textPipeline) textPipeline->setDeletionQueue(queue);
        if (uiPipeline) uiPipeline->setDeletionQueue(queue);
        if (uiTextPipeline) uiTextPipeline->setDeletionQueue(queue);
        if (billboardPipeline) billboardPipeline->setDeletionQueue(queue);
    }

    void RenderPassHandler::setVFXRuntimeProvider(services::IVFXRuntimeProvider* provider) { vfxRuntimeProvider = provider; }
    void RenderPassHandler::setVFXDistanceCullingEnabled(bool enabled) { if (vfxRuntimeProvider) vfxRuntimeProvider->setDistanceCullingEnabled(enabled); }
    void RenderPassHandler::setVFXDrawDistance(float distance) { if (vfxRuntimeProvider) vfxRuntimeProvider->setMaxDrawDistance(distance); }
    void RenderPassHandler::setBillboardDistanceCullingEnabled(bool enabled) { if (billboardPipelineInitialized && billboardPipeline) billboardPipeline->setDistanceCullingEnabled(enabled); }
    void RenderPassHandler::setBillboardDrawDistance(float distance) { if (billboardPipelineInitialized && billboardPipeline) billboardPipeline->setMaxDrawDistance(distance); }
    void RenderPassHandler::setWaterDistanceCullingEnabled(bool enabled) { if (waterRenderProvider) waterRenderProvider->setDistanceCullingEnabled(enabled); }
    void RenderPassHandler::setWaterDrawDistance(float distance) { if (waterRenderProvider) waterRenderProvider->setMaxDrawDistance(distance); }
    void RenderPassHandler::setTerrainDistanceCullingEnabled(bool enabled) { if (terrainRenderProvider) terrainRenderProvider->setDistanceCullingEnabled(enabled); }
    void RenderPassHandler::setTerrainDrawDistance(float distance) { if (terrainRenderProvider) terrainRenderProvider->setMaxDrawDistance(distance); }

    void RenderPassHandler::setTerrainRenderProvider(services::ITerrainRenderProvider* provider)
    {
        terrainRenderProvider = provider;
        if (provider && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setTileDataLoader(
                [provider](terrain::TerrainTile& tile, uint8_t lod) -> bool { return provider->ensureTileLODData(tile, lod); });
            gpuDrivenRenderer->setTileRAMEvictor(
                [provider](terrain::TerrainTile& tile) { provider->releaseTileRAMData(tile); });
            gpuDrivenRenderer->setTileAsyncDataLoader(
                [provider](const render::gpudriven::TerrainTileKey& key) -> render::gpudriven::TileLODLoadResult {
                    auto serviceResult = provider->asyncLoadTileLODData(key.coordX, key.coordZ);
                    render::gpudriven::TileLODLoadResult result;
                    result.key = key;
                    result.lodData = std::move(serviceResult.lodData);
                    result.weightMap = std::move(serviceResult.weightMap);
                    result.holeMask = std::move(serviceResult.holeMask);
                    result.hasWeightMap = serviceResult.hasWeightMap;
                    result.hasHoleMask = serviceResult.hasHoleMask;
                    result.success = serviceResult.success;
                    return result;
                });
        }
    }

    void RenderPassHandler::setGrassRenderProvider(services::IGrassRenderProvider* provider)
    {
        grassRenderProvider = provider;
        if (provider && gpuDrivenRenderer)
        {
            auto* renderer = gpuDrivenRenderer.get();
            provider->setAddTileCallback([renderer](int32_t x, int32_t z) { renderer->addVegetationTile(x, z); });
            provider->setRemoveTileCallback([renderer](int32_t x, int32_t z) { renderer->removeVegetationTile(x, z); });
            provider->setMarkDirtyCallback([renderer](int32_t x, int32_t z) { renderer->markVegetationTileDirty(x, z); });
            provider->setOnBillboardPaletteChanged([renderer](const std::vector<::vegetation::BillboardPaletteEntry>& entries, int32_t activeEntry)
            {
                renderer->setBillboardPaletteFromEntries(entries);
                renderer->setActiveBillboardEntry(activeEntry);
            });

            // Set palette loader - reads directly from ECS registry
            renderer->setBillboardPaletteLoader([]() -> std::vector<::vegetation::BillboardPaletteEntry> {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto view = registry.view<components::GrassComponent>();
                for (auto entity : view)
                    return view.get<components::GrassComponent>(entity).billboardPalette;
                return {};
            });
        }
    }

    void RenderPassHandler::setVegetationRenderProvider(services::IVegetationRenderProvider* provider)
    {
        if (provider && gpuDrivenRenderer)
        {
            auto* renderer = gpuDrivenRenderer.get();
            provider->setAddTileCallback([renderer](int32_t x, int32_t z) { renderer->addVegetationTile(x, z); });
            provider->setRemoveTileCallback([renderer](int32_t x, int32_t z) { renderer->removeVegetationTile(x, z); });
            provider->setMarkDirtyCallback([renderer](int32_t x, int32_t z) { renderer->markVegetationTileDirty(x, z); });
        }
    }

    void RenderPassHandler::setWaterRenderProvider(services::IWaterRenderProvider* provider) { waterRenderProvider = provider; }

    void RenderPassHandler::clearTerrainData() { if (gpuDrivenRenderer) gpuDrivenRenderer->clearTerrainData(); }
    void RenderPassHandler::evictTerrainTile(int32_t coordX, int32_t coordZ) { if (gpuDrivenRenderer) gpuDrivenRenderer->evictTerrainTile(coordX, coordZ); }
    void RenderPassHandler::setSelectedTerrainTile(int32_t coordX, int32_t coordZ) { if (gpuDrivenRenderer) gpuDrivenRenderer->setSelectedTerrainTile(coordX, coordZ); }
    void RenderPassHandler::clearSelectedTerrainTile() { if (gpuDrivenRenderer) gpuDrivenRenderer->clearSelectedTerrainTile(); }
    void RenderPassHandler::addTerrainFrustum(const math::Frustum& frustum, const glm::vec3& cameraPos) { additionalTerrainFrustums.emplace_back(frustum, cameraPos); }
    void RenderPassHandler::clearAdditionalTerrainFrustums() { additionalTerrainFrustums.clear(); }
    void RenderPassHandler::clearWaterData() { if (gpuDrivenRenderer) gpuDrivenRenderer->clearWaterData(); }
    void RenderPassHandler::setSelectedWaterTile(int32_t coordX, int32_t coordZ) { if (gpuDrivenRenderer) gpuDrivenRenderer->setSelectedWaterTile(coordX, coordZ); }
    void RenderPassHandler::clearSelectedWaterTile() { if (gpuDrivenRenderer) gpuDrivenRenderer->clearSelectedWaterTile(); }
    void RenderPassHandler::addWaterFrustum(const math::Frustum& frustum, const glm::vec3& cameraPos) { additionalWaterFrustums.emplace_back(frustum, cameraPos); }
    void RenderPassHandler::clearAdditionalWaterFrustums() { additionalWaterFrustums.clear(); }

    void RenderPassHandler::reinitMeshPipelineWithDefaults()
    {
        if (!meshPipelineInitialized) return;
        device.getLogicalDevice().waitIdle();
        meshPipeline->cleanUpForReinit();
        meshPipeline->initWithDefaults();

        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
            gpuDrivenRenderer->updateRenderPass(meshPipeline->getRenderPass(), meshPipeline->getIBLDescriptorSetLayout());
        if (vfxRuntimeProvider && vfxRuntimeProvider->isInitialized())
            vfxRuntimeProvider->recreate(meshPipeline->getRenderPass());
    }

    void RenderPassHandler::reinitMeshPipelineWithIBL()
    {
        if (!meshPipelineInitialized || !iblRenderer->isInitialized()) return;

        device.getLogicalDevice().waitIdle();
        meshPipeline->cleanUpForReinit();

        const auto& irradiance = iblRenderer->getIrradianceImage();
        const auto& prefilter = iblRenderer->getPrefilterImage();
        const auto& brdfLUT = iblRenderer->getBrdfLUTImage();
        meshPipeline->init(irradiance, prefilter, brdfLUT);

        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
            gpuDrivenRenderer->updateRenderPass(meshPipeline->getRenderPass(), meshPipeline->getIBLDescriptorSetLayout());
        if (vfxRuntimeProvider && vfxRuntimeProvider->isInitialized())
            vfxRuntimeProvider->recreate(meshPipeline->getRenderPass());
    }

    void RenderPassHandler::resetVolumetricFogComposite() { if (volumetricFogComposite) { volumetricFogComposite->cleanup(); volumetricFogComposite.reset(); } }
    void RenderPassHandler::initVolumetricFogComposite(volumetric::VolumetricPipeline* volPipeline)
    {
        if (volumetricFogComposite && volumetricFogComposite->isInitialized()) return;
        if (!volumetricFogComposite)
            volumetricFogComposite = std::make_unique<volumetric::VolumetricFogComposite>(device, swapChain, offscreenResources);
        volumetricFogComposite->init(volPipeline);
    }
    void RenderPassHandler::initSSGI() { if (ssgiPipeline && ssgiPipeline->isInitialized()) return; if (!ssgiPipeline) ssgiPipeline = std::make_unique<gi::SSGIPipeline>(device, swapChain, offscreenResources); ssgiPipeline->init(); }
    void RenderPassHandler::resetSSGI() { if (ssgiPipeline) { ssgiPipeline->cleanup(); ssgiPipeline.reset(); } }

    void RenderPassHandler::initAtmosphere() { if (atmospherePipeline && atmospherePipeline->isInitialized()) return; if (!atmospherePipeline) atmospherePipeline = std::make_unique<atmosphere::AtmospherePipeline>(device, swapChain, offscreenResources); atmospherePipeline->init(); }
    void RenderPassHandler::resetAtmosphere() { if (atmospherePipeline) { atmospherePipeline->cleanup(); atmospherePipeline.reset(); } }
    void RenderPassHandler::applyAtmosphereSettings(const atmosphere::AtmosphereSettings& settings) { if (settings.enabled) initAtmosphere(); if (atmospherePipeline) atmospherePipeline->updateSettings(settings); }

    void RenderPassHandler::initCloud()
    {
        if (cloudPipeline && cloudPipeline->isInitialized()) return;
        if (!cloudPipeline) cloudPipeline = std::make_unique<cloud::CloudPipeline>(device, swapChain, offscreenResources);
        if (atmospherePipeline) cloudPipeline->setAtmospherePipeline(atmospherePipeline.get());
        cloudPipeline->init();
    }
    void RenderPassHandler::resetCloud() { if (cloudPipeline) { cloudPipeline->cleanup(); cloudPipeline.reset(); } }
    void RenderPassHandler::applyCloudSettings(const cloud::CloudSettings& settings) { if (settings.enabled) initCloud(); if (cloudPipeline) cloudPipeline->updateSettings(settings); }
}
