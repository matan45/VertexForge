#include "RenderPassHandler.hpp"
#include "../core/Device.hpp"
#include "../core/SwapChain.hpp"
#include "ClearColor.hpp"
#include "IBL.hpp"
#include "DebugRenderer.hpp"
#include "mesh/StaticMeshPipeline.hpp"
#include "mesh/MeshTypes.hpp"
#include "billboard/BillboardPipeline.hpp"
#include "text/TextPipeline.hpp"
#include "ui/UIRenderPipeline.hpp"
#include "ui/UITextPipeline.hpp"
#include "occlusion/CameraOcclusionManager.hpp"
#include "gpudriven/GPUDrivenRenderer.hpp"
#include "gpudriven/terrain/TerrainRaycastPipeline.hpp"
#include "postprocess/PostProcessPipeline.hpp"
#include "volumetric/VolumetricFogComposite.hpp"
#include "gi/SSGIPipeline.hpp"
#include "transparency/WBOITPipeline.hpp"
#include "decal/DecalPipeline.hpp"
#include "volumetric/VolumetricPipeline.hpp"
#include "atmosphere/AtmospherePipeline.hpp"
#include "cloud/CloudPipeline.hpp"
#include "material/MaterialTextureCache.hpp"
#include "../../services/providers/vfx/IVFXRuntimeProvider.hpp"
#include "../../services/providers/terrain/ITerrainRenderProvider.hpp"
#include "../../services/providers/terrain/IWaterRenderProvider.hpp"
#include "../../services/providers/vegetation/IGrassRenderProvider.hpp"
#include "../../services/providers/vegetation/IVegetationRenderProvider.hpp"
#include "terrain/TerrainTile.hpp"
#include "material/MaterialTypes.hpp"

namespace render
{
    RenderPassHandler::RenderPassHandler(core::Device& device, core::SwapChain& swapChain,
                                         core::OffscreenResources& offscreenResources) : device{device},
        swapChain{swapChain}, offscreenResources{offscreenResources}
        , clearColor{std::make_unique<ClearColor>(device, swapChain, offscreenResources)}
        , iblRenderer{std::make_unique<IBL>(device, swapChain, offscreenResources)}
        , meshPipeline{std::make_unique<mesh::StaticMeshPipeline>(device, swapChain, offscreenResources)}
        , billboardPipeline{std::make_unique<billboard::BillboardPipeline>(device, swapChain, offscreenResources)}
        , textPipeline{std::make_unique<text::TextPipeline>(device, swapChain, offscreenResources)}
        , uiPipeline{std::make_unique<ui::UIRenderPipeline>(device, swapChain, offscreenResources)}
        , uiTextPipeline{std::make_unique<ui::UITextPipeline>(device, swapChain, offscreenResources, textPipeline->getFontCache())}
        , cameraOcclusionManager{std::make_unique<occlusion::CameraOcclusionManager>(device, swapChain)}
        , debugRenderer{std::make_unique<DebugRenderer>(device, swapChain)}
        , gpuDrivenRenderer{std::make_unique<gpudriven::GPUDrivenRenderer>(device, swapChain)}
        , terrainRaycastPipeline{std::make_unique<gpudriven::TerrainRaycastPipeline>(device)}
        , postProcessPipeline{std::make_unique<postprocess::PostProcessPipeline>(device, swapChain, offscreenResources)}
    {
    }

    RenderPassHandler::~RenderPassHandler()
    {
        if (materialChangeCallbackId)
        {
            material::MaterialManager::instance().unregisterChangeCallback(materialChangeCallbackId);
        }

        if (oceanFFTInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->cleanupOceanFFT();
            oceanFFTInitialized = false;
        }
    }

    void RenderPassHandler::init()
    {
        clearColor->init();

        if (terrainRaycastPipeline)
        {
            terrainRaycastPipeline->init();
            terrainRaycastPipeline->updateDepthImageView(offscreenResources.depthImage.depthImageView);
        }

        if (!materialChangeCallbackId)
        {
            materialChangeCallbackId = material::MaterialManager::instance().registerChangeCallback(
                [this](const std::string& materialPath)
                {
                    customShaderRequirementCache.erase(materialPath);

                    if (!material::isInstanceFile(materialPath))
                    {
                        std::erase_if(customShaderRequirementCache, [](const auto& pair)
                        {
                            return material::isInstanceFile(pair.first);
                        });
                    }
                });
        }
    }

