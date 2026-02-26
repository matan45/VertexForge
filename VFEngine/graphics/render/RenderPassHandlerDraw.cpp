#include "RenderPassHandler.hpp"
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
#include "../../services/providers/IVFXRuntimeProvider.hpp"
#include "../../services/providers/ITerrainRenderProvider.hpp"
#include "../../services/providers/IWaterRenderProvider.hpp"
#include "water/WaterTypes.hpp"
#include "water/WaterTile.hpp"

namespace
{
    void recordVFXAfterMeshPass(const vk::CommandBuffer& commandBuffer,
                                render::mesh::StaticMeshPipeline* meshPipeline,
                                services::IVFXRuntimeProvider* vfxProvider, uint32_t imageIndex)
    {
        // Begin mesh pass to clear depth, then immediately end it
        meshPipeline->beginRenderPass(commandBuffer, imageIndex);
        meshPipeline->endRenderPass(commandBuffer);

        // VFX render pass transitions depth to read-only for soft particle sampling
        meshPipeline->beginVFXRenderPass(commandBuffer, imageIndex);
        vfxProvider->recordDrawCommands(commandBuffer);
        meshPipeline->endRenderPass(commandBuffer);
    }
}

namespace render
{
    void RenderPassHandler::draw(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        clearColor->recordCommandBuffer(commandBuffer, imageIndex);
        iblRenderer->recordCommandBuffer(commandBuffer, imageIndex);

        drawSceneMeshes(commandBuffer, imageIndex);
        drawOverlays(commandBuffer, imageIndex);
        executeOcclusionPasses(commandBuffer);

        if (volumetricFogComposite && volumetricFogComposite->isInitialized())
        {
            volumetricFogComposite->setCameraData(currentNearPlane, currentFarPlane);
            volumetricFogComposite->execute(commandBuffer, imageIndex);
        }

        executePostProcess(commandBuffer, imageIndex);
        drawUIOverlays(commandBuffer, imageIndex);
    }

    void RenderPassHandler::updateGPUDrivenSceneData() const
    {
        if (!gpuDrivenRendererInitialized || !meshPipelineInitialized)
        {
            return;
        }

        gpuDrivenRenderer->updateScene(
            currentMeshDrawList, currentView, currentProjection,
            currentCameraPosition, currentNearPlane, currentFarPlane, currentTime);

        if (terrainRenderProvider && terrainRenderProvider->hasActiveTerrain() && currentFrustum)
        {
            auto visibleTiles = terrainRenderProvider->getVisibleTiles(*currentFrustum, currentCameraPosition);

            // Merge terrain tiles visible to additional cameras (RTT) so they are
            // available on the GPU for RTT render passes in the next frame.
            // Uses queryVisibleTiles (frustum+distance only) to avoid side effects
            // like LOD changes and isVisible flag corruption on the main camera tiles.
            for (const auto& [rttFrustum, rttCameraPos] : additionalTerrainFrustums)
            {
                auto rttTiles = terrainRenderProvider->queryVisibleTiles(rttFrustum, rttCameraPos);
                for (auto* tile : rttTiles)
                {
                    if (std::find(visibleTiles.begin(), visibleTiles.end(), tile) == visibleTiles.end())
                    {
                        visibleTiles.push_back(tile);
                    }
                }
            }

            auto matPath = terrainRenderProvider->getTerrainMaterialPath();
            gpuDrivenRenderer->updateTerrain(visibleTiles, currentCameraPosition, matPath);
        }

        if (waterRenderProvider && waterRenderProvider->hasActiveWater() && currentFrustum)
        {
            auto visibleTiles = waterRenderProvider->getVisibleWaterTiles(*currentFrustum, currentCameraPosition);

            // Merge water tiles visible to additional cameras (RTT) so they are
            // available on the GPU for RTT render passes in the next frame.
            // Uses queryVisibleWaterTiles to avoid corrupting isVisible flags.
            for (const auto& [rttFrustum, rttCameraPos] : additionalWaterFrustums)
            {
                auto rttTiles = waterRenderProvider->queryVisibleWaterTiles(rttFrustum, rttCameraPos);
                for (auto* tile : rttTiles)
                {
                    if (std::find(visibleTiles.begin(), visibleTiles.end(), tile) == visibleTiles.end())
                    {
                        visibleTiles.push_back(tile);
                    }
                }
            }

            auto settings = waterRenderProvider->getWaterGlobalSettings();
            auto tileConfig = waterRenderProvider->getWaterTileConfig();
            gpuDrivenRenderer->updateWater(visibleTiles, settings, tileConfig);
        }

        // Consume and discard RTT frustums so they don't persist across frames.
        // renderAll() re-populates them each frame during play mode.
        additionalTerrainFrustums.clear();
        additionalWaterFrustums.clear();
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
        }

