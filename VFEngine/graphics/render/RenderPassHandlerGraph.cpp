#include "RenderPassHandler.hpp"
#include "graph/RenderGraph.hpp"
#include "graph/RenderGraphProfiler.hpp"
#include "../core/SwapChain.hpp"
#include "../core/Device.hpp"
#include "postprocess/PostProcessPipeline.hpp"
#include "gi/SSGIPipeline.hpp"
#include "ssr/SSRPipeline.hpp"
#include "volumetric/VolumetricFogComposite.hpp"
#include "atmosphere/AtmospherePipeline.hpp"
#include "atmosphere/SkyEnvironmentCapture.hpp"
#include "ibl/HdrEnvironmentCapture.hpp"
#include "cloud/CloudPipeline.hpp"
#include "ClearColor.hpp"
#include "IBL.hpp"
#include "gpudriven/GPUDrivenRenderer.hpp"
#include "gpudriven/SelectionMaskPipeline.hpp"
#include "selection/SelectionOutlineComposite.hpp"
#include "mesh/StaticMeshPipeline.hpp"
#include "upscaling/UpscaleManager.hpp"
#include "../../services/providers/vfx/IVFXRuntimeProvider.hpp"

namespace render
{
    void RenderPassHandler::importFrameResources(uint32_t imageIndex)
    {
        auto extent = swapChain.getSwapchainExtent();

        graph::ImageResourceDesc colorDesc{};
        colorDesc.extent = extent;
        colorDesc.format = swapChain.getSceneColorFormat();
        colorDesc.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled |
                          vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;
        colorDesc.aspectMask = vk::ImageAspectFlagBits::eColor;
        colorDesc.debugName = "SceneColor";

        // Scene color imported as eUndefined — ClearColor always clears, so previous
        // contents are discarded. This avoids cross-frame layout mismatch since the
        // graph leaves color in eColorAttachmentOptimal after UIOverlays.
        sceneColorHandle = frameGraph->importImage(
            offscreenResources.colorImages[imageIndex].colorImage,
            offscreenResources.colorImages[imageIndex].colorImageView,
            vk::ImageLayout::eUndefined,
            colorDesc);

        graph::ImageResourceDesc depthDesc{};
        depthDesc.extent = extent;
        depthDesc.format = swapChain.getSwapchainDepthStencilFormat();
        depthDesc.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled |
                          vk::ImageUsageFlagBits::eTransferSrc;
        depthDesc.aspectMask = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
        depthDesc.debugName = "Depth";

        // Depth imported as eUndefined — ClearColor always clears, so previous
        // contents are discarded. Avoids cross-frame layout mismatch when
        // depth-reading passes (SSGI, VolumetricFog) leave depth at ReadOnly.
        depthHandle = frameGraph->importImage(
            offscreenResources.depthImage.depthImage,
            offscreenResources.depthImage.depthImageView,
            vk::ImageLayout::eUndefined,
            depthDesc);

        // Scoped MSAA: import the multisampled scene targets the pre-resolve passes
        // (ClearColor → Sky/IBL → Clouds → SceneMeshes) render into. The frame graph
        // auto-inserts the write-after-write barriers between those passes; the
        // SceneMeshes pass resolves them into the single-sample handles above, which
        // every post-resolve pass keeps reading unchanged.
        if (offscreenResources.msaaEnabled())
        {
            graph::ImageResourceDesc msaaColorDesc = colorDesc;
            msaaColorDesc.usage = vk::ImageUsageFlagBits::eColorAttachment;
            msaaColorDesc.debugName = "SceneColorMSAA";
            sceneColorMSAAHandle = frameGraph->importImage(
                offscreenResources.colorImagesMSAA[imageIndex].colorImage,
                offscreenResources.colorImagesMSAA[imageIndex].colorImageView,
                vk::ImageLayout::eUndefined,
                msaaColorDesc);

            graph::ImageResourceDesc msaaDepthDesc = depthDesc;
            msaaDepthDesc.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
            msaaDepthDesc.debugName = "DepthMSAA";
            depthMSAAHandle = frameGraph->importImage(
                offscreenResources.depthImageMSAA.depthImage,
                offscreenResources.depthImageMSAA.depthImageView,
                vk::ImageLayout::eUndefined,
                msaaDepthDesc);
        }
    }

