#include "RenderPassHandler.hpp"
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
#include "upscaling/UpscaleManager.hpp"
#include "../../services/providers/vfx/IVFXRuntimeProvider.hpp"
#include "../../services/providers/terrain/ITerrainRenderProvider.hpp"
#include "../../services/providers/terrain/IOceanRenderProvider.hpp"
#include "../../services/providers/vegetation/IGrassRenderProvider.hpp"
#include "vfx/distortion/DistortionResources.hpp"
#include "vfx/distortion/VFXDistortionComposite.hpp"
#include "threading/JobSystem.hpp"
#include <chrono>

namespace
{
    void recordVFXAfterMeshPass(const vk::CommandBuffer& commandBuffer,
                                render::mesh::StaticMeshPipeline* meshPipeline,
                                services::IVFXRuntimeProvider* vfxProvider, uint32_t imageIndex)
    {
        meshPipeline->beginRenderPass(commandBuffer, imageIndex);
        meshPipeline->endRenderPass(commandBuffer);

        meshPipeline->beginVFXRenderPass(commandBuffer, imageIndex);
        vfxProvider->recordDrawCommands(commandBuffer);
        meshPipeline->endRenderPass(commandBuffer);
    }
}

namespace render
{
    void RenderPassHandler::draw(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex)
    {
        clearColor->recordCommandBuffer(commandBuffer, imageIndex);

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

            if (!asyncComputeActive)
                atmospherePipeline->dispatchCompute(commandBuffer);
            atmospherePipeline->renderSky(commandBuffer, imageIndex);
        }
        else
        {
            iblRenderer->recordCommandBuffer(commandBuffer, imageIndex);
        }

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

