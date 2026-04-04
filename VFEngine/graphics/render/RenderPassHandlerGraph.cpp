#include "RenderPassHandler.hpp"
#include "graph/RenderGraph.hpp"
#include "graph/RenderGraphProfiler.hpp"
#include "../core/SwapChain.hpp"
#include "../core/Device.hpp"
#include "postprocess/PostProcessPipeline.hpp"
#include "gi/SSGIPipeline.hpp"
#include "volumetric/VolumetricFogComposite.hpp"
#include "atmosphere/AtmospherePipeline.hpp"
#include "cloud/CloudPipeline.hpp"
#include "ClearColor.hpp"
#include "IBL.hpp"
#include "gpudriven/GPUDrivenRenderer.hpp"
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

        // Scene color starts in eShaderReadOnlyOptimal (from previous frame's final pass,
        // or from offscreen resource initialization on first frame)
        sceneColorHandle = frameGraph->importImage(
            offscreenResources.colorImages[imageIndex].colorImage,
            offscreenResources.colorImages[imageIndex].colorImageView,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            colorDesc);

        graph::ImageResourceDesc depthDesc{};
        depthDesc.extent = extent;
        depthDesc.format = swapChain.getSwapchainDepthStencilFormat();
        depthDesc.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled |
                          vk::ImageUsageFlagBits::eTransferSrc;
        depthDesc.aspectMask = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
        depthDesc.debugName = "Depth";

        // Depth starts in eDepthStencilAttachmentOptimal (from previous frame or initialization)
        depthHandle = frameGraph->importImage(
            offscreenResources.depthImage.depthImage,
            offscreenResources.depthImage.depthImageView,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            depthDesc);
    }

    void RenderPassHandler::buildFrameGraph(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex)
    {
        // =====================================================================
        // Layout tracking convention:
        //   opaqueWrite/opaqueRead — pass manages its own barriers, graph just
        //                            tracks the final layout for downstream passes.
        //   read/write             — graph inserts barriers (for executeGraphManaged passes).
        //
        // Most passes leave scene color in eShaderReadOnlyOptimal
        // and depth in eDepthStencilAttachmentOptimal.
        // =====================================================================

        // --- Scene core passes (opaque, self-managed barriers) ---

        // ClearColor: opaque, manages own transitions (eUndefined→eColorAttachment→eShaderReadOnly)
        {
            auto builder = frameGraph->addPass("ClearColor",
                [this](vk::CommandBuffer cmd, uint32_t idx) {
                    clearColor->recordCommandBuffer(cmd, idx);
                });
            sceneColorHandle = builder.opaqueWrite(sceneColorHandle, graph::ResourceUsage::ShaderRead);
            depthHandle = builder.opaqueWrite(depthHandle, graph::ResourceUsage::DepthAttachmentWrite);
            builder.setSegment(graph::HookSegment::Scene);
            builder.setSideEffect();
        }

        // Atmosphere Sky: initialLayout=eColorAttachmentOptimal (handled by render pass),
        //                 finalLayout=eShaderReadOnlyOptimal
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
                    atmospherePipeline->renderSky(cmd, idx);
                });
            builder.opaqueRead(sceneColorHandle, graph::ResourceUsage::ShaderRead);
            sceneColorHandle = builder.opaqueWrite(sceneColorHandle, graph::ResourceUsage::ShaderRead);
            builder.setSegment(graph::HookSegment::Scene);
            builder.setSideEffect();
        }
        else
        {
            auto builder = frameGraph->addPass("IBL",
                [this](vk::CommandBuffer cmd, uint32_t idx) {
                    iblRenderer->recordCommandBuffer(cmd, idx);
                });
            builder.opaqueRead(sceneColorHandle, graph::ResourceUsage::ShaderRead);
            sceneColorHandle = builder.opaqueWrite(sceneColorHandle, graph::ResourceUsage::ShaderRead);
            builder.setSegment(graph::HookSegment::Scene);
            builder.setSideEffect();
        }

        // Clouds: initialLayout=eColorAttachmentOptimal, finalLayout=eShaderReadOnlyOptimal
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
                    cloudPipeline->renderComposite(cmd, idx);
                });
            builder.opaqueRead(sceneColorHandle, graph::ResourceUsage::ShaderRead);
            sceneColorHandle = builder.opaqueWrite(sceneColorHandle, graph::ResourceUsage::ShaderRead);
            builder.setSegment(graph::HookSegment::Scene);
            builder.setSideEffect();
        }

        // SceneMeshes: finalLayout color=eShaderReadOnlyOptimal, depth=eDepthStencilAttachmentOptimal
        {
            auto builder = frameGraph->addPass("SceneMeshes",
                [this](vk::CommandBuffer cmd, uint32_t idx) {
                    drawSceneMeshes(cmd, idx);
                });
            builder.opaqueRead(sceneColorHandle, graph::ResourceUsage::ShaderRead);
            sceneColorHandle = builder.opaqueWrite(sceneColorHandle, graph::ResourceUsage::ShaderRead);
            depthHandle = builder.opaqueWrite(depthHandle, graph::ResourceUsage::DepthAttachmentWrite);
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

        // --- Post-scene opaque passes ---

        // Distortion: finalLayout color=eShaderReadOnlyOptimal, depth=eDepthStencilAttachmentOptimal
        {
            auto builder = frameGraph->addPass("Distortion",
                [this](vk::CommandBuffer cmd, uint32_t idx) {
                    executeDistortionPass(cmd, idx);
                });
            builder.opaqueRead(sceneColorHandle, graph::ResourceUsage::ShaderRead);
            sceneColorHandle = builder.opaqueWrite(sceneColorHandle, graph::ResourceUsage::ShaderRead);
            builder.setSegment(graph::HookSegment::PostScene);
            builder.setSideEffect();
        }

        // Overlays: finalLayout color=eShaderReadOnlyOptimal, depth=eDepthStencilAttachmentOptimal
        {
            auto builder = frameGraph->addPass("Overlays",
                [this](vk::CommandBuffer cmd, uint32_t idx) {
                    drawOverlays(cmd, idx);
                });
            builder.opaqueRead(sceneColorHandle, graph::ResourceUsage::ShaderRead);
            sceneColorHandle = builder.opaqueWrite(sceneColorHandle, graph::ResourceUsage::ShaderRead);
            builder.setSegment(graph::HookSegment::PostScene);
            builder.setSideEffect();
        }

        // Occlusion: finalLayout depth=eDepthStencilAttachmentOptimal
        {
            auto builder = frameGraph->addPass("OcclusionPasses",
                [this](vk::CommandBuffer cmd, uint32_t /*idx*/) {
                    executeOcclusionPasses(cmd);
                });
            builder.opaqueRead(depthHandle, graph::ResourceUsage::DepthAttachmentRead);
            depthHandle = builder.opaqueWrite(depthHandle, graph::ResourceUsage::DepthAttachmentWrite);
            builder.setSegment(graph::HookSegment::PostScene);
            builder.setSideEffect();
        }

        // AtmosphereComposite: finalLayout color=eShaderReadOnlyOptimal
        if (atmospherePipeline && atmospherePipeline->isEnabled())
        {
            auto builder = frameGraph->addPass("AtmosphereComposite",
                [this](vk::CommandBuffer cmd, uint32_t idx) {
                    atmospherePipeline->renderComposite(cmd, idx);
                });
            builder.opaqueRead(sceneColorHandle, graph::ResourceUsage::ShaderRead);
            sceneColorHandle = builder.opaqueWrite(sceneColorHandle, graph::ResourceUsage::ShaderRead);
            builder.setSegment(graph::HookSegment::PostScene);
            builder.setSideEffect();
        }

        // --- VolumetricFog, SSGI (opaque — own barriers) ---

        if (volumetricFogComposite && volumetricFogComposite->isInitialized())
        {
            volumetricFogComposite->setCameraData(currentNearPlane, currentFarPlane);

            auto builder = frameGraph->addPass("VolumetricFogComposite",
                [this](vk::CommandBuffer cmd, uint32_t idx) {
                    volumetricFogComposite->execute(cmd, idx);
                });
            builder.opaqueRead(sceneColorHandle, graph::ResourceUsage::ShaderRead);
            builder.opaqueRead(depthHandle, graph::ResourceUsage::DepthAttachmentRead);
            sceneColorHandle = builder.opaqueWrite(sceneColorHandle, graph::ResourceUsage::ShaderRead);
            depthHandle = builder.opaqueWrite(depthHandle, graph::ResourceUsage::DepthAttachmentWrite);
            builder.setSegment(graph::HookSegment::PostScene);
            builder.setSideEffect();
        }

        if (ssgiPipeline && ssgiPipeline->isInitialized())
        {
            ssgiPipeline->setCameraData(currentView, currentProjection,
                                         currentCameraPosition,
                                         currentNearPlane, currentFarPlane,
                                         taaFrameIndex);

            auto builder = frameGraph->addPass("SSGI",
                [this](vk::CommandBuffer cmd, uint32_t idx) {
                    ssgiPipeline->execute(cmd, idx);
                });
            builder.opaqueRead(sceneColorHandle, graph::ResourceUsage::ShaderRead);
            builder.opaqueRead(depthHandle, graph::ResourceUsage::DepthAttachmentRead);
            sceneColorHandle = builder.opaqueWrite(sceneColorHandle, graph::ResourceUsage::ShaderRead);
            depthHandle = builder.opaqueWrite(depthHandle, graph::ResourceUsage::DepthAttachmentWrite);
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

        // --- Depth copy (opaque — uses its own pipelineBarrier2KHR) ---

        if (offscreenResources.prevFrameDepthCreated)
        {
            uint32_t currentFrame = imageIndex % core::MAX_FRAMES_IN_FLIGHT;

            auto builder = frameGraph->addPass("DepthCopy",
                [this, currentFrame](vk::CommandBuffer cmd, uint32_t /*idx*/) {
                    vk::ImageSubresourceRange depthStencilRange(
                        vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil, 0, 1, 0, 1);
                    vk::ImageSubresourceRange depthOnlyRange(
                        vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1);

                    {
                        std::array<vk::ImageMemoryBarrier2, 2> preCopyBarriers{};
                        preCopyBarriers[0].srcStageMask = vk::PipelineStageFlagBits2::eLateFragmentTests;
                        preCopyBarriers[0].srcAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
                        preCopyBarriers[0].dstStageMask = vk::PipelineStageFlagBits2::eCopy;
                        preCopyBarriers[0].dstAccessMask = vk::AccessFlagBits2::eTransferRead;
                        preCopyBarriers[0].oldLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
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
                        preCopyBarriers[1].subresourceRange = depthOnlyRange;

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
                        postCopyBarriers[1].subresourceRange = depthOnlyRange;

                        vk::DependencyInfo depInfo{};
                        depInfo.imageMemoryBarrierCount = static_cast<uint32_t>(postCopyBarriers.size());
                        depInfo.pImageMemoryBarriers = postCopyBarriers.data();
                        cmd.pipelineBarrier2KHR(depInfo);
                    }
                });
            // Depth ends at eDepthStencilAttachmentOptimal after post-copy barrier
            builder.opaqueRead(depthHandle, graph::ResourceUsage::DepthAttachmentRead);
            depthHandle = builder.opaqueWrite(depthHandle, graph::ResourceUsage::DepthAttachmentWrite);
            builder.setSegment(graph::HookSegment::PostScene);
            builder.setSideEffect();
        }

        // --- Post-process / Upscale (opaque) ---

        {
            auto* upscaleManager = device.getUpscaleManager();
            bool upscalingActive = upscaleManager && upscaleManager->isActive();

            if (upscalingActive)
            {
                auto builder = frameGraph->addPass("Upscale",
                    [this](vk::CommandBuffer cmd, uint32_t idx) {
                        executeUpscale(cmd, idx);
                        executePostUpscalePostProcess(cmd, idx);
                    });
                builder.opaqueRead(sceneColorHandle, graph::ResourceUsage::ShaderRead);
                builder.opaqueRead(depthHandle, graph::ResourceUsage::DepthAttachmentRead);
                sceneColorHandle = builder.opaqueWrite(sceneColorHandle, graph::ResourceUsage::ShaderRead);
                builder.setSegment(graph::HookSegment::PostProcess);
                builder.setSideEffect();
            }
            else
            {
                auto builder = frameGraph->addPass("PostProcess",
                    [this](vk::CommandBuffer cmd, uint32_t idx) {
                        executePostProcess(cmd, idx);
                    });
                builder.opaqueRead(sceneColorHandle, graph::ResourceUsage::ShaderRead);
                sceneColorHandle = builder.opaqueWrite(sceneColorHandle, graph::ResourceUsage::ShaderRead);
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

        // UI overlays
        {
            auto builder = frameGraph->addPass("UIOverlays",
                [this](vk::CommandBuffer cmd, uint32_t idx) {
                    drawUIOverlays(cmd, idx);
                });
            builder.opaqueRead(sceneColorHandle, graph::ResourceUsage::ShaderRead);
            sceneColorHandle = builder.opaqueWrite(sceneColorHandle, graph::ResourceUsage::ShaderRead);
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
