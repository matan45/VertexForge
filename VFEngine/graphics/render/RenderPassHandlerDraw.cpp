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
#include <chrono>

namespace render
{
    void RenderPassHandler::draw(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex)
    {
        // Plugin texture CPU->GPU uploads — recorded before the frame graph so the
        // copies land outside any render pass and complete before the scene samples them.
        if (pluginTextureManager) pluginTextureManager->flushUploads(commandBuffer);

        // Lit plugin custom pipelines: pick up the RT shadow mask layout once the
        // RT shadow pipeline comes online — rebuilds them with RT_SHADOW_ENABLED
        // + set 13. Cheap no-op while the layout is unchanged.
        syncCustomPipelineRTShadow();

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
        if (uiPipelineInitialized && !currentUIImageDrawList.empty())
            uiPipeline->recordCommandBufferGraphManaged(commandBuffer, imageIndex);

        if (uiTextPipelineInitialized && !currentUITextDrawList.empty())
            uiTextPipeline->recordCommandBufferGraphManaged(commandBuffer, imageIndex);
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

        bool hasTerrainToRender = gpuDrivenRenderer && gpuDrivenRenderer->isTerrainRenderingEnabled()
            && terrainRenderProvider && terrainRenderProvider->hasActiveTerrain();

        bool hasWaterToRender = gpuDrivenRenderer && gpuDrivenRenderer->isWaterRenderingEnabled()
            && oceanRenderProvider && oceanRenderProvider->hasActiveOcean();

        bool needsMeshPass = meshPipelineInitialized && (!currentMeshDrawList.empty() || hasCustomShaderMeshes
            || hasDebugItems || hasVFX || hasTerrainToRender || hasWaterToRender || hasPluginDraws);

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

        if ((hasMeshesToRender || hasTerrainToRender || hasWaterToRender || hasPluginDraws)
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
            recordParallelScenePassGraphManaged(commandBuffer, imageIndex, iblDescriptorSet,
                                                debugRendererPtr, hasCustomShaderMeshes, wboitActive);
        else
            recordInlineScenePassGraphManaged(commandBuffer, imageIndex, iblDescriptorSet,
                                              debugRendererPtr, hasCustomShaderMeshes, wboitActive);

        if (decalRenderingEnabled && decalPipeline && decalPipeline->isInitialized() && decalPipeline->hasDecals())
        {
            decalPipeline->setCameraData(currentView, currentProjection, currentNearPlane, currentFarPlane);
            decalPipeline->renderGraphManaged(commandBuffer, imageIndex);
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

        auto setupSecondary = [&](vk::CommandBuffer sec) {
            vk::CommandBufferInheritanceRenderingInfo inheritRendering{};
            inheritRendering.colorAttachmentCount = 1;
            inheritRendering.pColorAttachmentFormats = &colorFormat;
            inheritRendering.depthAttachmentFormat = depthFormat;

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
                gpuDrivenRenderer->renderBillboardDraw(commandBuffer, iblDescriptorSet);

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

        // Plugin custom pipelines draw last in the scene pass, just before
        // post-processing.
        if (customPipelineManager)
            customPipelineManager->render(commandBuffer, currentView, currentProjection,
                                          buildCustomLightingSets(iblDescriptorSet));

        meshPipeline->endRenderPassGraphManaged(commandBuffer, imageIndex);
    }
}