    void RenderPassHandler::initMeshPipeline(bool enableGPUDriven)
    {
        if (meshPipelineInitialized)
        {
            return;
        }

        if (iblRenderer->isInitialized())
        {
            const auto& irradiance = iblRenderer->getIrradianceImage();
            const auto& prefilter = iblRenderer->getPrefilterImage();
            const auto& brdfLUT = iblRenderer->getBrdfLUTImage();
            meshPipeline->init(irradiance, prefilter, brdfLUT);
        }
        else
        {
            meshPipeline->initWithDefaults();
        }
        meshPipelineInitialized = true;

        if (enableGPUDriven)
        {
            initGPUDrivenRenderer();
        }

        if (vfxRuntimeProvider && !vfxRuntimeProvider->isInitialized())
        {
            vfxRuntimeProvider->init(meshPipeline->getRenderPass());
        }
    }

    void RenderPassHandler::initGPUDrivenRenderer()
    {
        if (gpuDrivenRendererInitialized)
        {
            return;
        }

        if (!meshPipelineInitialized)
        {
            return;
        }

        vk::DescriptorSetLayout iblLayout = meshPipeline->getIBLDescriptorSetLayout();
        vk::RenderPass renderPass = meshPipeline->getRenderPass();

        gpuDrivenRenderer->init(iblLayout, renderPass);

        auto& texCache = meshPipeline->getMaterialTextureCache();
        gpuDrivenRenderer->setMaterialTextureCache(&texCache);

        if (texCache.hasDefaultTexture())
        {
            gpuDrivenRenderer->setDefaultTexture(texCache.getDefaultView(), texCache.getDefaultSampler());
        }

        gpuDrivenRenderer->setEnabled(true);
        gpuDrivenRendererInitialized = true;

        wboitPipeline = std::make_unique<transparency::WBOITPipeline>(device, swapChain, offscreenResources);
        wboitPipeline->init();
        gpuDrivenRenderer->initWBOITPipeline(wboitPipeline->getWBOITRenderPass());

        decalPipeline = std::make_unique<decal::DecalPipeline>(device, swapChain, offscreenResources);
        decalPipeline->init();
    }

    void RenderPassHandler::resetVolumetricFogComposite()
    {
        if (volumetricFogComposite)
        {
            volumetricFogComposite->cleanup();
            volumetricFogComposite.reset();
        }
    }

    void RenderPassHandler::initVolumetricFogComposite(volumetric::VolumetricPipeline* volPipeline)
    {
        if (volumetricFogComposite && volumetricFogComposite->isInitialized())
            return;

        if (!volumetricFogComposite)
        {
            volumetricFogComposite = std::make_unique<volumetric::VolumetricFogComposite>(
                device, swapChain, offscreenResources);
        }

        volumetricFogComposite->init(volPipeline);
    }

    void RenderPassHandler::initSSGI()
    {
        if (ssgiPipeline && ssgiPipeline->isInitialized())
            return;

        if (!ssgiPipeline)
        {
            ssgiPipeline = std::make_unique<gi::SSGIPipeline>(
                device, swapChain, offscreenResources);
        }

        ssgiPipeline->init();
    }

    void RenderPassHandler::resetSSGI()
    {
        if (ssgiPipeline)
        {
            ssgiPipeline->cleanup();
            ssgiPipeline.reset();
        }
    }

    void RenderPassHandler::initAtmosphere()
    {
        if (atmospherePipeline && atmospherePipeline->isInitialized())
            return;

        if (!atmospherePipeline)
        {
            atmospherePipeline = std::make_unique<atmosphere::AtmospherePipeline>(
                device, swapChain, offscreenResources);
        }

        atmospherePipeline->init();
    }

    void RenderPassHandler::resetAtmosphere()
    {
        if (atmospherePipeline)
        {
            atmospherePipeline->cleanup();
            atmospherePipeline.reset();
        }
    }

    void RenderPassHandler::applyAtmosphereSettings(const atmosphere::AtmosphereSettings& settings)
    {
        if (settings.enabled)
        {
            initAtmosphere();
        }

        if (atmospherePipeline)
        {
            atmospherePipeline->updateSettings(settings);
        }
    }

