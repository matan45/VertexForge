#include "RenderPassHandler.hpp"
#include "graph/RenderGraph.hpp"
#include "graph/RenderGraphProfiler.hpp"
#include "upscaling/MotionVectorPass.hpp"
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
#include "material/MaterialTypes.hpp"
#include "vfx/distortion/DistortionResources.hpp"
#include "vfx/distortion/VFXDistortionComposite.hpp"

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
        , frameGraph{std::make_unique<graph::RenderGraph>(device)}
        , graphProfiler{std::make_unique<graph::RenderGraphProfiler>()}
    {
    }

    RenderPassHandler::~RenderPassHandler()
    {
        if (materialChangeCallbackId)
            material::MaterialManager::instance().unregisterChangeCallback(materialChangeCallbackId);
    }

    void RenderPassHandler::init()
    {
        sharedCameraUBO = std::make_unique<common::SharedCameraUBO>(device);
        sharedCameraUBO->init();

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
                        std::erase_if(customShaderRequirementCache, [](const auto& pair) {
                            return material::isInstanceFile(pair.first);
                        });
                    }
                });
        }
    }

    void RenderPassHandler::initMeshPipeline(bool enableGPUDriven)
    {
        if (meshPipelineInitialized) return;

        if (sharedCameraUBO)
        {
            meshPipeline->setExternalCameraBuffer(sharedCameraUBO->getBuffer());
        }

        if (iblRenderer->isInitialized())
        {
            meshPipeline->init(iblRenderer->getIrradianceImage(), iblRenderer->getPrefilterImage(), iblRenderer->getBrdfLUTImage());
        }
        else
        {
            meshPipeline->initWithDefaults();
        }
        meshPipelineInitialized = true;

        if (enableGPUDriven) initGPUDrivenRenderer();

        if (vfxRuntimeProvider && !vfxRuntimeProvider->isInitialized())
        {
            if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
            {
                auto* lbm = gpuDrivenRenderer->getLightBufferManager();
                auto* cgm = gpuDrivenRenderer->getClusterGridManager();
                auto* lcp = gpuDrivenRenderer->getLightCullingPipeline();
                if (lbm && cgm && lcp)
                {
                    vfxRuntimeProvider->setLightingLayouts(lbm->getDescriptorSetLayout(), cgm->getDescriptorSetLayout(), lcp->getDescriptorSetLayout());
                    vfxLightingInitialized = true;
                }
            }
            vfxRuntimeProvider->init(swapChain.getSceneColorFormat(), swapChain.getSwapchainDepthStencilFormat());
        }
    }

    void RenderPassHandler::initGPUDrivenRenderer()
    {
        if (gpuDrivenRendererInitialized || !meshPipelineInitialized) return;

        gpuDrivenRenderer->init(meshPipeline->getIBLDescriptorSetLayout(),
                               {swapChain.getSceneColorFormat()}, swapChain.getSwapchainDepthStencilFormat(),
                               offscreenResources.depthImage.depthImageView);

        auto& texCache = meshPipeline->getMaterialTextureCache();
        gpuDrivenRenderer->setMaterialTextureCache(&texCache);

        if (texCache.hasDefaultTexture())
            gpuDrivenRenderer->setDefaultTexture(texCache.getDefaultView(), texCache.getDefaultSampler());

        gpuDrivenRenderer->setEnabled(true);
        gpuDrivenRendererInitialized = true;

        wboitPipeline = std::make_unique<transparency::WBOITPipeline>(device, swapChain, offscreenResources);
        wboitPipeline->init();
        gpuDrivenRenderer->initWBOITPipeline(
            {vk::Format::eR16G16B16A16Sfloat, vk::Format::eR8Unorm},
            swapChain.getSwapchainDepthStencilFormat());

        decalPipeline = std::make_unique<decal::DecalPipeline>(device, swapChain, offscreenResources);
        decalPipeline->init();
    }

    void RenderPassHandler::recreateOverlayPipelines()
    {
        if (debugRendererInitialized) debugRenderer->recreate(swapChain.getSceneColorFormat(), swapChain.getSwapchainDepthStencilFormat());
        if (billboardPipelineInitialized) billboardPipeline->recreate();
        if (textPipelineInitialized) textPipeline->recreate();
        if (uiPipelineInitialized) uiPipeline->recreate();
        if (uiTextPipelineInitialized) uiTextPipeline->recreate();
    }

    void RenderPassHandler::recreate()
    {
        if (meshPipelineInitialized)
        {
            meshPipeline->recreate();
            if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
            {
                gpuDrivenRenderer->recreateRefractionResources(offscreenResources.depthImage.depthImageView);
                gpuDrivenRenderer->updateFormats({swapChain.getSceneColorFormat()}, swapChain.getSwapchainDepthStencilFormat());
            }
            if (vfxRuntimeProvider && vfxRuntimeProvider->isInitialized())
                vfxRuntimeProvider->recreate(swapChain.getSceneColorFormat(), swapChain.getSwapchainDepthStencilFormat());
        }

        recreateOverlayPipelines();

        for (const auto& [cameraId, camera] : cameraOcclusionManager->getAllCameras())
        {
            if (camera->hiZInitialized)
                cameraOcclusionManager->recreateCameraHiZ(cameraId, offscreenResources.depthImage.depthImage,
                    offscreenResources.depthImage.depthImageView, swapChain.getSwapchainDepthStencilFormat());
        }

        if (terrainRaycastPipeline && terrainRaycastPipeline->isInitialized())
            terrainRaycastPipeline->updateDepthImageView(offscreenResources.depthImage.depthImageView);
        if (volumetricFogComposite && volumetricFogComposite->isInitialized()) volumetricFogComposite->recreate();
        if (ssgiPipeline && ssgiPipeline->isInitialized()) ssgiPipeline->recreate();
        if (atmospherePipeline && atmospherePipeline->isInitialized()) atmospherePipeline->recreate();
        if (cloudPipeline && cloudPipeline->isInitialized()) cloudPipeline->recreate();
        if (wboitPipeline && wboitPipeline->isInitialized()) wboitPipeline->recreate();
        if (decalPipeline && decalPipeline->isInitialized()) decalPipeline->recreate();
        if (distortionInitialized)
        {
            distortionResources->recreate(
                swapChain.getSceneColorFormat(),
                swapChain.getSwapchainDepthStencilFormat(),
                swapChain.getSwapchainExtent(),
                offscreenResources.depthImage.depthImageView,
                offscreenResources);
            distortionComposite->recreate(swapChain.getSceneColorFormat(),
                                           distortionResources->getCompositeDescriptorSetLayout());
            if (vfxRuntimeProvider && vfxRuntimeProvider->isInitialized())
                vfxRuntimeProvider->recreateDistortion(vk::Format::eR16G16Sfloat, swapChain.getSwapchainDepthStencilFormat());
        }
        if (postProcessPipeline && postProcessPipeline->isInitialized()) postProcessPipeline->recreate();
    }

    void RenderPassHandler::cleanUpPipelines() const
    {
        if (billboardPipelineInitialized) billboardPipeline->cleanUp();
        if (textPipelineInitialized) textPipeline->cleanUp();
        if (uiPipelineInitialized) uiPipeline->cleanUp();
        if (uiTextPipelineInitialized) uiTextPipeline->cleanUp();
        if (debugRendererInitialized) { debugRenderer->cleanUp(); debugRenderer->cleanUpShaders(); }
        if (meshPipelineInitialized) meshPipeline->cleanUpShader();
    }

    void RenderPassHandler::cleanUp()
    {
        if (oceanFFTInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->cleanupOceanFFT();
            oceanFFTInitialized = false;
        }
        if (terrainRaycastPipeline) terrainRaycastPipeline->cleanup();
        if (wboitPipeline) wboitPipeline->cleanup();
        if (decalPipeline) decalPipeline->cleanup();
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer) gpuDrivenRenderer->cleanup();
        if (cameraOcclusionManager) cameraOcclusionManager->cleanup();
        cleanUpPipelines();
        if (volumetricFogComposite) volumetricFogComposite->cleanup();
        if (ssgiPipeline) ssgiPipeline->cleanup();
        if (atmospherePipeline) atmospherePipeline->cleanup();
        if (cloudPipeline) cloudPipeline->cleanup();
        if (distortionInitialized)
        {
            distortionComposite->cleanup();
            distortionResources->cleanup();
            distortionInitialized = false;
        }
        if (graphProfiler && graphProfiler->isEnabled())
            graphProfiler->cleanup(device.getLogicalDevice());
        if (postProcessPipeline) postProcessPipeline->cleanup();
        meshPipeline->cleanUp();
        if (sharedCameraUBO) sharedCameraUBO->cleanup();
        iblRenderer->cleanUp();
        clearColor->cleanUp();
    }

    void RenderPassHandler::initDistortionPass()
    {
        if (distortionInitialized) return;

        distortionResources = std::make_unique<vfx::DistortionResources>(device);
        distortionResources->init(
            swapChain.getSceneColorFormat(),
            swapChain.getSwapchainDepthStencilFormat(),
            swapChain.getSwapchainExtent(),
            offscreenResources.depthImage.depthImageView,
            offscreenResources);

        distortionComposite = std::make_unique<vfx::VFXDistortionComposite>(device, swapChain);
        distortionComposite->init(swapChain.getSceneColorFormat(),
                                   distortionResources->getCompositeDescriptorSetLayout());

        if (vfxRuntimeProvider && vfxRuntimeProvider->isInitialized())
            vfxRuntimeProvider->initDistortion(vk::Format::eR16G16Sfloat, swapChain.getSwapchainDepthStencilFormat());

        distortionInitialized = true;
    }
}
