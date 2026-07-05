#include "RenderPassHandler.hpp"
#include "graph/RenderGraph.hpp"
#include "print/Log.hpp"
#include "decal/DecalPipeline.hpp"
#include "../core/SwapChain.hpp"
#include "../core/Device.hpp"
#include "../core/ImageUtilities.hpp"
#include "../core/ThreadCommandPoolManager.hpp"
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
#include "postprocess/PostProcessPipeline.hpp"
#include "volumetric/VolumetricFogComposite.hpp"
#include "gi/SSGIPipeline.hpp"
#include "atmosphere/AtmospherePipeline.hpp"
#include "cloud/CloudPipeline.hpp"
#include "transparency/WBOITPipeline.hpp"
#include "custom/CustomPipelineManager.hpp"
#include "custom/PluginTextureManager.hpp"
#include "lighting/GPULightBufferManager.hpp"
#include "lighting/ClusterGridManager.hpp"
#include "lighting/LightCullingPipeline.hpp"
#include "shadow/ShadowSystem.hpp"
#include "upscaling/UpscaleManager.hpp"
#include "../../services/providers/vfx/IVFXRuntimeProvider.hpp"
#include "../../services/providers/terrain/ITerrainRenderProvider.hpp"
#include "../../services/providers/terrain/IOceanRenderProvider.hpp"
#include "../../services/providers/vegetation/IGrassRenderProvider.hpp"
#include "vfx/distortion/DistortionResources.hpp"
#include "vfx/distortion/VFXDistortionComposite.hpp"
#include "../core/DynamicRenderingHelpers.hpp"
#include "threading/JobSystem.hpp"
#include "stats/FrameDrawStats.hpp"
#include "stats/GpuPassStats.hpp"
#include "graph/RenderGraphProfiler.hpp"
#include <chrono>

namespace render
{
    void RenderPassHandler::draw(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex)
    {
        // Plugin texture CPU->GPU uploads — recorded before the frame graph so the
        // copies land outside any render pass and complete before the scene samples them.
        // Publish the previous frame's draw-call total and reset the accumulator for
        // this frame, before any pass records draws (VK-1368).
        FrameDrawStats::beginFrame();

        if (pluginTextureManager) pluginTextureManager->flushUploads(commandBuffer);

        // Lit plugin custom pipelines: pick up the RT shadow mask layout once the
        // RT shadow pipeline comes online — rebuilds them with RT_SHADOW_ENABLED
        // + set 13. Cheap no-op while the layout is unchanged.
        syncCustomPipelineRTShadow();

        // GPU pass profiling: readback last frame's timestamps before this
        // frame's graph records new ones into the same per-frame pool slot
        syncGraphProfiler(imageIndex);

        frameGraph->reset();
        importFrameResources(imageIndex);
        buildFrameGraph(commandBuffer, imageIndex);
        frameGraph->compile();
        frameGraph->execute(commandBuffer, imageIndex);

        // Plugin custom draws are enqueued per frame — drop them whether or not
        // the scene pass consumed them (e.g. GPU-driven renderer disabled).
        if (customPipelineManager) customPipelineManager->endFrame();
    }

    custom::CustomLightingSets RenderPassHandler::buildCustomLightingSets(vk::DescriptorSet iblDescriptorSet) const
    {
        // Same per-frame sets the terrain pipeline consumes (see
        // GPUDrivenRenderer::renderTerrainDraw); lit custom pipelines are
        // skipped while any set is missing.
        custom::CustomLightingSets sets{};
        if (!gpuDrivenRendererInitialized || !gpuDrivenRenderer) return sets;

        sets.ibl = iblDescriptorSet;
        if (auto* lbm = gpuDrivenRenderer->getLightBufferManager()) sets.lights = lbm->getDescriptorSet();
        if (auto* cgm = gpuDrivenRenderer->getClusterGridManager()) sets.clusterParams = cgm->getDescriptorSet();
        if (auto* lcp = gpuDrivenRenderer->getLightCullingPipeline()) sets.clusterIndices = lcp->getDescriptorSet();

        auto* shadowSystem = gpuDrivenRenderer->getShadowSystem();
        if (shadowSystem && shadowSystem->isInitialized())
        {
            sets.shadowData = shadowSystem->getShadowDataDescSet();
            sets.shadowTextures = shadowSystem->getShadowTextureDescSet();
        }

        // Set 13 — fetched per frame so raw <-> denoised switches and resizes
        // are picked up automatically; null while RT shadows are offline.
        sets.rtShadowMask = gpuDrivenRenderer->getActiveRTShadowMaskDescriptorSet();
        return sets;
    }

    void RenderPassHandler::syncCustomPipelineRTShadow()
    {
        if (!customPipelineManager || !gpuDrivenRendererInitialized || !gpuDrivenRenderer) return;
        customPipelineManager->setRTShadowMaskLayout(gpuDrivenRenderer->getActiveRTShadowMaskLayout());
    }