    void RenderPassHandler::initCloud()
    {
        if (cloudPipeline && cloudPipeline->isInitialized())
            return;

        if (!cloudPipeline)
        {
            cloudPipeline = std::make_unique<cloud::CloudPipeline>(
                device, swapChain, offscreenResources);
        }

        // Wire atmosphere pipeline for transmittance LUT access
        if (atmospherePipeline)
        {
            cloudPipeline->setAtmospherePipeline(atmospherePipeline.get());
        }

        cloudPipeline->init();
    }

    void RenderPassHandler::resetCloud()
    {
        if (cloudPipeline)
        {
            cloudPipeline->cleanup();
            cloudPipeline.reset();
        }
    }

    void RenderPassHandler::applyCloudSettings(const cloud::CloudSettings& settings)
    {
        if (settings.enabled)
        {
            initCloud();
        }

        if (cloudPipeline)
        {
            cloudPipeline->updateSettings(settings);
        }
    }

    void RenderPassHandler::reinitMeshPipelineWithDefaults()
    {
        if (!meshPipelineInitialized)
        {
            return;
        }

        device.getLogicalDevice().waitIdle();

        meshPipeline->cleanUpForReinit();

        meshPipeline->initWithDefaults();

        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->updateRenderPass(
                meshPipeline->getRenderPass(),
                meshPipeline->getIBLDescriptorSetLayout());
        }

