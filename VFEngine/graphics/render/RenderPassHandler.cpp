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
#include "gpudriven/TerrainRaycastPipeline.hpp"
#include "postprocess/PostProcessPipeline.hpp"
#include "volumetric/VolumetricFogComposite.hpp"
#include "transparency/WBOITPipeline.hpp"
#include "volumetric/VolumetricPipeline.hpp"
#include "material/MaterialTextureCache.hpp"
#include "../../services/providers/IVFXRuntimeProvider.hpp"
#include "../../services/providers/ITerrainRenderProvider.hpp"
#include "../../services/providers/IWaterRenderProvider.hpp"
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

        if (wboitPipeline && wboitPipeline->isInitialized())
        {
            wboitPipeline->recreate();
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

        if (postProcessPipeline)
        {
            postProcessPipeline->cleanup();
        }

        meshPipeline->cleanUp();
        iblRenderer->cleanUp();
        clearColor->cleanUp();
    }
}