        bool hasTerrainToRender = gpuDrivenRenderer && gpuDrivenRenderer->isTerrainRenderingEnabled()
            && terrainRenderProvider && terrainRenderProvider->hasActiveTerrain();

        bool hasWaterToRender = gpuDrivenRenderer && gpuDrivenRenderer->isWaterRenderingEnabled()
            && waterRenderProvider && waterRenderProvider->hasActiveWater();

        bool needsMeshPass = meshPipelineInitialized && (!currentMeshDrawList.empty() || hasCustomShaderMeshes
            || hasDebugItems || hasVFX || hasTerrainToRender || hasWaterToRender);

        updateGPUDrivenSceneData();

        if (!needsMeshPass)
        {
            return;
        }

        DebugRenderer* debugRendererPtr = hasDebugItems ? debugRenderer.get() : nullptr;

        if (hasVFX)
        {
            vfxRuntimeProvider->recordComputeCommands(commandBuffer);
        }

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
            {
                recordVFXAfterMeshPass(commandBuffer, meshPipeline.get(), vfxRuntimeProvider, imageIndex);
            }
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
        gpuDrivenRenderer->dispatchCompute(commandBuffer);

        vk::DescriptorSet iblDescriptorSet = meshPipeline->getIBLDescriptorSet(imageIndex);
        meshPipeline->beginRenderPass(commandBuffer, imageIndex);

        // Set dynamic viewport/scissor for mesh shader pipelines
        auto extent = swapChain.getSwapchainExtent();
        vk::Viewport viewport{0.0f, 0.0f,
                               static_cast<float>(extent.width), static_cast<float>(extent.height),
                               0.0f, 1.0f};
        commandBuffer.setViewport(0, viewport);
        vk::Rect2D scissor{{0, 0}, extent};
        commandBuffer.setScissor(0, scissor);

        gpuDrivenRenderer->renderDraw(commandBuffer, iblDescriptorSet);

        bool useWBOIT = wboitEnabled && wboitPipeline && wboitPipeline->isInitialized()
                        && gpuDrivenRenderer->isWBOITReady();
        if (!useWBOIT)
        {
            gpuDrivenRenderer->renderTransparentDraw(commandBuffer, iblDescriptorSet);
        }

        // Additive/Multiply always drawn in main pass (commutative, don't need OIT)
        gpuDrivenRenderer->renderBlendDraw(commandBuffer, iblDescriptorSet);

        if (gpuDrivenRenderer->isTerrainRenderingEnabled())
        {
            gpuDrivenRenderer->renderTerrainDraw(commandBuffer, iblDescriptorSet);
        }

        if (gpuDrivenRenderer->isWaterRenderingEnabled())
        {
            gpuDrivenRenderer->renderWaterDraw(commandBuffer, iblDescriptorSet);
        }

        if (hasCustomShaderMeshes)
        {
            meshPipeline->renderMeshList(commandBuffer, imageIndex, customShaderMeshDrawList, currentFrustum);
        }

        if (debugRendererPtr)
        {
            debugRendererPtr->render(commandBuffer, combinedMeshDrawList, currentView, currentProjection,
                                     [this](const std::string& meshId)
                                     {
                                         return meshPipeline->getMesh(meshId);
                                     });
        }

        meshPipeline->endRenderPass(commandBuffer);

        if (useWBOIT && gpuDrivenRenderer->hasTransparentObjects())
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

    void RenderPassHandler::drawOverlays(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        if (billboardPipelineInitialized && !currentBillboardDrawList.empty())
        {
            billboardPipeline->recordCommandBuffer(commandBuffer, imageIndex);
        }

        if (textPipelineInitialized && !currentTextDrawList.empty())
        {
            textPipeline->recordCommandBuffer(commandBuffer, imageIndex);
        }
    }