    void RenderPassHandler::buildFrameGraph(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex)
    {
        // =====================================================================
        // All passes use graph-managed barriers (read/write).
        // The render graph's ResourceTracker + BarrierBatcher automatically
        // inserts the correct barriers based on ResourceUsage mappings.
        // Dynamic rendering does NOT change image layouts — the graph has
        // full control over all layout transitions.
        // =====================================================================

        // Scoped MSAA: the pre-resolve passes write the multisampled handles; when
        // MSAA is off these aliases ARE the single-sample handles, so the pass
        // declarations below are unconditional. The SceneMeshes pass resolves into
        // sceneColorHandle/depthHandle, which all post-resolve passes keep using.
        const bool msaa = offscreenResources.msaaEnabled();
        graph::ResourceHandle& colorH = msaa ? sceneColorMSAAHandle : sceneColorHandle;
        graph::ResourceHandle& depthH = msaa ? depthMSAAHandle : depthHandle;

        // Selection visibility is produced by the regular scene fragment shader.
        bool selectionOutlineActive = hasSelectedEntities() &&
                                      gpuDrivenRendererInitialized &&
                                      gpuDrivenRenderer->isEnabled() &&
                                      meshPipelineInitialized &&
                                      gpuDrivenRenderer->ensureSelectionMaskResources();
        if (selectionOutlineActive)
        {
            if (!selectionOutlineComposite)
            {
                selectionOutlineComposite =
                    std::make_unique<selection::SelectionOutlineComposite>(device, swapChain);
                selectionOutlineComposite->init();
            }
            selectionOutlineActive = selectionOutlineComposite->isInitialized();
        }
        if (selectionOutlineActive)
        {
            auto* coverage = gpuDrivenRenderer->getSelectionMaskPipeline();
            selectionOutlineComposite->setMaskInput(coverage->getMaskImageView(),
                                                    coverage->getMaskSampler());

            graph::ImageResourceDesc maskDesc{};
            maskDesc.extent = coverage->getMaskExtent();
            maskDesc.format = gpudriven::SelectionMaskPipeline::getMaskFormat();
            maskDesc.usage = vk::ImageUsageFlagBits::eStorage |
                             vk::ImageUsageFlagBits::eSampled |
                             vk::ImageUsageFlagBits::eTransferDst;
            maskDesc.aspectMask = vk::ImageAspectFlagBits::eColor;
            maskDesc.debugName = "SelectionVisibility";
            // The mask is held permanently in eGeneral (SelectionMaskPipeline transitions
            // it on create and the composite read below returns it to eGeneral each frame),
            // so it is already in eGeneral at import time.
            selectionMaskHandle = frameGraph->importImage(
                coverage->getMaskImage(), coverage->getMaskImageView(),
                vk::ImageLayout::eGeneral, maskDesc);
        }

        // --- Scene core passes ---

        // ClearColor
        {
            auto builder = frameGraph->addPass("ClearColor",
                [this](vk::CommandBuffer cmd, uint32_t idx) {
                    clearColor->recordCommandBufferGraphManaged(cmd, idx);
                });
            colorH = builder.write(colorH, graph::ResourceUsage::ColorAttachmentWrite);
            depthH = builder.write(depthH, graph::ResourceUsage::DepthAttachmentWrite);
            builder.setSegment(graph::HookSegment::Scene);
            builder.setSideEffect();
        }

        // VK-1574: time-sliced non-blocking HDR IBL bake. Mutually exclusive with the VK-1569
        // dynamic-sky capture (gated !dynamicAmbient). Registered before the sky/IBL and mesh passes
        // so publish()'s barriers order the live env + irradiance + prefilter ahead of their
        // consumers (skybox reads envLive; mesh reads irradiance/prefilter). Writes only the
        // persistent capture cubemaps (manages its own barriers) => side-effect pass.
        {
            const bool dynamicAmbient = atmospherePipeline && atmospherePipeline->isEnabled() &&
                                        atmospherePipeline->getSettings().dynamicAmbient;
            if (hdrEnvCapture && hdrEnvCapture->isInitialized() && !dynamicAmbient)
            {
                hdrEnvCapture->pollSource(); // promote a newly-decoded HDR to resident (self-contained upload)
                if (hdrEnvCapture->hasSource())
                {
                    const uint64_t hdrEpoch = hdrEnvCapture->currentEpoch();
                    const int itemsPerFrame = atmospherePipeline ? atmospherePipeline->getSettings().ambientItemsPerFrame : 6;
                    const uint32_t budget = itemsPerFrame > 0 ? static_cast<uint32_t>(itemsPerFrame) : 1u;
                    auto captureBuilder = frameGraph->addPass("HdrEnvCapture",
                        [this, hdrEpoch, budget](vk::CommandBuffer cmd, uint32_t) {
                            hdrEnvCapture->recordCapture(cmd, budget, hdrEpoch);
                        });
                    captureBuilder.setSegment(graph::HookSegment::Scene);
                    captureBuilder.setSideEffect();
                }
            }
        }

        // Atmosphere Sky / IBL
        if (atmospherePipeline && atmospherePipeline->isEnabled())
        {
            atmospherePipeline->setCameraData(currentView, currentProjection,
                                               currentCameraPosition,
                                               currentNearPlane, currentFarPlane,
                                               currentTime);
            if (gpuDrivenRendererInitialized)
            {
                auto* lbm = gpuDrivenRenderer->getLightBufferManager();
                auto sunDir = lbm->getFirstDirectionalLightDirection();
                if (sunDir)
                    atmospherePipeline->setSunDirection(*sunDir);
            }

            auto builder = frameGraph->addPass("Atmosphere",
                [this, asyncCompute = asyncComputeActive](vk::CommandBuffer cmd, uint32_t idx) {
                    if (!asyncCompute)
                        atmospherePipeline->dispatchCompute(cmd);
                    atmospherePipeline->renderSkyGraphManaged(cmd, idx);
                });
            colorH = builder.write(colorH, graph::ResourceUsage::ColorAttachmentWrite);
            builder.setSegment(graph::HookSegment::Scene);
            builder.setSideEffect();

            // VK-1569: time-sliced dynamic sky -> IBL ambient capture, recorded right after the sky
            // LUTs are refreshed. It writes only the persistent capture cubemaps (managing their own
            // barriers) and does not touch scene color, so it stays a side-effect pass.
            if (skyEnvCapture && skyEnvCapture->isInitialized() &&
                atmospherePipeline->getSettings().dynamicAmbient)
            {
                const uint64_t ambientEpoch = computeAmbientCaptureEpoch();
                const int itemsPerFrame = atmospherePipeline->getSettings().ambientItemsPerFrame;
                const uint32_t budget = itemsPerFrame > 0 ? static_cast<uint32_t>(itemsPerFrame) : 1u;
                auto captureBuilder = frameGraph->addPass("SkyAmbientCapture",
                    [this, ambientEpoch, budget](vk::CommandBuffer cmd, uint32_t) {
                        skyEnvCapture->recordCapture(cmd, budget, ambientEpoch);
                    });
                captureBuilder.setSegment(graph::HookSegment::Scene);
                captureBuilder.setSideEffect();
            }
        }
        else
        {
            auto builder = frameGraph->addPass("IBL",
                [this](vk::CommandBuffer cmd, uint32_t idx) {
                    iblRenderer->recordCommandBufferGraphManaged(cmd, idx);
                });
            colorH = builder.write(colorH, graph::ResourceUsage::ColorAttachmentWrite);
            builder.setSegment(graph::HookSegment::Scene);
            builder.setSideEffect();
        }

        // Clouds
        if (cloudPipeline && cloudPipeline->isEnabled())
        {
            cloudPipeline->setCameraData(currentView, currentProjection,
                                          currentCameraPosition,
                                          currentNearPlane, currentFarPlane,
                                          currentTime);
            if (gpuDrivenRendererInitialized)
            {
                auto* lbm = gpuDrivenRenderer->getLightBufferManager();
                auto sunDir = lbm->getFirstDirectionalLightDirection();
                if (sunDir)
                    cloudPipeline->setSunDirection(*sunDir);
            }
            if (atmospherePipeline && atmospherePipeline->isInitialized())
            {
                auto atmosSettings = atmospherePipeline->getSettings();
                cloudPipeline->setSunIrradiance(atmosSettings.sunIrradiance);
            }

            auto builder = frameGraph->addPass("Clouds",
                [this, asyncCompute = asyncComputeActive](vk::CommandBuffer cmd, uint32_t idx) {
                    if (!asyncCompute)
                        cloudPipeline->dispatchCompute(cmd);
                    cloudPipeline->renderCompositeGraphManaged(cmd, idx);
                });
            colorH = builder.write(colorH, graph::ResourceUsage::ColorAttachmentWrite);
            builder.read(depthH, graph::ResourceUsage::DepthAttachmentRead);
            depthH = builder.write(depthH, graph::ResourceUsage::DepthAttachmentRead);
            builder.setSegment(graph::HookSegment::Scene);
            builder.setSideEffect();
        }

        // SceneMeshes — opaque geometry. Under MSAA this renders into the multisampled
        // handles and resolves into the single-sample handles, which the post-resolve
        // passes below consume.
        if (selectionOutlineActive)
        {
            auto builder = frameGraph->addPass("SelectionCoverageClear",
                [this](vk::CommandBuffer cmd, uint32_t) {
                    gpuDrivenRenderer->getSelectionMaskPipeline()->clearVisibility(cmd);
                });
            selectionMaskHandle = builder.write(selectionMaskHandle,
                                                graph::ResourceUsage::TransferDst);
            builder.setSegment(graph::HookSegment::Scene);
            builder.setSideEffect();
        }

        {
            auto builder = frameGraph->addPass("SceneMeshes",
                [this](vk::CommandBuffer cmd, uint32_t idx) {
                    drawSceneMeshesGraphManaged(cmd, idx);
                });
            colorH = builder.write(colorH, graph::ResourceUsage::ColorAttachmentWrite);
            depthH = builder.write(depthH, graph::ResourceUsage::DepthAttachmentWrite);
            if (msaa)
            {
                // Resolve targets — the dynamic-rendering resolve attachments write these.
                sceneColorHandle = builder.write(sceneColorHandle, graph::ResourceUsage::ColorAttachmentWrite);
                depthHandle = builder.write(depthHandle, graph::ResourceUsage::DepthAttachmentWrite);
            }
            if (selectionOutlineActive)
            {
                selectionMaskHandle = builder.write(selectionMaskHandle,
                                                    graph::ResourceUsage::ShaderWrite);
            }
            builder.setSegment(graph::HookSegment::Scene);
            builder.setSideEffect();
        }

        // Hook: PostScene
        if (!renderHooks.empty())
        {
            auto builder = frameGraph->addPass("Hook_PostScene",
                [this](vk::CommandBuffer cmd, uint32_t idx) {
                    executeRenderHooks(plugin::RenderPassHookPoint::PostScene, cmd, idx);
                });
            builder.setSegment(graph::HookSegment::Scene);
            builder.setSideEffect();
        }

        // --- Post-scene passes ---

        // Distortion
        {
            auto builder = frameGraph->addPass("Distortion",
                [this](vk::CommandBuffer cmd, uint32_t idx) {
                    executeDistortionPass(cmd, idx);
                });
            sceneColorHandle = builder.write(sceneColorHandle, graph::ResourceUsage::ColorAttachmentWrite);
            builder.setSegment(graph::HookSegment::PostScene);
            builder.setSideEffect();
        }

        // Overlays (Billboard + Text)
        {
            auto builder = frameGraph->addPass("Overlays",
                [this](vk::CommandBuffer cmd, uint32_t idx) {
                    drawOverlaysGraphManaged(cmd, idx);
                });
            sceneColorHandle = builder.write(sceneColorHandle, graph::ResourceUsage::ColorAttachmentWrite);
            builder.setSegment(graph::HookSegment::PostScene);
            builder.setSideEffect();
        }

        // Occlusion
        {
            auto builder = frameGraph->addPass("OcclusionPasses",
                [this](vk::CommandBuffer cmd, uint32_t /*idx*/) {
                    executeOcclusionPasses(cmd);
                });
            depthHandle = builder.write(depthHandle, graph::ResourceUsage::DepthAttachmentWrite);
            builder.setSegment(graph::HookSegment::PostScene);
            builder.setSideEffect();
        }

        // AtmosphereComposite
        if (atmospherePipeline && atmospherePipeline->isEnabled())
        {
            auto builder = frameGraph->addPass("AtmosphereComposite",
                [this](vk::CommandBuffer cmd, uint32_t idx) {
                    atmospherePipeline->renderCompositeGraphManaged(cmd, idx);
                });
            sceneColorHandle = builder.write(sceneColorHandle, graph::ResourceUsage::ColorAttachmentWrite);
            builder.read(depthHandle, graph::ResourceUsage::DepthAttachmentRead);
            depthHandle = builder.write(depthHandle, graph::ResourceUsage::DepthAttachmentRead);
            builder.setSegment(graph::HookSegment::PostScene);
            builder.setSideEffect();
        }

        // VolumetricFogComposite
        if (volumetricFogComposite && volumetricFogComposite->isInitialized())
        {
            volumetricFogComposite->setCameraData(currentNearPlane, currentFarPlane);

            auto builder = frameGraph->addPass("VolumetricFogComposite",
                [this](vk::CommandBuffer cmd, uint32_t idx) {
                    volumetricFogComposite->executeGraphManaged(cmd, idx);
                });
            sceneColorHandle = builder.write(sceneColorHandle, graph::ResourceUsage::ColorAttachmentWrite);
            builder.read(depthHandle, graph::ResourceUsage::DepthAttachmentRead);
            depthHandle = builder.write(depthHandle, graph::ResourceUsage::DepthAttachmentRead);
            builder.setSegment(graph::HookSegment::PostScene);
            builder.setSideEffect();
        }

        // SSR
        if (ssrPipeline && ssrPipeline->isInitialized())
        {
            ssrPipeline->setCameraData(currentView, currentProjection,
                                        currentCameraPosition,
                                        currentNearPlane, currentFarPlane,
                                        taaFrameIndex);

            auto builder = frameGraph->addPass("SSR",
                [this](vk::CommandBuffer cmd, uint32_t idx) {
                    ssrPipeline->executeGraphManaged(cmd, idx);
                });
            sceneColorHandle = builder.write(sceneColorHandle, graph::ResourceUsage::ColorAttachmentWrite);
            builder.read(depthHandle, graph::ResourceUsage::DepthAttachmentRead);
            builder.setSegment(graph::HookSegment::PostScene);
            builder.setSideEffect();
        }

        // SSGI
        if (ssgiPipeline && ssgiPipeline->isInitialized())
        {
            ssgiPipeline->setCameraData(currentView, currentProjection,
                                         currentCameraPosition,
                                         currentNearPlane, currentFarPlane,
                                         taaFrameIndex);

            auto builder = frameGraph->addPass("SSGI",
                [this](vk::CommandBuffer cmd, uint32_t idx) {
                    ssgiPipeline->executeGraphManaged(cmd, idx);
                });
            sceneColorHandle = builder.write(sceneColorHandle, graph::ResourceUsage::ColorAttachmentWrite);
            builder.read(depthHandle, graph::ResourceUsage::DepthAttachmentRead);
            depthHandle = builder.write(depthHandle, graph::ResourceUsage::DepthAttachmentRead);
            builder.setSegment(graph::HookSegment::PostScene);
            builder.setSideEffect();
        }

        // Hook: PrePostProcess
        if (!renderHooks.empty())
        {
            auto builder = frameGraph->addPass("Hook_PrePostProcess",
                [this](vk::CommandBuffer cmd, uint32_t idx) {
                    executeRenderHooks(plugin::RenderPassHookPoint::PrePostProcess, cmd, idx);
                });
            builder.setSegment(graph::HookSegment::PostScene);
            builder.setSideEffect();
        }

        // --- Depth copy ---

        if (offscreenResources.prevFrameDepthCreated)
        {
            uint32_t currentFrame = imageIndex % core::MAX_FRAMES_IN_FLIGHT;

            auto builder = frameGraph->addPass("DepthCopy",
                [this, currentFrame](vk::CommandBuffer cmd, uint32_t /*idx*/) {
                    vk::ImageSubresourceRange depthStencilRange(
                        vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil, 0, 1, 0, 1);

                    // Pre-copy: transition scene depth to TransferSrc and prevFrameDepth to TransferDst.
                    // Depth is always in AttachmentOptimal here — VFX/WBOIT/Distortion all
                    // restore depth to AttachmentOptimal after their read-only usage.
                    vk::ImageLayout depthCurrentLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

                    {
                        std::array<vk::ImageMemoryBarrier2, 2> preCopyBarriers{};
                        preCopyBarriers[0].srcStageMask = vk::PipelineStageFlagBits2::eLateFragmentTests
                            | vk::PipelineStageFlagBits2::eFragmentShader;
                        preCopyBarriers[0].srcAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite
                            | vk::AccessFlagBits2::eDepthStencilAttachmentRead;
                        preCopyBarriers[0].dstStageMask = vk::PipelineStageFlagBits2::eCopy;
                        preCopyBarriers[0].dstAccessMask = vk::AccessFlagBits2::eTransferRead;
                        preCopyBarriers[0].oldLayout = depthCurrentLayout;
                        preCopyBarriers[0].newLayout = vk::ImageLayout::eTransferSrcOptimal;
                        preCopyBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        preCopyBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        preCopyBarriers[0].image = offscreenResources.depthImage.depthImage;
                        preCopyBarriers[0].subresourceRange = depthStencilRange;

                        preCopyBarriers[1].srcStageMask = vk::PipelineStageFlagBits2::eNone;
                        preCopyBarriers[1].srcAccessMask = vk::AccessFlagBits2::eNone;
                        preCopyBarriers[1].dstStageMask = vk::PipelineStageFlagBits2::eCopy;
                        preCopyBarriers[1].dstAccessMask = vk::AccessFlagBits2::eTransferWrite;
                        preCopyBarriers[1].oldLayout = vk::ImageLayout::eUndefined;
                        preCopyBarriers[1].newLayout = vk::ImageLayout::eTransferDstOptimal;
                        preCopyBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        preCopyBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        preCopyBarriers[1].image = offscreenResources.prevFrameDepth[currentFrame].image;
                        preCopyBarriers[1].subresourceRange = depthStencilRange;

                        vk::DependencyInfo depInfo{};
                        depInfo.imageMemoryBarrierCount = static_cast<uint32_t>(preCopyBarriers.size());
                        depInfo.pImageMemoryBarriers = preCopyBarriers.data();
                        cmd.pipelineBarrier2KHR(depInfo);
                    }

                    vk::ImageCopy region{};
                    region.srcSubresource = {vk::ImageAspectFlagBits::eDepth, 0, 0, 1};
                    region.dstSubresource = {vk::ImageAspectFlagBits::eDepth, 0, 0, 1};
                    region.extent = vk::Extent3D(
                        swapChain.getSwapchainExtent().width,
                        swapChain.getSwapchainExtent().height, 1);
                    cmd.copyImage(
                        offscreenResources.depthImage.depthImage, vk::ImageLayout::eTransferSrcOptimal,
                        offscreenResources.prevFrameDepth[currentFrame].image, vk::ImageLayout::eTransferDstOptimal,
                        region);

                    // Post-copy: restore scene depth to DepthAttachmentOptimal (matches write declaration),
                    // transition prevFrameDepth to ShaderReadOnly for async compute.
                    {
                        std::array<vk::ImageMemoryBarrier2, 2> postCopyBarriers{};
                        postCopyBarriers[0].srcStageMask = vk::PipelineStageFlagBits2::eCopy;
                        postCopyBarriers[0].srcAccessMask = vk::AccessFlagBits2::eTransferRead;
                        postCopyBarriers[0].dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests;
                        postCopyBarriers[0].dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentRead |
                                                            vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
                        postCopyBarriers[0].oldLayout = vk::ImageLayout::eTransferSrcOptimal;
                        postCopyBarriers[0].newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
                        postCopyBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        postCopyBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        postCopyBarriers[0].image = offscreenResources.depthImage.depthImage;
                        postCopyBarriers[0].subresourceRange = depthStencilRange;

                        postCopyBarriers[1].srcStageMask = vk::PipelineStageFlagBits2::eCopy;
                        postCopyBarriers[1].srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
                        postCopyBarriers[1].dstStageMask = vk::PipelineStageFlagBits2::eComputeShader;
                        postCopyBarriers[1].dstAccessMask = vk::AccessFlagBits2::eShaderRead;
                        postCopyBarriers[1].oldLayout = vk::ImageLayout::eTransferDstOptimal;
                        postCopyBarriers[1].newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                        postCopyBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        postCopyBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        postCopyBarriers[1].image = offscreenResources.prevFrameDepth[currentFrame].image;
                        postCopyBarriers[1].subresourceRange = depthStencilRange;

                        vk::DependencyInfo depInfo{};
                        depInfo.imageMemoryBarrierCount = static_cast<uint32_t>(postCopyBarriers.size());
                        depInfo.pImageMemoryBarriers = postCopyBarriers.data();
                        cmd.pipelineBarrier2KHR(depInfo);
                    }
                });
            depthHandle = builder.write(depthHandle, graph::ResourceUsage::DepthAttachmentWrite);
            builder.setSegment(graph::HookSegment::PostScene);
            builder.setSideEffect();
        }

        // --- Post-process / Upscale ---

        {
            auto* upscaleManager = device.getUpscaleManager();
            bool upscalingActive = upscaleManager && upscaleManager->isActive();

            if (upscalingActive)
            {
                auto builder = frameGraph->addPass("Upscale",
                    [this](vk::CommandBuffer cmd, uint32_t idx) {
                        executeUpscaleGraphManaged(cmd, idx);
                        executePostUpscalePostProcess(cmd, idx);
                    });
                builder.read(sceneColorHandle, graph::ResourceUsage::ShaderRead);
                builder.read(depthHandle, graph::ResourceUsage::DepthAttachmentRead);
                sceneColorHandle = builder.write(sceneColorHandle, graph::ResourceUsage::ShaderRead);
                builder.setSegment(graph::HookSegment::PostProcess);
                builder.setSideEffect();
            }
            else
            {
                auto builder = frameGraph->addPass("PostProcess",
                    [this](vk::CommandBuffer cmd, uint32_t idx) {
                        executePostProcess(cmd, idx);
                    });
                builder.read(sceneColorHandle, graph::ResourceUsage::ShaderRead);
                sceneColorHandle = builder.write(sceneColorHandle, graph::ResourceUsage::ShaderRead);
                builder.setSegment(graph::HookSegment::PostProcess);
                builder.setSideEffect();
            }
        }

        // Hook: PostPostProcess
        if (!renderHooks.empty())
        {
            auto builder = frameGraph->addPass("Hook_PostPostProcess",
                [this](vk::CommandBuffer cmd, uint32_t idx) {
                    executeRenderHooks(plugin::RenderPassHookPoint::PostPostProcess, cmd, idx);
                });
            builder.setSegment(graph::HookSegment::PostProcess);
            builder.setSideEffect();
        }

        // VK-1490: editor selection outline, pass 2 — dilate the visible-only
        // mask into an ~2px #FFA100 silhouette after post-processing (so the
        // outline is not tonemapped/blurred) and before the UI overlays.
        if (selectionOutlineActive)
        {
            auto builder = frameGraph->addPass("SelectionOutline",
                [this](vk::CommandBuffer cmd, uint32_t idx) {
                    auto* maskPipeline = gpuDrivenRenderer->getSelectionMaskPipeline();
                    selectionOutlineComposite->executeGraphManaged(cmd,
                        offscreenResources.colorImages[idx].colorImageView,
                        swapChain.getSwapchainExtent(),
                        maskPipeline->getMaskExtent());
                });
            // StorageRead (eGeneral) rather than ShaderRead (eShaderReadOnly): the mask
            // sampler reads fine in eGeneral, and this leaves the image in eGeneral at
            // frame end so the always-bound set-15 descriptor stays layout-correct on
            // the following no-selection / RTT frames.
            builder.read(selectionMaskHandle, graph::ResourceUsage::StorageRead);
            sceneColorHandle = builder.write(sceneColorHandle, graph::ResourceUsage::ColorAttachmentWrite);
            builder.setSegment(graph::HookSegment::UI);
            builder.setSideEffect();
        }

        // UI overlays
        {
            auto builder = frameGraph->addPass("UIOverlays",
                [this](vk::CommandBuffer cmd, uint32_t idx) {
                    drawUIOverlaysGraphManaged(cmd, idx);
                });
            sceneColorHandle = builder.write(sceneColorHandle, graph::ResourceUsage::ColorAttachmentWrite);
            builder.setSegment(graph::HookSegment::UI);
            builder.setSideEffect();
        }

        // Hook: Overlay
        if (!renderHooks.empty())
        {
            auto builder = frameGraph->addPass("Hook_Overlay",
                [this](vk::CommandBuffer cmd, uint32_t idx) {
                    executeRenderHooks(plugin::RenderPassHookPoint::Overlay, cmd, idx);
                });
            builder.setSegment(graph::HookSegment::Overlay);
            builder.setSideEffect();
        }
    }
}
