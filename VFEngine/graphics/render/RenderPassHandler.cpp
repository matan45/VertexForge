#include "RenderPassHandler.hpp"
#include "graph/RenderGraph.hpp"
#include "graph/RenderGraphProfiler.hpp"
#include "upscaling/MotionVectorPass.hpp"
#include "upscaling/ReactiveMaskPass.hpp"
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
#include "selection/SelectionOutlineComposite.hpp"
#include "volumetric/VolumetricFogComposite.hpp"
#include "gi/SSGIPipeline.hpp"
#include "ssr/SSRPipeline.hpp"
#include "transparency/WBOITPipeline.hpp"
#include "decal/DecalPipeline.hpp"
#include "custom/CustomPipelineManager.hpp"
#include "custom/PluginTextureManager.hpp"
#include "volumetric/VolumetricPipeline.hpp"
#include "atmosphere/AtmospherePipeline.hpp"
#include "atmosphere/SkyEnvironmentCapture.hpp"
#include "ibl/HdrEnvironmentCapture.hpp"
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
        , customPipelineManager{std::make_unique<custom::CustomPipelineManager>(device, swapChain)}
        , pluginTextureManager{std::make_unique<custom::PluginTextureManager>(device)}
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

        gpuDrivenRenderer->setPluginTextureManager(pluginTextureManager.get());

        gpuDrivenRenderer->setEnabled(true);
        gpuDrivenRendererInitialized = true;

        wboitPipeline = std::make_unique<transparency::WBOITPipeline>(device, swapChain, offscreenResources);
        wboitPipeline->init();
        gpuDrivenRenderer->initWBOITPipeline(
            {vk::Format::eR16G16B16A16Sfloat, vk::Format::eR8Unorm},
            swapChain.getSwapchainDepthStencilFormat());

        decalPipeline = std::make_unique<decal::DecalPipeline>(device, swapChain, offscreenResources);
        decalPipeline->init();

        // Lighting layouts for lit plugin custom pipelines (receiveLighting) —
        // builds any lit pipelines plugins created before the renderer was ready.
        if (customPipelineManager)
        {
            auto* lbm = gpuDrivenRenderer->getLightBufferManager();
            auto* cgm = gpuDrivenRenderer->getClusterGridManager();
            auto* lcp = gpuDrivenRenderer->getLightCullingPipeline();
            auto* shadowSystem = gpuDrivenRenderer->getShadowSystem();
            if (lbm && cgm && lcp && shadowSystem)
            {
                customPipelineManager->setLightingLayouts({
                    meshPipeline->getIBLDescriptorSetLayout(),
                    lbm->getDescriptorSetLayout(),
                    cgm->getDescriptorSetLayout(),
                    lcp->getDescriptorSetLayout(),
                    shadowSystem->getShadowDataLayout(),
                    shadowSystem->getShadowTextureLayout(),
                    // RT shadow mask (set 13) — usually null here; the per-frame
                    // syncCustomPipelineRTShadow picks it up when RT comes online.
                    gpuDrivenRenderer->getActiveRTShadowMaskLayout()});
            }
        }
    }

    void RenderPassHandler::recreateOverlayPipelines()
    {
        if (debugRendererInitialized) debugRenderer->recreate(swapChain.getSceneColorFormat(), swapChain.getSwapchainDepthStencilFormat());
        if (billboardPipelineInitialized) billboardPipeline->recreate();
        if (textPipelineInitialized) textPipeline->recreate();
        if (uiPipelineInitialized) uiPipeline->recreate();
        if (uiTextPipelineInitialized) uiTextPipeline->recreate();
        if (customPipelineManager) customPipelineManager->recreatePipelines();
    }

    plugin::CustomPipelineHandle RenderPassHandler::createCustomPipeline(const plugin::CustomPipelineDesc& desc)
    {
        return customPipelineManager->createPipeline(desc);
    }

    plugin::CustomMeshHandle RenderPassHandler::uploadCustomMesh(plugin::CustomMeshData&& data)
    {
        return customPipelineManager->uploadMesh(std::move(data));
    }

    void RenderPassHandler::enqueueCustomDraw(plugin::CustomDrawItem&& item)
    {
        customPipelineManager->enqueueDraw(std::move(item));
    }

    void RenderPassHandler::destroyCustomPipeline(plugin::CustomPipelineHandle handle)
    {
        customPipelineManager->destroyPipeline(handle);
    }

    plugin::PostProcessEffectHandle RenderPassHandler::registerPostProcessEffect(
        const plugin::PostProcessEffectDesc& desc)
    {
        if (!plugin::validatePostProcessEffectDesc(desc) || !postProcessPipeline)
            return {};
        return postProcessPipeline->addPluginEffect(
            desc.fragmentGlsl, plugin::postProcessOrderToPriority(desc),
            desc.paramsSize, desc.startEnabled, desc.debugName);
    }

    void RenderPassHandler::updatePostProcessEffectParams(plugin::PostProcessEffectHandle handle,
                                                          std::vector<std::byte>&& params)
    {
        if (postProcessPipeline)
            postProcessPipeline->setPluginEffectParams(handle, std::move(params));
    }

    void RenderPassHandler::setPostProcessEffectEnabled(plugin::PostProcessEffectHandle handle, bool enabled)
    {
        if (postProcessPipeline)
            postProcessPipeline->setPluginEffectEnabled(handle, enabled);
    }

    void RenderPassHandler::unregisterPostProcessEffect(plugin::PostProcessEffectHandle handle)
    {
        if (postProcessPipeline)
            postProcessPipeline->removePluginEffect(handle);
    }

    void RenderPassHandler::destroyCustomMesh(plugin::CustomMeshHandle handle)
    {
        customPipelineManager->destroyMesh(handle);
    }

    plugin::PluginTextureHandle RenderPassHandler::createPluginTexture2D(uint32_t width, uint32_t height,
                                                                         plugin::TextureFormat format)
    {
        return pluginTextureManager->createTexture2D(width, height, format);
    }

    void RenderPassHandler::updatePluginTexture2D(plugin::PluginTextureHandle handle,
                                                  std::vector<std::byte>&& data)
    {
        pluginTextureManager->updateTexture2D(handle, std::move(data));
    }

    void RenderPassHandler::destroyPluginTexture2D(plugin::PluginTextureHandle handle)
    {
        // VK-1488: drop any UI external-texture binding before the image is destroyed so
        // no stale descriptor lingers in the UI/billboard bindless tables.
        std::string uiKey = pluginTextureManager->removeUITexture(handle);
        if (!uiKey.empty())
            unregisterExternalTexture(uiKey);
        pluginTextureManager->destroyTexture2D(handle);
    }

    std::string RenderPassHandler::registerPluginUITexture(plugin::PluginTextureHandle handle)
    {
        // Record the handle->key binding; the per-frame repoint in draw() fills the current
        // swapchain image's bindless slot from the texture's (stable) view/sampler.
        return pluginTextureManager->registerUITexture(handle);
    }

    void RenderPassHandler::unregisterPluginUITexture(plugin::PluginTextureHandle handle)
    {
        std::string uiKey = pluginTextureManager->removeUITexture(handle);
        if (!uiKey.empty())
            unregisterExternalTexture(uiKey);
    }

    void RenderPassHandler::bindWorldMask(plugin::PluginTextureHandle handle,
                                          const glm::vec3& worldMin, const glm::vec3& worldMax,
                                          const plugin::WorldMaskParams& params)
    {
        pluginTextureManager->bindWorldMask(handle, worldMin, worldMax, params);
    }

    void RenderPassHandler::unbindWorldMask()
    {
        pluginTextureManager->unbindWorldMask();
    }

    void RenderPassHandler::setWorldMaskParams(const plugin::WorldMaskParams& params)
    {
        pluginTextureManager->setWorldMaskParams(params);
    }

    float RenderPassHandler::sampleWorldMask(float worldX, float worldZ) const
    {
        if (!pluginTextureManager) return 1.0f;
        return pluginTextureManager->sampleWorldMask(worldX, worldZ);
    }

    void RenderPassHandler::setWorldMaskDebugEnabled(bool enabled)
    {
        pluginTextureManager->setDebugMaskEnabled(enabled);
    }

    bool RenderPassHandler::getWorldMaskDebugEnabled() const
    {
        return pluginTextureManager->getDebugMaskEnabled();
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
        if (ssrPipeline && ssrPipeline->isInitialized()) ssrPipeline->recreate();
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
        if (customPipelineManager) customPipelineManager->cleanUp();
        if (pluginTextureManager) pluginTextureManager->cleanUp();
        if (cameraOcclusionManager) cameraOcclusionManager->cleanup();
        cleanUpPipelines();
        if (selectionOutlineComposite) selectionOutlineComposite->cleanup(); // VK-1490
        if (volumetricFogComposite) volumetricFogComposite->cleanup();
        if (ssgiPipeline) ssgiPipeline->cleanup();
        if (ssrPipeline) ssrPipeline->cleanup();
        if (atmospherePipeline) atmospherePipeline->cleanup();
        if (cloudPipeline) cloudPipeline->cleanup();
        if (distortionInitialized)
        {
            distortionComposite->cleanup();
            distortionResources->cleanup();
            distortionInitialized = false;
        }
        // Initialized covers enabled-then-disabled too (query pools exist
        // independently of the enable flag)
        if (graphProfiler && graphProfilerInitialized)
        {
            graphProfiler->cleanup(device.getLogicalDevice());
            graphProfilerInitialized = false;
        }
        // VK-1480: aux VT timestamp pool shares the profiler's lifetime. Guard on the
        // init flag (mirrors graphProfiler above) so a run where the profiler was never
        // enabled never touches the pool's (null) query handles.
        if (vtTimestampPoolInitialized)
        {
            vtTimestampPool.cleanup(device.getLogicalDevice());
            vtTimestampPoolInitialized = false;
        }
        // VK-1529 tier 1: always-on whole-frame pool, so unlike the two above it is
        // initialized on the first frame rather than on an enable request.
        if (frameTimePoolInitialized)
        {
            frameTimePool.cleanup(device.getLogicalDevice());
            frameTimePoolInitialized = false;
            frameTimeSlotWritten.fill(false);
            // Drop the smoothing state too, or a re-init blends timings from before
            // the teardown into the new device's first samples.
            frameGpuEmaSeeded = false;
            emaFrameGpuMs = 0.0f;
            vtEma.clear();
        }
        if (postProcessPipeline) postProcessPipeline->cleanup();
        meshPipeline->cleanUp();
        if (sharedCameraUBO) sharedCameraUBO->cleanup();
        iblRenderer->cleanUp();
        if (skyEnvCapture) skyEnvCapture->cleanup(); // VK-1569
        if (hdrEnvCapture) hdrEnvCapture->cleanup(); // VK-1574
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