    void RenderPassHandler::executeOcclusionPasses(const vk::CommandBuffer& commandBuffer) const
    {
        occlusion::CameraId activeCameraId = cameraOcclusionManager->getActiveCameraId();

        if (!cameraOcclusionManager->isHiZInitialized(activeCameraId))
        {
            return;
        }

        cameraOcclusionManager->generateHiZ(activeCameraId, commandBuffer);

        if (cameraOcclusionManager->isOcclusionInitialized(activeCameraId))
        {
            cameraOcclusionManager->runOcclusionCulling(activeCameraId, commandBuffer);
        }

        if (terrainRaycastPipeline && terrainRaycastPipeline->isInitialized())
        {
            dispatchTerrainRaycast(commandBuffer);
        }
    }

    void RenderPassHandler::dispatchTerrainRaycast(const vk::CommandBuffer& commandBuffer) const
    {
        vk::ImageMemoryBarrier toShaderRead{};
        toShaderRead.oldLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        toShaderRead.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        toShaderRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toShaderRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toShaderRead.image = offscreenResources.depthImage.depthImage;
        toShaderRead.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth
            | vk::ImageAspectFlagBits::eStencil;
        toShaderRead.subresourceRange.baseMipLevel = 0;
        toShaderRead.subresourceRange.levelCount = 1;
        toShaderRead.subresourceRange.baseArrayLayer = 0;
        toShaderRead.subresourceRange.layerCount = 1;
        toShaderRead.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        toShaderRead.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        commandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eLateFragmentTests,
            vk::PipelineStageFlagBits::eComputeShader,
            {}, {}, {}, toShaderRead);

        glm::mat4 invViewProjection = glm::inverse(currentProjection * currentView);
        auto extent = swapChain.getSwapchainExtent();
        terrainRaycastPipeline->dispatch(commandBuffer, invViewProjection, extent.width, extent.height);
        terrainRaycastPipeline->copyResultsToStaging(commandBuffer);

        vk::ImageMemoryBarrier toAttachment{};
        toAttachment.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        toAttachment.newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        toAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toAttachment.image = offscreenResources.depthImage.depthImage;
        toAttachment.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth
            | vk::ImageAspectFlagBits::eStencil;
        toAttachment.subresourceRange.baseMipLevel = 0;
        toAttachment.subresourceRange.levelCount = 1;
        toAttachment.subresourceRange.baseArrayLayer = 0;
        toAttachment.subresourceRange.layerCount = 1;
        toAttachment.srcAccessMask = vk::AccessFlagBits::eShaderRead;
        toAttachment.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead
            | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

        commandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eEarlyFragmentTests,
            {}, {}, {}, toAttachment);
    }

    void RenderPassHandler::executePostProcess(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        postProcessPipeline->setCameraData(currentNearPlane, currentFarPlane,
                                           currentCameraPosition, currentView, currentProjection, currentTime);

        if (gpuDrivenRendererInitialized)
        {
            updateSunScreenPosition();
        }

        postProcessPipeline->execute(commandBuffer, imageIndex);
    }

    void RenderPassHandler::updateSunScreenPosition() const
    {
        auto* lbm = gpuDrivenRenderer->getLightBufferManager();
        auto sunDir = lbm->getFirstDirectionalLightDirection();
        if (!sunDir)
        {
            postProcessPipeline->setSunData({0.5f, 0.5f}, false);
            return;
        }

        glm::vec3 sunWorldPos = currentCameraPosition - (*sunDir) * currentFarPlane;
        glm::vec4 clip = currentProjection * currentView * glm::vec4(sunWorldPos, 1.0f);
        if (clip.w > 0.0f)
        {
            glm::vec2 screenUV = (glm::vec2(clip) / clip.w) * 0.5f + 0.5f;
            postProcessPipeline->setSunData(screenUV, true);
        }
        else
        {
            postProcessPipeline->setSunData({0.5f, 0.5f}, false);
        }
    }

    void RenderPassHandler::drawUIOverlays(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        if (uiPipelineInitialized && !currentUIImageDrawList.empty())
        {
            uiPipeline->recordCommandBuffer(commandBuffer, imageIndex);
        }

        if (uiTextPipelineInitialized && !currentUITextDrawList.empty())
        {
            uiTextPipeline->recordCommandBuffer(commandBuffer, imageIndex);
        }
    }
}