    void RenderPassHandler::syncGraphProfiler(uint32_t imageIndex)
    {
        if (!graphProfiler || graphProfilerUnsupported) return;

        auto& sink = GpuPassStats::instance();
        bool wanted = sink.isEnabledRequested();

        if (wanted && !graphProfilerInitialized)
        {
            // 2 timestamp queries per pass; 64 is far above the current graph size
            constexpr uint32_t kMaxProfiledPasses = 64;
            if (graphProfiler->init(device, kMaxProfiledPasses))
            {
                frameGraph->setProfiler(graphProfiler.get());
                graphProfilerInitialized = true;
            }
            else
            {
                graphProfilerUnsupported = true;
                sink.markUnsupported();
                return;
            }
        }

        if (!graphProfilerInitialized) return;

        if (graphProfiler->isEnabled() != wanted)
        {
            graphProfiler->setEnabled(wanted);
            if (!wanted) sink.clear();
        }

        if (!wanted) return;

        graphProfiler->readbackAndUpdate(device.getLogicalDevice(), imageIndex);

        auto stats = graphProfiler->getStats();
        GpuFrameStats out;
        out.valid = stats.passCount > 0;
        out.totalMs = stats.totalMs;
        out.emaTotalMs = stats.emaTotalMs;
        out.barrierCount = stats.barrierCount;
        out.barrierFlushCount = stats.barrierFlushCount;
        out.passTimings.reserve(stats.passTimings.size());
        for (const auto& pass : stats.passTimings)
        {
            out.passTimings.push_back({pass.name, pass.ms, pass.emaMs});
        }
        sink.publish(std::move(out));
    }

    void RenderPassHandler::executeDistortionPass(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex)
    {
        if (!vfxRuntimeProvider || !vfxRuntimeProvider->isInitialized() || !vfxRuntimeProvider->hasDistortionEmitters())
            return;

        // Lazy init: create distortion resources on first use
        if (!distortionInitialized)
            initDistortionPass();

        if (!distortionInitialized || !distortionResources || !distortionResources->isInitialized())
            return;

        auto extent = distortionResources->getExtent();

        // 1. Copy scene color before distortion
        distortionResources->copySceneColor(commandBuffer,
            offscreenResources.colorImages[imageIndex].colorImage,
            extent.width, extent.height);

        // 2. Distortion vector pass (clear + render distortion emitters into R16G16 buffer)
        // Transition distortion image: ShaderReadOnly -> ColorAttachmentOptimal
        {
            vk::ImageMemoryBarrier toColorAttach{};
            toColorAttach.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            toColorAttach.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
            toColorAttach.image = distortionResources->getDistortionImage();
            toColorAttach.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
            toColorAttach.srcAccessMask = vk::AccessFlagBits::eShaderRead;
            toColorAttach.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
            commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader,
                                          vk::PipelineStageFlagBits::eColorAttachmentOutput,
                                          {}, {}, {}, toColorAttach);
        }