        if (vfxRuntimeProvider && vfxRuntimeProvider->isInitialized())
        {
            vfxRuntimeProvider->recreate(meshPipeline->getRenderPass());
        }
    }

    void RenderPassHandler::reinitMeshPipelineWithIBL()
    {
        if (!meshPipelineInitialized)
        {
            return;
        }

        if (!iblRenderer->isInitialized())
        {
            return;
        }

        device.getLogicalDevice().waitIdle();

        meshPipeline->cleanUpForReinit();

        const auto& irradiance = iblRenderer->getIrradianceImage();
        const auto& prefilter = iblRenderer->getPrefilterImage();
        const auto& brdfLUT = iblRenderer->getBrdfLUTImage();
        meshPipeline->init(irradiance, prefilter, brdfLUT);

        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->updateRenderPass(
                meshPipeline->getRenderPass(),
                meshPipeline->getIBLDescriptorSetLayout());
        }

        if (vfxRuntimeProvider && vfxRuntimeProvider->isInitialized())
        {
            vfxRuntimeProvider->recreate(meshPipeline->getRenderPass());
        }
    }

    void RenderPassHandler::setDeletionQueue(core::DeferredDeletionQueue* queue)
    {
        if (gpuDrivenRenderer && gpuDrivenRendererInitialized)
        {
            gpuDrivenRenderer->setDeletionQueue(queue);
        }

        if (textPipeline)
        {
            textPipeline->setDeletionQueue(queue);
        }
        if (uiPipeline)
        {
            uiPipeline->setDeletionQueue(queue);
        }
        if (uiTextPipeline)
        {
            uiTextPipeline->setDeletionQueue(queue);
        }
        if (billboardPipeline)
        {
            billboardPipeline->setDeletionQueue(queue);
        }
    }

    void RenderPassHandler::setVFXRuntimeProvider(services::IVFXRuntimeProvider* provider)
    {
        vfxRuntimeProvider = provider;
    }

    void RenderPassHandler::setVFXDistanceCullingEnabled(bool enabled)
    {
        if (vfxRuntimeProvider) vfxRuntimeProvider->setDistanceCullingEnabled(enabled);
    }

    void RenderPassHandler::setVFXDrawDistance(float distance)
    {
        if (vfxRuntimeProvider) vfxRuntimeProvider->setMaxDrawDistance(distance);
    }

    void RenderPassHandler::setBillboardDistanceCullingEnabled(bool enabled)
    {
        if (billboardPipelineInitialized && billboardPipeline)
            billboardPipeline->setDistanceCullingEnabled(enabled);
    }

    void RenderPassHandler::setBillboardDrawDistance(float distance)
    {
        if (billboardPipelineInitialized && billboardPipeline)
            billboardPipeline->setMaxDrawDistance(distance);
    }

    void RenderPassHandler::setWaterDistanceCullingEnabled(bool enabled)
    {
        if (waterRenderProvider) waterRenderProvider->setDistanceCullingEnabled(enabled);
    }

    void RenderPassHandler::setWaterDrawDistance(float distance)
    {
        if (waterRenderProvider) waterRenderProvider->setMaxDrawDistance(distance);
    }

    void RenderPassHandler::setTerrainDistanceCullingEnabled(bool enabled)
    {
        if (terrainRenderProvider) terrainRenderProvider->setDistanceCullingEnabled(enabled);
    }

    void RenderPassHandler::setTerrainDrawDistance(float distance)
    {
        if (terrainRenderProvider) terrainRenderProvider->setMaxDrawDistance(distance);
    }

    void RenderPassHandler::setTerrainRenderProvider(services::ITerrainRenderProvider* provider)
    {
        terrainRenderProvider = provider;

        if (provider && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setTileDataLoader(
                [provider](terrain::TerrainTile& tile, uint8_t lod) -> bool {
                    return provider->ensureTileLODData(tile, lod);
                });
            gpuDrivenRenderer->setTileRAMEvictor(
                [provider](terrain::TerrainTile& tile) {
                    provider->releaseTileRAMData(tile);
                });
        }
    }

    void RenderPassHandler::setGrassRenderProvider(services::IGrassRenderProvider* provider)
    {
        grassRenderProvider = provider;
        if (provider && gpuDrivenRenderer)
        {
            auto* renderer = gpuDrivenRenderer.get();
            provider->setAddTileCallback([renderer](int32_t x, int32_t z) {
                renderer->addVegetationTile(x, z);
            });
            provider->setRemoveTileCallback([renderer](int32_t x, int32_t z) {
                renderer->removeVegetationTile(x, z);
            });
            provider->setMarkDirtyCallback([renderer](int32_t x, int32_t z) {
                renderer->markVegetationTileDirty(x, z);
            });
        }
    }

    void RenderPassHandler::setVegetationRenderProvider(services::IVegetationRenderProvider* provider)
    {
        if (provider && gpuDrivenRenderer)
        {
            auto* renderer = gpuDrivenRenderer.get();
            provider->setAddTileCallback([renderer](int32_t x, int32_t z) {
                renderer->addVegetationTile(x, z);
            });
            provider->setRemoveTileCallback([renderer](int32_t x, int32_t z) {
                renderer->removeVegetationTile(x, z);
            });
            provider->setMarkDirtyCallback([renderer](int32_t x, int32_t z) {
                renderer->markVegetationTileDirty(x, z);
            });
        }
    }

    void RenderPassHandler::setWaterRenderProvider(services::IWaterRenderProvider* provider)
    {
        waterRenderProvider = provider;
    }

    void RenderPassHandler::clearTerrainData()
    {
        if (gpuDrivenRenderer)
        {
            gpuDrivenRenderer->clearTerrainData();
        }
    }

    void RenderPassHandler::evictTerrainTile(int32_t coordX, int32_t coordZ)
    {
        if (gpuDrivenRenderer)
        {
            gpuDrivenRenderer->evictTerrainTile(coordX, coordZ);
        }
    }

    void RenderPassHandler::setSelectedTerrainTile(int32_t coordX, int32_t coordZ)
    {
        if (gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setSelectedTerrainTile(coordX, coordZ);
        }
    }

    void RenderPassHandler::clearSelectedTerrainTile()
    {
        if (gpuDrivenRenderer)
        {
            gpuDrivenRenderer->clearSelectedTerrainTile();
        }
    }

    void RenderPassHandler::addTerrainFrustum(const math::Frustum& frustum, const glm::vec3& cameraPos)
    {
        additionalTerrainFrustums.emplace_back(frustum, cameraPos);
    }

    void RenderPassHandler::clearAdditionalTerrainFrustums()
    {
        additionalTerrainFrustums.clear();
    }

    void RenderPassHandler::clearWaterData()
    {
        if (gpuDrivenRenderer)
        {
            gpuDrivenRenderer->clearWaterData();
        }
    }

    void RenderPassHandler::setSelectedWaterTile(int32_t coordX, int32_t coordZ)
    {
        if (gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setSelectedWaterTile(coordX, coordZ);
        }
    }

    void RenderPassHandler::clearSelectedWaterTile()
    {
        if (gpuDrivenRenderer)
        {
            gpuDrivenRenderer->clearSelectedWaterTile();
        }
    }

    void RenderPassHandler::addWaterFrustum(const math::Frustum& frustum, const glm::vec3& cameraPos)
    {
        additionalWaterFrustums.emplace_back(frustum, cameraPos);
    }

    void RenderPassHandler::clearAdditionalWaterFrustums()
    {
        additionalWaterFrustums.clear();
    }

    void RenderPassHandler::recreateOverlayPipelines()
    {
        if (debugRendererInitialized)
        {
            debugRenderer->recreate(meshPipeline->getRenderPass());
        }

        if (billboardPipelineInitialized)
        {
            billboardPipeline->recreate();
        }

        if (textPipelineInitialized)
        {
            textPipeline->recreate();
        }

        if (uiPipelineInitialized)
        {
            uiPipeline->recreate();
        }

        if (uiTextPipelineInitialized)
        {
            uiTextPipeline->recreate();
        }
    }

    void RenderPassHandler::recreate()
    {
        iblRenderer->recreate();
        clearColor->recreate();

        if (meshPipelineInitialized)
        {
            meshPipeline->recreate();

            if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
            {
                gpuDrivenRenderer->updateRenderPass(meshPipeline->getRenderPass());
            }

            if (vfxRuntimeProvider && vfxRuntimeProvider->isInitialized())
            {
                vfxRuntimeProvider->recreate(meshPipeline->getRenderPass());
            }
        }

        recreateOverlayPipelines();

        for (const auto& [cameraId, camera] : cameraOcclusionManager->getAllCameras())
        {
            if (camera->hiZInitialized)
            {
                cameraOcclusionManager->recreateCameraHiZ(
                    cameraId,
                    offscreenResources.depthImage.depthImage,
                    offscreenResources.depthImage.depthImageView,
                    swapChain.getSwapchainDepthStencilFormat());
            }
        }

        if (terrainRaycastPipeline && terrainRaycastPipeline->isInitialized())
        {
            terrainRaycastPipeline->updateDepthImageView(offscreenResources.depthImage.depthImageView);
        }

        if (volumetricFogComposite && volumetricFogComposite->isInitialized())
        {
            volumetricFogComposite->recreate();
        }

        if (ssgiPipeline && ssgiPipeline->isInitialized())
        {
            ssgiPipeline->recreate();
        }

        if (atmospherePipeline && atmospherePipeline->isInitialized())
        {
            atmospherePipeline->recreate();
        }

        if (cloudPipeline && cloudPipeline->isInitialized())
        {
            cloudPipeline->recreate();
        }

        if (wboitPipeline && wboitPipeline->isInitialized())
        {
            wboitPipeline->recreate();
        }

        if (decalPipeline && decalPipeline->isInitialized())
        {
            decalPipeline->recreate();
        }

        if (postProcessPipeline && postProcessPipeline->isInitialized())
        {
            postProcessPipeline->recreate();
        }
    }

    void RenderPassHandler::cleanUpPipelines() const
    {
        if (billboardPipelineInitialized)
        {
            billboardPipeline->cleanUp();
        }

        if (textPipelineInitialized)
        {
            textPipeline->cleanUp();
        }

        if (uiPipelineInitialized)
        {
            uiPipeline->cleanUp();
        }

        if (uiTextPipelineInitialized)
        {
            uiTextPipeline->cleanUp();
        }

        if (debugRendererInitialized)
        {
            debugRenderer->cleanUp();
            debugRenderer->cleanUpShaders();
        }

        if (meshPipelineInitialized)
        {
            meshPipeline->cleanUpShader();
        }
    }

    void RenderPassHandler::cleanUp() const
    {
        if (terrainRaycastPipeline)
        {
            terrainRaycastPipeline->cleanup();
        }

        if (wboitPipeline)
        {
            wboitPipeline->cleanup();
        }

        if (decalPipeline)
        {
            decalPipeline->cleanup();
        }

        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->cleanup();
        }

        if (cameraOcclusionManager)
        {
            cameraOcclusionManager->cleanup();
        }

        cleanUpPipelines();

        if (volumetricFogComposite)
        {
            volumetricFogComposite->cleanup();
        }

        if (ssgiPipeline)
        {
            ssgiPipeline->cleanup();
        }

        if (atmospherePipeline)
        {
            atmospherePipeline->cleanup();
        }

        if (postProcessPipeline)
        {
            postProcessPipeline->cleanup();
        }

        meshPipeline->cleanUp();
        iblRenderer->cleanUp();
        clearColor->cleanUp();
    }
}