            if (!asyncComputeActive)
                cloudPipeline->dispatchCompute(commandBuffer);
            cloudPipeline->renderComposite(commandBuffer, imageIndex);
        }

        drawSceneMeshes(commandBuffer, imageIndex);

        executeRenderHooks(plugin::RenderPassHookPoint::PostScene, commandBuffer, imageIndex);

        executeDistortionPass(commandBuffer, imageIndex);

        drawOverlays(commandBuffer, imageIndex);
        executeOcclusionPasses(commandBuffer);

        if (atmospherePipeline && atmospherePipeline->isEnabled())
            atmospherePipeline->renderComposite(commandBuffer, imageIndex);

        if (volumetricFogComposite && volumetricFogComposite->isInitialized())
        {
            volumetricFogComposite->setCameraData(currentNearPlane, currentFarPlane);
            volumetricFogComposite->execute(commandBuffer, imageIndex);
        }

        if (ssgiPipeline && ssgiPipeline->isInitialized())
        {
            ssgiPipeline->setCameraData(currentView, currentProjection,
                                         currentCameraPosition,
                                         currentNearPlane, currentFarPlane,
                                         taaFrameIndex);
            ssgiPipeline->execute(commandBuffer, imageIndex);
        }

        executeRenderHooks(plugin::RenderPassHookPoint::PrePostProcess, commandBuffer, imageIndex);

        // Copy current depth to prev-frame depth slot for async compute motion vectors.
        // Uses synchronization2 barriers for consistency with the rest of the codebase.
        if (offscreenResources.prevFrameDepthCreated)
        {
            uint32_t currentFrame = imageIndex % core::MAX_FRAMES_IN_FLIGHT;
            vk::ImageSubresourceRange depthStencilRange(
                vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil, 0, 1, 0, 1);
            vk::ImageSubresourceRange depthOnlyRange(
                vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1);

            // Pre-copy: transition main depth (attachment->transferSrc) and
            // prev-frame depth (undefined->transferDst) in a single barrier call
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
                commandBuffer.pipelineBarrier2KHR(depInfo);
            }

            // Copy depth
            vk::ImageCopy region{};
            region.srcSubresource = {vk::ImageAspectFlagBits::eDepth, 0, 0, 1};
            region.dstSubresource = {vk::ImageAspectFlagBits::eDepth, 0, 0, 1};
            region.extent = vk::Extent3D(
                swapChain.getSwapchainExtent().width,
                swapChain.getSwapchainExtent().height,
                1);

            commandBuffer.copyImage(
                offscreenResources.depthImage.depthImage, vk::ImageLayout::eTransferSrcOptimal,
                offscreenResources.prevFrameDepth[currentFrame].image, vk::ImageLayout::eTransferDstOptimal,
                region);

            // Post-copy: transition main depth back (transferSrc->attachment) and
            // prev-frame depth to shader read (for async compute next frame)
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
                commandBuffer.pipelineBarrier2KHR(depInfo);
            }
        }

        {
            auto* upscaleManager = device.getUpscaleManager();
            bool upscalingActive = upscaleManager && upscaleManager->isActive();

            if (upscalingActive)
            {
                // Feed raw HDR scene color directly to DLSS (no pre-upscale post-process).
                // DLSS expects jittered HDR input. All post-process runs after at display resolution.
                executeUpscale(commandBuffer, imageIndex);
                executePostUpscalePostProcess(commandBuffer, imageIndex);
            }
            else
            {
                executePostProcess(commandBuffer, imageIndex);
            }
        }

        executeRenderHooks(plugin::RenderPassHookPoint::PostPostProcess, commandBuffer, imageIndex);

        drawUIOverlays(commandBuffer, imageIndex);

        executeRenderHooks(plugin::RenderPassHookPoint::Overlay, commandBuffer, imageIndex);
    }

    void RenderPassHandler::drawSceneMeshes(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        bool hasDebugItems = debugRendererInitialized && debugRenderer->hasItemsToRender();
        bool hasVFX = vfxRuntimeProvider && vfxRuntimeProvider->isInitialized()
            && vfxRuntimeProvider->getInstanceCount() > 0;
        bool hasCustomShaderMeshes = !customShaderMeshDrawList.empty();

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

        bool hasTerrainToRender = gpuDrivenRenderer && gpuDrivenRenderer->isTerrainRenderingEnabled()
            && terrainRenderProvider && terrainRenderProvider->hasActiveTerrain();

        bool hasWaterToRender = gpuDrivenRenderer && gpuDrivenRenderer->isWaterRenderingEnabled()
            && oceanRenderProvider && oceanRenderProvider->hasActiveOcean();

        bool needsMeshPass = meshPipelineInitialized && (!currentMeshDrawList.empty() || hasCustomShaderMeshes
            || hasDebugItems || hasVFX || hasTerrainToRender || hasWaterToRender);

        updateGPUDrivenSceneData();

        if (!needsMeshPass)
        {
            if (decalRenderingEnabled && decalPipeline && decalPipeline->isInitialized() && decalPipeline->hasDecals())
            {
                decalPipeline->setCameraData(currentView, currentProjection, currentNearPlane, currentFarPlane);
                decalPipeline->render(commandBuffer, imageIndex);
            }
            return;
        }

        DebugRenderer* debugRendererPtr = hasDebugItems ? debugRenderer.get() : nullptr;

        if (hasVFX && !asyncComputeActive)
            vfxRuntimeProvider->recordComputeCommands(commandBuffer);

        bool hasMeshesToRender = !currentMeshDrawList.empty();

        if ((hasMeshesToRender || hasTerrainToRender || hasWaterToRender) && gpuDrivenRendererInitialized && gpuDrivenRenderer->isEnabled())
        {
            drawGPUDrivenMeshPass(commandBuffer, imageIndex, debugRendererPtr, hasCustomShaderMeshes, hasVFX);
        }
        else if (!currentMeshDrawList.empty() || hasCustomShaderMeshes || hasDebugItems)
        {
            meshPipeline->recordCommandBuffer(commandBuffer, imageIndex, combinedMeshDrawList, currentFrustum,
                                              debugRendererPtr, currentView, currentProjection);
            if (hasVFX)
                recordVFXAfterMeshPass(commandBuffer, meshPipeline.get(), vfxRuntimeProvider, imageIndex);
        }
        else if (hasVFX)
        {
            recordVFXAfterMeshPass(commandBuffer, meshPipeline.get(), vfxRuntimeProvider, imageIndex);
        }
    }

    void RenderPassHandler::drawGPUDrivenMeshPass(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex,
                                                   DebugRenderer* debugRendererPtr, bool hasCustomShaderMeshes,
                                                   bool hasVFX) const
    {
        updateGPUDrivenHiZ();
        if (asyncComputeActive)
            gpuDrivenRenderer->dispatchGraphicsCompute(commandBuffer, imageIndex);
        else
            gpuDrivenRenderer->dispatchCompute(commandBuffer, imageIndex);

        vk::DescriptorSet iblDescriptorSet = meshPipeline->getIBLDescriptorSet(imageIndex);

        // Depth prepass for meshlet-level Hi-Z occlusion culling
        if (gpuDrivenRenderer->isMeshletOcclusionCullingEnabled())
        {
            gpuDrivenRenderer->renderDepthPrepass(commandBuffer, iblDescriptorSet);
            gpuDrivenRenderer->generatePrepassHiZ(commandBuffer);
        }

        // RT shadow dispatch (after depth+normal prepass, before forward pass)
        if (gpuDrivenRenderer->isRTShadowReady())
        {
            gpuDrivenRenderer->dispatchRTShadow(commandBuffer, imageIndex);
        }

        if (oceanFFTInitialized)
        {
            gpuDrivenRenderer->readbackOceanDisplacement();
            gpuDrivenRenderer->dispatchOceanFFT(commandBuffer, currentTime);
        }

        bool useParallel = parallelSceneRecording && sceneThreadPoolManager &&
                           sceneThreadPoolManager->getThreadCount() > 1;

        bool wboitActive = wboitEnabled && wboitPipeline && wboitPipeline->isInitialized()
                           && gpuDrivenRenderer->isWBOITReady();

        if (useParallel)
            recordParallelScenePass(commandBuffer, imageIndex, iblDescriptorSet,
                                    debugRendererPtr, hasCustomShaderMeshes, wboitActive);
        else
            recordInlineScenePass(commandBuffer, imageIndex, iblDescriptorSet,
                                  debugRendererPtr, hasCustomShaderMeshes, wboitActive);

        if (decalRenderingEnabled && decalPipeline && decalPipeline->isInitialized() && decalPipeline->hasDecals())
        {
            decalPipeline->setCameraData(currentView, currentProjection, currentNearPlane, currentFarPlane);
            decalPipeline->render(commandBuffer, imageIndex);
        }

        if (wboitActive && gpuDrivenRenderer->hasTransparentObjects())
        {
            wboitPipeline->beginWBOITPass(commandBuffer, imageIndex);
            gpuDrivenRenderer->renderWBOITDraw(commandBuffer, iblDescriptorSet);
            wboitPipeline->endWBOITPass(commandBuffer);
            wboitPipeline->composite(commandBuffer, imageIndex);
        }

        if (hasVFX)
        {
            meshPipeline->beginVFXRenderPass(commandBuffer, imageIndex);
            vfxRuntimeProvider->recordDrawCommands(commandBuffer);
            meshPipeline->endRenderPass(commandBuffer);
        }
    }

    void RenderPassHandler::recordParallelScenePass(
        const vk::CommandBuffer& commandBuffer, uint32_t imageIndex,
        vk::DescriptorSet iblDescriptorSet, DebugRenderer* debugRendererPtr,
        bool hasCustomShaderMeshes, bool wboitActive) const
    {
        auto sceneRecordStart = std::chrono::high_resolution_clock::now();
        sceneThreadPoolManager->resetFrame(imageIndex);

        vk::RenderPass rp = meshPipeline->getRenderPass();
        vk::Framebuffer fb = meshPipeline->getFramebuffer(imageIndex);
        auto extent = swapChain.getSwapchainExtent();

        auto setupSecondary = [&](vk::CommandBuffer sec) {
            vk::CommandBufferInheritanceInfo inheritance{};
            inheritance.renderPass = rp;
            inheritance.subpass = 0;
            inheritance.framebuffer = fb;

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

        // When water is present: billboards/overlays drawn inline after render pass break
        // When no water: original secondary command buffer approach
        std::future<void> waterFuture;
        if (!hasWater && hasBillboards)
        {
            waterFuture = threading::JobSystem::instance().submit([&]() {
                waterCmd = sceneThreadPoolManager->getSecondary(3, imageIndex);
                setupSecondary(waterCmd);
                gpuDrivenRenderer->renderBillboardDraw(waterCmd, iblDescriptorSet);
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
            overlayCmd.end();
        }

        meshPipeline->beginRenderPassForSecondary(commandBuffer, imageIndex);

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
            // Break render pass for refraction copy
            meshPipeline->endRenderPass(commandBuffer);

            auto extent = swapChain.getSwapchainExtent();
            gpuDrivenRenderer->copySceneColorForRefraction(
                commandBuffer,
                offscreenResources.colorImages[imageIndex].colorImage,
                extent.width, extent.height);

            // Water continue pass (inline)
            meshPipeline->beginWaterContinuePass(commandBuffer, imageIndex);

            vk::Viewport viewport{0.0f, 0.0f,
                                   static_cast<float>(extent.width),
                                   static_cast<float>(extent.height),
                                   0.0f, 1.0f};
            commandBuffer.setViewport(0, viewport);
            vk::Rect2D scissor{{0, 0}, extent};
            commandBuffer.setScissor(0, scissor);

            gpuDrivenRenderer->renderWaterDraw(commandBuffer, iblDescriptorSet);

            if (hasBillboards)
                gpuDrivenRenderer->renderBillboardDraw(commandBuffer, iblDescriptorSet);

            if (hasCustomShaderMeshes)
                meshPipeline->renderMeshList(commandBuffer, imageIndex, customShaderMeshDrawList, currentFrustum);

            gpuDrivenRenderer->renderGIDebug(commandBuffer, currentProjection * currentView);

            if (debugRendererPtr)
            {
                debugRendererPtr->render(commandBuffer, combinedMeshDrawList, currentView, currentProjection,
                    [this](const std::string& meshId) { return meshPipeline->getMesh(meshId); });
            }
        }

        auto sceneRecordEnd = std::chrono::high_resolution_clock::now();
        lastSceneRecordingUs = std::chrono::duration<float, std::micro>(sceneRecordEnd - sceneRecordStart).count();
        lastSceneSecondaryCount = static_cast<uint32_t>(secondaries.size());

        meshPipeline->endRenderPass(commandBuffer);
    }

    void RenderPassHandler::recordInlineScenePass(
        const vk::CommandBuffer& commandBuffer, uint32_t imageIndex,
        vk::DescriptorSet iblDescriptorSet, DebugRenderer* debugRendererPtr,
        bool hasCustomShaderMeshes, bool wboitActive) const
    {
        meshPipeline->beginRenderPass(commandBuffer, imageIndex);

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
            // End main render pass to copy scene color for refraction
            meshPipeline->endRenderPass(commandBuffer);

            // Copy scene color to refraction texture
            gpuDrivenRenderer->copySceneColorForRefraction(
                commandBuffer,
                offscreenResources.colorImages[imageIndex].colorImage,
                extent.width, extent.height);

            // Restart render pass with load ops (preserves color + depth)
            meshPipeline->beginWaterContinuePass(commandBuffer, imageIndex);

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
            gpuDrivenRenderer->renderBillboardDraw(commandBuffer, iblDescriptorSet);

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

        meshPipeline->endRenderPass(commandBuffer);
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
        // Only color attachment (index 0) is cleared; depth uses eLoad
        vk::ClearValue colorClear{};
        colorClear.color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}};

        vk::RenderPassBeginInfo rpBegin{};
        rpBegin.renderPass = distortionResources->getDistortionVectorRenderPass();
        rpBegin.framebuffer = distortionResources->getDistortionVectorFramebuffer();
        rpBegin.renderArea.offset = vk::Offset2D{0, 0};
        rpBegin.renderArea.extent = extent;
        rpBegin.clearValueCount = 1;
        rpBegin.pClearValues = &colorClear;

        commandBuffer.beginRenderPass(rpBegin, vk::SubpassContents::eInline);

        vk::Viewport viewport{0.0f, 0.0f,
            static_cast<float>(extent.width), static_cast<float>(extent.height),
            0.0f, 1.0f};
        commandBuffer.setViewport(0, viewport);
        vk::Rect2D scissor{{0, 0}, extent};
        commandBuffer.setScissor(0, scissor);

        vfxRuntimeProvider->recordDistortionDrawCommands(commandBuffer);

        commandBuffer.endRenderPass();

        // 3. Composite pass: apply distortion to scene color
        distortionComposite->record(commandBuffer,
            distortionResources->getCompositeRenderPass(),
            distortionResources->getCompositeFramebuffer(imageIndex),
            extent,
            distortionResources->getCompositeDescriptorSet());
    }

    void RenderPassHandler::drawOverlays(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        if (billboardPipelineInitialized && !currentBillboardDrawList.empty())
            billboardPipeline->recordCommandBuffer(commandBuffer, imageIndex);

        if (textPipelineInitialized && !currentTextDrawList.empty())
            textPipeline->recordCommandBuffer(commandBuffer, imageIndex);
    }
}