        // Transition scene depth: AttachmentOptimal -> ReadOnlyOptimal for depth sampling
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.depthImage.depthImage,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            vk::ImageLayout::eDepthStencilReadOnlyOptimal,
            vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil);

        auto colorAttach = core::colorClear(distortionResources->getDistortionView(),
            vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}});
        auto depthAttach = core::depthReadOnly(distortionResources->getSceneDepthView());

        core::DynamicRenderingInfo dynInfo{};
        dynInfo.extent = extent;
        dynInfo.colorAttachments = {colorAttach};
        dynInfo.depthAttachment = depthAttach;

        core::beginDynamicRendering(commandBuffer, dynInfo);

        vk::Viewport viewport{0.0f, 0.0f,
            static_cast<float>(extent.width), static_cast<float>(extent.height),
            0.0f, 1.0f};
        commandBuffer.setViewport(0, viewport);
        vk::Rect2D scissor{{0, 0}, extent};
        commandBuffer.setScissor(0, scissor);

        vfxRuntimeProvider->recordDistortionDrawCommands(commandBuffer);

        core::endDynamicRendering(commandBuffer);

        // Restore scene depth: ReadOnlyOptimal -> AttachmentOptimal
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.depthImage.depthImage,
            vk::ImageLayout::eDepthStencilReadOnlyOptimal,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil);

        // Transition distortion image back: ColorAttachmentOptimal -> ShaderReadOnlyOptimal
        {
            vk::ImageMemoryBarrier toShaderRead{};
            toShaderRead.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
            toShaderRead.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            toShaderRead.image = distortionResources->getDistortionImage();
            toShaderRead.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
            toShaderRead.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
            toShaderRead.dstAccessMask = vk::AccessFlagBits::eShaderRead;
            commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                                          vk::PipelineStageFlagBits::eFragmentShader,
                                          {}, {}, {}, toShaderRead);
        }

        // 3. Composite pass: apply distortion to scene color
        distortionComposite->record(commandBuffer,
            offscreenResources.colorImages[imageIndex].colorImageView,
            extent,
            distortionResources->getCompositeDescriptorSet());
    }

    void RenderPassHandler::capturePreTransparencyColor(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        auto* upscaleManager = device.getUpscaleManager();
        if (!upscaleManager || !upscaleManager->isActive()
            || !offscreenResources.upscaleResourcesCreated
            || !offscreenResources.preTransparencyColor.image)
            return;

        auto renderRes = upscaleManager->getResolutionManager().getRenderResolution();
        vk::Image srcColorImage = offscreenResources.colorImages[imageIndex].colorImage;
        vk::Image dstImage = offscreenResources.preTransparencyColor.image;

        // Destination is fully overwritten - discard previous contents
        vk::ImageMemoryBarrier toTransferDst{};
        toTransferDst.oldLayout = vk::ImageLayout::eUndefined;
        toTransferDst.newLayout = vk::ImageLayout::eTransferDstOptimal;
        toTransferDst.image = dstImage;
        toTransferDst.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        toTransferDst.srcAccessMask = vk::AccessFlagBits::eShaderRead;
        toTransferDst.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
        commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                                      vk::PipelineStageFlagBits::eTransfer,
                                      {}, {}, {}, toTransferDst);

        vk::ImageMemoryBarrier srcToTransfer{};
        srcToTransfer.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
        srcToTransfer.newLayout = vk::ImageLayout::eTransferSrcOptimal;
        srcToTransfer.image = srcColorImage;
        srcToTransfer.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        srcToTransfer.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        srcToTransfer.dstAccessMask = vk::AccessFlagBits::eTransferRead;
        commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                                      vk::PipelineStageFlagBits::eTransfer,
                                      {}, {}, {}, srcToTransfer);

        vk::ImageCopy copyRegion{};
        copyRegion.srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
        copyRegion.dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
        copyRegion.extent = vk::Extent3D{renderRes.width, renderRes.height, 1};
        commandBuffer.copyImage(srcColorImage, vk::ImageLayout::eTransferSrcOptimal,
                                dstImage, vk::ImageLayout::eTransferDstOptimal,
                                copyRegion);

        vk::ImageMemoryBarrier dstToRead{};
        dstToRead.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        dstToRead.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        dstToRead.image = dstImage;
        dstToRead.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        dstToRead.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        dstToRead.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                      vk::PipelineStageFlagBits::eComputeShader,
                                      {}, {}, {}, dstToRead);

        // Restore the scene color to the layout the render graph expects
        vk::ImageMemoryBarrier srcBack{};
        srcBack.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
        srcBack.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
        srcBack.image = srcColorImage;
        srcBack.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        srcBack.srcAccessMask = vk::AccessFlagBits::eTransferRead;
        srcBack.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eShaderRead;
        commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                      vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eFragmentShader,
                                      {}, {}, {}, srcBack);

        preTransparencyCaptured = true;
    }

    // ======================== Graph-managed dispatch variants ========================

    void RenderPassHandler::drawOverlaysGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        if (billboardPipelineInitialized && !currentBillboardDrawList.empty())
            billboardPipeline->recordCommandBufferGraphManaged(commandBuffer, imageIndex);

        if (textPipelineInitialized && !currentTextDrawList.empty())
            textPipeline->recordCommandBufferGraphManaged(commandBuffer, imageIndex);
    }

    void RenderPassHandler::drawUIOverlaysGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        // When upscaling is active the UI composites into displayColorImages (display res), which the
        // frame graph does not track — executePostUpscale leaves it in SHADER_READ_ONLY_OPTIMAL. The UI
        // records begin dynamic rendering with colorLoad (expects COLOR_ATTACHMENT_OPTIMAL), so transition
        // it here. Without upscaling the UI targets the render-res colorImages, which the graph already
        // transitions via the UIOverlays pass's ColorAttachmentWrite declaration — leave that path alone.
        const bool hasUIImages = uiPipelineInitialized && !currentUIImageDrawList.empty();
        const bool hasUIText = uiTextPipelineInitialized && !currentUITextDrawList.empty();
        // Only round-trip the display target's layout when we actually record UI into it.
        // With nothing to draw the transition pair is a no-op that would still assert the
        // image is currently in SHADER_READ_ONLY_OPTIMAL — skip it to avoid a spurious
        // barrier and a layout-mismatch if the post-upscale path left it elsewhere.
        const bool hasDisplay = !offscreenResources.displayColorImages.empty() && (hasUIImages || hasUIText);

        if (hasDisplay)
            core::ImageUtilities::transitionImageLayout(commandBuffer,
                offscreenResources.displayColorImages[imageIndex].colorImage,
                vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageAspectFlagBits::eColor);

        if (hasUIImages)
            uiPipeline->recordCommandBufferGraphManaged(commandBuffer, imageIndex);

        if (hasUIText)
            uiTextPipeline->recordCommandBufferGraphManaged(commandBuffer, imageIndex);

        // Overlay layer (tooltips, modal windows) records after ALL main UI
        // images and text so its backgrounds cover underlying labels too.
        if (hasUIImages)
            uiPipeline->recordCommandBufferGraphManaged(commandBuffer, imageIndex, true);

        if (hasUIText)
            uiTextPipeline->recordCommandBufferGraphManaged(commandBuffer, imageIndex, true);

        // Restore to SHADER_READ_ONLY_OPTIMAL so render() can sample displayColorImages for presentation.
        if (hasDisplay)
            core::ImageUtilities::transitionImageLayout(commandBuffer,
                offscreenResources.displayColorImages[imageIndex].colorImage,
                vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageAspectFlagBits::eColor);
    }

    void RenderPassHandler::drawSceneMeshesGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        bool hasDebugItems = debugRendererInitialized && debugRenderer->hasItemsToRender();
        bool hasVFX = vfxRuntimeProvider && vfxRuntimeProvider->isInitialized()
            && vfxRuntimeProvider->getInstanceCount() > 0;
        bool hasCustomShaderMeshes = !customShaderMeshDrawList.empty();
        bool hasPluginDraws = customPipelineManager && customPipelineManager->hasDraws();

        if (hasVFX)
        {
            services::VFXCameraParams vfxCamera;
            vfxCamera.view = currentView;
            vfxCamera.projection = currentProjection;
            vfxCamera.cameraPos = currentCameraPosition;
            vfxCamera.time = currentTime;
            vfxCamera.nearPlane = currentNearPlane;
            vfxCamera.farPlane = currentFarPlane;
            vfxRuntimeProvider->setCamera(vfxCamera);
            vfxRuntimeProvider->setSceneDepthImageView(offscreenResources.depthImage.depthImageView);

            if (vfxLightingInitialized && gpuDrivenRendererInitialized && gpuDrivenRenderer)
            {
                auto* lbm = gpuDrivenRenderer->getLightBufferManager();
                auto* cgm = gpuDrivenRenderer->getClusterGridManager();
                auto* lcp = gpuDrivenRenderer->getLightCullingPipeline();

                if (lbm && cgm && lcp)
                {
                    vfxRuntimeProvider->updateLightingDescriptorSets(
                        lbm->getDescriptorSet(),
                        cgm->getDescriptorSet(),
                        lcp->getDescriptorSet());
                }
            }
        }

        // VFX proxy lights: instances with light emission illuminate the scene
        // as transient point lights (set before dispatchCompute runs
        // updateFromScene; an empty list clears last frame's lights)
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            if (auto* lbm = gpuDrivenRenderer->getLightBufferManager())
            {
                std::vector<lighting::GPULightBufferManager::TransientPointLight> transientLights;
                if (hasVFX)
                {
                    auto proxyLights = vfxRuntimeProvider->getActiveProxyLights();
                    transientLights.reserve(proxyLights.size());
                    for (const auto& proxy : proxyLights)
                        transientLights.push_back({proxy.position, proxy.color, proxy.intensity, proxy.radius});
                }
                lbm->setTransientPointLights(std::move(transientLights));
            }
        }

        bool hasTerrainToRender = gpuDrivenRenderer && gpuDrivenRenderer->isTerrainRenderingEnabled()
            && terrainRenderProvider && terrainRenderProvider->hasActiveTerrain();

        bool hasWaterToRender = gpuDrivenRenderer && gpuDrivenRenderer->isWaterRenderingEnabled()
            && oceanRenderProvider && oceanRenderProvider->hasActiveOcean();

        bool hasBillboardsToRender = gpuDrivenRenderer && gpuDrivenRenderer->isBillboardRenderingEnabled();

        bool needsMeshPass = meshPipelineInitialized && (!currentMeshDrawList.empty() || hasCustomShaderMeshes
            || hasDebugItems || hasVFX || hasTerrainToRender || hasWaterToRender || hasPluginDraws
            || hasBillboardsToRender);

        updateGPUDrivenSceneData();

        if (!needsMeshPass)
        {
            if (decalRenderingEnabled && decalPipeline && decalPipeline->isInitialized() && decalPipeline->hasDecals())
            {
                decalPipeline->setCameraData(currentView, currentProjection, currentNearPlane, currentFarPlane);
                decalPipeline->renderGraphManaged(commandBuffer, imageIndex);
            }
            return;
        }

        DebugRenderer* debugRendererPtr = hasDebugItems ? debugRenderer.get() : nullptr;

        if (hasVFX && !asyncComputeActive)
            vfxRuntimeProvider->recordComputeCommands(commandBuffer);

        bool hasMeshesToRender = !currentMeshDrawList.empty();

        if ((hasMeshesToRender || hasTerrainToRender || hasWaterToRender || hasPluginDraws || hasBillboardsToRender)
            && gpuDrivenRendererInitialized && gpuDrivenRenderer->isEnabled())
        {
            drawGPUDrivenMeshPassGraphManaged(commandBuffer, imageIndex, debugRendererPtr, hasCustomShaderMeshes, hasVFX);
        }
        else if (!currentMeshDrawList.empty() || hasCustomShaderMeshes || hasDebugItems)
        {
            meshPipeline->recordCommandBufferGraphManaged(commandBuffer, imageIndex, combinedMeshDrawList, currentFrustum,
                                              debugRendererPtr, currentView, currentProjection);
            if (hasVFX)
            {
                capturePreTransparencyColor(commandBuffer, imageIndex);
                meshPipeline->beginVFXRenderPassGraphManaged(commandBuffer, imageIndex);
                vfxRuntimeProvider->recordDrawCommands(commandBuffer);
                meshPipeline->endRenderPassGraphManaged(commandBuffer, imageIndex);
                meshPipeline->restoreDepthAfterVFX(commandBuffer);
            }
        }
        else if (hasVFX)
        {
            meshPipeline->beginRenderPassGraphManaged(commandBuffer, imageIndex);
            meshPipeline->endRenderPassGraphManaged(commandBuffer, imageIndex);

            capturePreTransparencyColor(commandBuffer, imageIndex);
            meshPipeline->beginVFXRenderPassGraphManaged(commandBuffer, imageIndex);
            vfxRuntimeProvider->recordDrawCommands(commandBuffer);
            meshPipeline->endRenderPassGraphManaged(commandBuffer, imageIndex);
            meshPipeline->restoreDepthAfterVFX(commandBuffer);
        }
    }

    void RenderPassHandler::drawGPUDrivenMeshPassGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex,
                                                               DebugRenderer* debugRendererPtr, bool hasCustomShaderMeshes,
                                                               bool hasVFX) const
    {
        updateGPUDrivenHiZ();
        if (asyncComputeActive)
            gpuDrivenRenderer->dispatchGraphicsCompute(commandBuffer, imageIndex);
        else
            gpuDrivenRenderer->dispatchCompute(commandBuffer, imageIndex);

        vk::DescriptorSet iblDescriptorSet = meshPipeline->getIBLDescriptorSet(imageIndex);

        if (gpuDrivenRenderer->isMeshletOcclusionCullingEnabled())
        {
            gpuDrivenRenderer->renderDepthPrepass(commandBuffer, iblDescriptorSet);
            gpuDrivenRenderer->generatePrepassHiZ(commandBuffer);
        }

        // Plugin world mask: one-time pipeline recreate on first bind + descriptor upkeep
        gpuDrivenRenderer->dispatchWorldMask();

        if (gpuDrivenRenderer->isRTShadowReady())
            gpuDrivenRenderer->dispatchRTShadow(commandBuffer, imageIndex);

        if (gpuDrivenRenderer->isRTSpotShadowReady())
            gpuDrivenRenderer->dispatchRTSpotShadow(commandBuffer, imageIndex);

        if (gpuDrivenRenderer->isRTPointShadowReady())
            gpuDrivenRenderer->dispatchRTPointShadow(commandBuffer, imageIndex);

        if (oceanFFTInitialized)
        {
            gpuDrivenRenderer->readbackOceanDisplacement();
            gpuDrivenRenderer->dispatchOceanFFT(commandBuffer, currentTime);
        }

        // VK-1209: bake requested terrain RVT pages into the atlas (dynamic rendering, outside the
        // scene pass) before the terrain draw samples it.
        if (gpuDrivenRenderer->isTerrainRVTActive())
            gpuDrivenRenderer->bakeTerrainRVT(commandBuffer);

        // VK-1209: stream + upload requested material SVT pages, and clear feedback, before meshes sample.
        if (gpuDrivenRenderer->isSVTActive())
            gpuDrivenRenderer->updateAndUploadSVT(commandBuffer);

        bool useParallel = parallelSceneRecording && sceneThreadPoolManager &&
                           sceneThreadPoolManager->getThreadCount() > 1;

        bool wboitActive = wboitEnabled && wboitPipeline && wboitPipeline->isInitialized()
                           && gpuDrivenRenderer->isWBOITReady();

        if (useParallel)
            recordParallelScenePassGraphManaged(commandBuffer, imageIndex, iblDescriptorSet,
                                                debugRendererPtr, hasCustomShaderMeshes, wboitActive);
        else
            recordInlineScenePassGraphManaged(commandBuffer, imageIndex, iblDescriptorSet,
                                              debugRendererPtr, hasCustomShaderMeshes, wboitActive);

        // VK-1209: terrain wrote its page requests during the scene pass; copy them to staging for
        // next frame's readback (outside the pass).
        if (gpuDrivenRenderer->isTerrainRVTActive())
            gpuDrivenRenderer->copyTerrainRVTFeedback(commandBuffer);

        // VK-1209: meshes wrote their SVT page requests during the scene pass; copy to staging.
        if (gpuDrivenRenderer->isSVTActive())
            gpuDrivenRenderer->copySVTFeedback(commandBuffer);

        if (decalRenderingEnabled && decalPipeline && decalPipeline->isInitialized() && decalPipeline->hasDecals())
        {
            decalPipeline->setCameraData(currentView, currentProjection, currentNearPlane, currentFarPlane);
            decalPipeline->renderGraphManaged(commandBuffer, imageIndex);
        }

        // Opaque rendering (incl. decals) is complete - snapshot it for the
        // upscaler reactive mask before transparency (WBOIT/VFX) draws
        if ((wboitActive && gpuDrivenRenderer->hasTransparentObjects()) || hasVFX)
            capturePreTransparencyColor(commandBuffer, imageIndex);

        if (wboitActive && gpuDrivenRenderer->hasTransparentObjects())
        {
            wboitPipeline->beginWBOITPass(commandBuffer, imageIndex);
            gpuDrivenRenderer->renderWBOITDraw(commandBuffer, iblDescriptorSet);
            wboitPipeline->endWBOITPass(commandBuffer);
            wboitPipeline->composite(commandBuffer, imageIndex);
        }

        if (hasVFX)
        {
            meshPipeline->beginVFXRenderPassGraphManaged(commandBuffer, imageIndex);
            vfxRuntimeProvider->recordDrawCommands(commandBuffer);
            meshPipeline->endRenderPassGraphManaged(commandBuffer, imageIndex);
            meshPipeline->restoreDepthAfterVFX(commandBuffer);
        }
    }

    void RenderPassHandler::recordParallelScenePassGraphManaged(
        const vk::CommandBuffer& commandBuffer, uint32_t imageIndex,
        vk::DescriptorSet iblDescriptorSet, DebugRenderer* debugRendererPtr,
        bool hasCustomShaderMeshes, bool wboitActive) const
    {
        auto sceneRecordStart = std::chrono::high_resolution_clock::now();
        sceneThreadPoolManager->resetFrame(imageIndex);

        auto extent = swapChain.getSwapchainExtent();
        vk::Format colorFormat = swapChain.getSceneColorFormat();
        vk::Format depthFormat = swapChain.getSwapchainDepthStencilFormat();

        const vk::SampleCountFlagBits sampleCount = offscreenResources.sampleCount;

        auto setupSecondary = [&](vk::CommandBuffer sec) {
            vk::CommandBufferInheritanceRenderingInfo inheritRendering{};
            inheritRendering.colorAttachmentCount = 1;
            inheritRendering.pColorAttachmentFormats = &colorFormat;
            inheritRendering.depthAttachmentFormat = depthFormat;
            inheritRendering.rasterizationSamples = sampleCount;

            vk::CommandBufferInheritanceInfo inheritance{};
            inheritance.pNext = &inheritRendering;

            vk::CommandBufferBeginInfo beginInfo{};
            beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit |
                              vk::CommandBufferUsageFlagBits::eRenderPassContinue;
            beginInfo.pInheritanceInfo = &inheritance;
            sec.begin(beginInfo);

            vk::Viewport viewport{0.0f, 0.0f,
                static_cast<float>(extent.width), static_cast<float>(extent.height),
                0.0f, 1.0f};
            sec.setViewport(0, viewport);
            vk::Rect2D scissor{{0, 0}, extent};
            sec.setScissor(0, scissor);
            // Dynamic MSAA: this opaque secondary renders into the (multisampled) scene targets.
            sec.setRasterizationSamplesEXT(sampleCount);
        };

        bool hasTerrain = gpuDrivenRenderer->isTerrainRenderingEnabled();
        bool hasGrass = gpuDrivenRenderer->isGrassRenderingEnabled();
        bool hasWater = gpuDrivenRenderer->isWaterRenderingEnabled();
        bool hasBillboards = gpuDrivenRenderer->isBillboardRenderingEnabled();

        vk::CommandBuffer meshCmd{nullptr};
        vk::CommandBuffer terrainCmd{nullptr};
        vk::CommandBuffer grassCmd{nullptr};
        vk::CommandBuffer waterCmd{nullptr};
        vk::CommandBuffer overlayCmd{nullptr};

        auto meshFuture = threading::JobSystem::instance().submit([&]() {
            meshCmd = sceneThreadPoolManager->getSecondary(0, imageIndex);
            setupSecondary(meshCmd);
            gpuDrivenRenderer->renderDraw(meshCmd, iblDescriptorSet);
            if (!wboitActive) gpuDrivenRenderer->renderTransparentDraw(meshCmd, iblDescriptorSet);
            gpuDrivenRenderer->renderBlendDraw(meshCmd, iblDescriptorSet);
            meshCmd.end();
        }, threading::JobPriority::HIGH);

        std::future<void> terrainFuture;
        if (hasTerrain)
        {
            terrainFuture = threading::JobSystem::instance().submit([&]() {
                terrainCmd = sceneThreadPoolManager->getSecondary(1, imageIndex);
                setupSecondary(terrainCmd);
                gpuDrivenRenderer->renderTerrainDraw(terrainCmd, iblDescriptorSet);
                terrainCmd.end();
            }, threading::JobPriority::HIGH);
        }

        std::future<void> grassFuture;
        if (hasGrass)
        {
            grassFuture = threading::JobSystem::instance().submit([&]() {
                grassCmd = sceneThreadPoolManager->getSecondary(2, imageIndex);
                setupSecondary(grassCmd);
                gpuDrivenRenderer->renderGrassDraw(grassCmd);
                grassCmd.end();
            }, threading::JobPriority::HIGH);
        }

        std::future<void> waterFuture;
        if (!hasWater && hasBillboards)
        {
            waterFuture = threading::JobSystem::instance().submit([&]() {
                waterCmd = sceneThreadPoolManager->getSecondary(3, imageIndex);
                setupSecondary(waterCmd);
                gpuDrivenRenderer->renderBillboardDraw(waterCmd, iblDescriptorSet, currentTime);
                waterCmd.end();
            }, threading::JobPriority::HIGH);
        }

        meshFuture.get();
        if (terrainFuture.valid()) terrainFuture.get();
        if (grassFuture.valid()) grassFuture.get();
        if (waterFuture.valid()) waterFuture.get();

        if (!hasWater)
        {
            overlayCmd = sceneThreadPoolManager->getSecondary(4, imageIndex);
            setupSecondary(overlayCmd);
            if (hasCustomShaderMeshes)
                meshPipeline->renderMeshList(overlayCmd, imageIndex, customShaderMeshDrawList, currentFrustum);
            gpuDrivenRenderer->renderGIDebug(overlayCmd, currentProjection * currentView);
            if (debugRendererPtr)
            {
                debugRendererPtr->render(overlayCmd, combinedMeshDrawList, currentView, currentProjection,
                    [this](const std::string& meshId) { return meshPipeline->getMesh(meshId); });
            }
            // Plugin custom pipelines draw last in the scene pass, just before
            // post-processing.
            if (customPipelineManager)
                customPipelineManager->render(overlayCmd, currentView, currentProjection,
                                              buildCustomLightingSets(iblDescriptorSet));
            overlayCmd.end();
        }

        meshPipeline->beginRenderPassForSecondaryGraphManaged(commandBuffer, imageIndex);

        std::vector<vk::CommandBuffer> secondaries;
        secondaries.reserve(5);
        secondaries.push_back(meshCmd);
        if (terrainCmd) secondaries.push_back(terrainCmd);
        if (grassCmd) secondaries.push_back(grassCmd);
        if (!hasWater && waterCmd) secondaries.push_back(waterCmd);
        if (!hasWater && overlayCmd) secondaries.push_back(overlayCmd);

        commandBuffer.executeCommands(
            static_cast<uint32_t>(secondaries.size()),
            secondaries.data()
        );

        if (hasWater)
        {
            meshPipeline->endRenderPassGraphManaged(commandBuffer, imageIndex);

            auto extent = swapChain.getSwapchainExtent();
            gpuDrivenRenderer->copySceneColorForRefraction(
                commandBuffer,
                offscreenResources.colorImages[imageIndex].colorImage,
                extent.width, extent.height);

            meshPipeline->beginWaterContinuePassGraphManaged(commandBuffer, imageIndex);

            vk::Viewport viewport{0.0f, 0.0f,
                                   static_cast<float>(extent.width),
                                   static_cast<float>(extent.height),
                                   0.0f, 1.0f};
            commandBuffer.setViewport(0, viewport);
            vk::Rect2D scissor{{0, 0}, extent};
            commandBuffer.setScissor(0, scissor);

            gpuDrivenRenderer->renderWaterDraw(commandBuffer, iblDescriptorSet);

            if (hasBillboards)
                gpuDrivenRenderer->renderBillboardDraw(commandBuffer, iblDescriptorSet, currentTime);

            if (hasCustomShaderMeshes)
                meshPipeline->renderMeshList(commandBuffer, imageIndex, customShaderMeshDrawList, currentFrustum);

            gpuDrivenRenderer->renderGIDebug(commandBuffer, currentProjection * currentView);

            if (debugRendererPtr)
            {
                debugRendererPtr->render(commandBuffer, combinedMeshDrawList, currentView, currentProjection,
                    [this](const std::string& meshId) { return meshPipeline->getMesh(meshId); });
            }

            // Plugin custom pipelines draw last in the scene pass, just before
            // post-processing.
            if (customPipelineManager)
                customPipelineManager->render(commandBuffer, currentView, currentProjection,
                                              buildCustomLightingSets(iblDescriptorSet));
        }

        auto sceneRecordEnd = std::chrono::high_resolution_clock::now();
        lastSceneRecordingUs = std::chrono::duration<float, std::micro>(sceneRecordEnd - sceneRecordStart).count();
        lastSceneSecondaryCount = static_cast<uint32_t>(secondaries.size());

        meshPipeline->endRenderPassGraphManaged(commandBuffer, imageIndex);
    }

    void RenderPassHandler::recordInlineScenePassGraphManaged(
        const vk::CommandBuffer& commandBuffer, uint32_t imageIndex,
        vk::DescriptorSet iblDescriptorSet, DebugRenderer* debugRendererPtr,
        bool hasCustomShaderMeshes, bool wboitActive) const
    {
        meshPipeline->beginRenderPassGraphManaged(commandBuffer, imageIndex);

        auto extent = swapChain.getSwapchainExtent();
        vk::Viewport viewport{0.0f, 0.0f,
                               static_cast<float>(extent.width), static_cast<float>(extent.height),
                               0.0f, 1.0f};
        commandBuffer.setViewport(0, viewport);
        vk::Rect2D scissor{{0, 0}, extent};
        commandBuffer.setScissor(0, scissor);

        gpuDrivenRenderer->renderDraw(commandBuffer, iblDescriptorSet);

        if (!wboitActive)
            gpuDrivenRenderer->renderTransparentDraw(commandBuffer, iblDescriptorSet);

        gpuDrivenRenderer->renderBlendDraw(commandBuffer, iblDescriptorSet);

        if (gpuDrivenRenderer->isTerrainRenderingEnabled())
            gpuDrivenRenderer->renderTerrainDraw(commandBuffer, iblDescriptorSet);

        if (gpuDrivenRenderer->isGrassRenderingEnabled())
            gpuDrivenRenderer->renderGrassDraw(commandBuffer);

        bool hasWater = gpuDrivenRenderer->isWaterRenderingEnabled()
            && oceanRenderProvider && oceanRenderProvider->hasActiveOcean();

        if (hasWater)
        {
            meshPipeline->endRenderPassGraphManaged(commandBuffer, imageIndex);

            gpuDrivenRenderer->copySceneColorForRefraction(
                commandBuffer,
                offscreenResources.colorImages[imageIndex].colorImage,
                extent.width, extent.height);

            meshPipeline->beginWaterContinuePassGraphManaged(commandBuffer, imageIndex);

            auto waterExtent = swapChain.getSwapchainExtent();
            vk::Viewport waterViewport{0.0f, 0.0f,
                                        static_cast<float>(waterExtent.width),
                                        static_cast<float>(waterExtent.height),
                                        0.0f, 1.0f};
            commandBuffer.setViewport(0, waterViewport);
            vk::Rect2D waterScissor{{0, 0}, waterExtent};
            commandBuffer.setScissor(0, waterScissor);

            gpuDrivenRenderer->renderWaterDraw(commandBuffer, iblDescriptorSet);
        }

        if (gpuDrivenRenderer->isBillboardRenderingEnabled())
            gpuDrivenRenderer->renderBillboardDraw(commandBuffer, iblDescriptorSet, currentTime);

        if (hasCustomShaderMeshes)
            meshPipeline->renderMeshList(commandBuffer, imageIndex, customShaderMeshDrawList, currentFrustum);

        gpuDrivenRenderer->renderGIDebug(commandBuffer, currentProjection * currentView);

        if (debugRendererPtr)
        {
            debugRendererPtr->render(commandBuffer, combinedMeshDrawList, currentView, currentProjection,
                                     [this](const std::string& meshId)
                                     {
                                         return meshPipeline->getMesh(meshId);
                                     });
        }

        // Plugin custom pipelines draw last in the scene pass, just before
        // post-processing.
        if (customPipelineManager)
            customPipelineManager->render(commandBuffer, currentView, currentProjection,
                                          buildCustomLightingSets(iblDescriptorSet));

        meshPipeline->endRenderPassGraphManaged(commandBuffer, imageIndex);
    }
}
