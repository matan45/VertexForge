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
#include "../../services/providers/IVFXRuntimeProvider.hpp"
#include "../../services/providers/ITerrainRenderProvider.hpp"

namespace
{
    void recordVFXInMeshPass(const vk::CommandBuffer& commandBuffer,
                             render::mesh::StaticMeshPipeline* meshPipeline,
                             services::IVFXRuntimeProvider* vfxProvider, uint32_t imageIndex)
    {
        meshPipeline->beginRenderPass(commandBuffer, imageIndex);
        vfxProvider->recordDrawCommands(commandBuffer);
        meshPipeline->endRenderPass(commandBuffer);
    }
}

namespace render
{
    // --- GPU-driven camera & scene data ---

    void RenderPassHandler::setGPUDrivenCameraData(const glm::vec3& cameraPos, float nearPlane, float farPlane,
                                                   float time)
    {
        currentCameraPosition = cameraPos;
        currentNearPlane = nearPlane;
        currentFarPlane = farPlane;
        currentTime = time;
    }

    void RenderPassHandler::setVisibleLightsFromBVH(const std::vector<uint32_t>& visibleLights)
    {
        if (gpuDrivenRenderer && gpuDrivenRendererInitialized)
        {
            gpuDrivenRenderer->setVisibleLightsFromBVH(visibleLights);
        }
    }

    void RenderPassHandler::clearVisibleLights()
    {
        if (gpuDrivenRenderer && gpuDrivenRendererInitialized)
        {
            gpuDrivenRenderer->clearVisibleLights();
        }
    }

    void RenderPassHandler::readBackLightOcclusionResults()
    {
        if (gpuDrivenRenderer && gpuDrivenRendererInitialized)
        {
            gpuDrivenRenderer->readBackLightOcclusionResults();
        }
    }

    // --- Terrain raycast ---

    void RenderPassHandler::readBackTerrainRaycastResults()
    {
        if (terrainRaycastPipeline && terrainRaycastPipeline->isInitialized())
        {
            terrainRaycastPipeline->readBackResults();
        }
    }

    void RenderPassHandler::setRaycastCursorUV(const glm::vec2& uv)
    {
        if (terrainRaycastPipeline)
        {
            terrainRaycastPipeline->setCursorUV(uv);
        }
    }

    void RenderPassHandler::clearRaycastCursor()
    {
        if (terrainRaycastPipeline)
        {
            terrainRaycastPipeline->clearCursor();
        }
    }

    terrain::TerrainHitResult RenderPassHandler::getTerrainHitResult() const
    {
        if (terrainRaycastPipeline && terrainRaycastPipeline->isInitialized())
        {
            return terrainRaycastPipeline->getLastResult();
        }
        return {};
    }

    void RenderPassHandler::setBrushOverlayParams(float radius, float falloff, float shape)
    {
        brushOverlayRadius_ = radius;
        brushOverlayFalloff_ = falloff;
        brushOverlayShape_ = shape;
    }

    void RenderPassHandler::updateBrushOverlayFromHitResult()
    {
        if (!gpuDrivenRendererInitialized || !gpuDrivenRenderer)
        {
            return;
        }

        auto hitResult = getTerrainHitResult();
        if (hitResult.hit && brushOverlayRadius_ > 0.0f)
        {
            glm::vec2 worldPos(hitResult.position.x, hitResult.position.z);
            gpuDrivenRenderer->setBrushOverlay(worldPos, brushOverlayRadius_, brushOverlayFalloff_, brushOverlayShape_);
        }
        else
        {
            gpuDrivenRenderer->setBrushOverlay(glm::vec2(0.0f), 0.0f, 0.0f, 0.0f);
        }
    }

    // --- GPU-driven view/culling setters ---

    void RenderPassHandler::setViewMode(uint32_t mode)
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setViewMode(mode);
        }
    }

    uint32_t RenderPassHandler::getViewMode() const
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            return gpuDrivenRenderer->getViewMode();
        }
        return 0;
    }

    void RenderPassHandler::setFrustumCullingEnabled(bool enabled)
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setFrustumCullingEnabled(enabled);
        }
    }

    void RenderPassHandler::setOcclusionCullingEnabled(bool enabled)
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setOcclusionCullingEnabled(enabled);
        }
    }

    void RenderPassHandler::setLODSelectionEnabled(bool enabled)
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setLODSelectionEnabled(enabled);
        }
    }

    void RenderPassHandler::setMeshletFrustumCullingEnabled(bool enabled)
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setMeshletFrustumCullingEnabled(enabled);
        }
    }

    void RenderPassHandler::setMeshletBackfaceCullingEnabled(bool enabled)
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setMeshletBackfaceCullingEnabled(enabled);
        }
    }

    void RenderPassHandler::setTerrainFrustumCullingEnabled(bool enabled)
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setTerrainFrustumCullingEnabled(enabled);
        }
    }

    void RenderPassHandler::setTerrainMeshletCullingEnabled(bool enabled)
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setTerrainMeshletCullingEnabled(enabled);
        }
    }

    void RenderPassHandler::setTerrainRenderingEnabled(bool enabled)
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setTerrainRenderingEnabled(enabled);
        }
    }

    void RenderPassHandler::setTerrainLODBias(float bias)
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setTerrainLODBias(bias);
        }
    }

    void RenderPassHandler::setTerrainErrorThreshold(float threshold)
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setTerrainErrorThreshold(threshold);
        }
    }

    void RenderPassHandler::setTerrainTextureScale(float scale)
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setTerrainTextureScale(scale);
        }
    }

    void RenderPassHandler::setTerrainShadowLOD(uint32_t lod)
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setTerrainShadowLOD(lod);
        }
    }

    // --- HiZ update ---

    void RenderPassHandler::updateGPUDrivenHiZ() const
    {
        if (!gpuDrivenRendererInitialized || !gpuDrivenRenderer)
        {
            return;
        }

        occlusion::CameraId activeCameraId = cameraOcclusionManager->getActiveCameraId();
        if (!cameraOcclusionManager->isHiZInitialized(activeCameraId))
        {
            return;
        }

        auto* camera = cameraOcclusionManager->getCamera(activeCameraId);
        if (!camera || !camera->hiZBuffer || !camera->hiZBuffer->isInitialized())
        {
            return;
        }

        gpuDrivenRenderer->updateHiZPyramid(
            camera->hiZBuffer->getHiZImageView(),
            camera->hiZBuffer->getHiZSampler(),
            camera->hiZBuffer->getMipLevels()
        );

        if (!lightOcclusionInitialized)
        {
            gpuDrivenRenderer->initLightOcclusionCulling(camera->hiZBuffer.get());
            lightOcclusionInitialized = true;
        }
    }

    // --- Debug renderer delegation ---

    void RenderPassHandler::setCameraFrustumDrawList(std::vector<mesh::CameraFrustumRenderData>&& frustums)
    {
        if (debugRenderer)
        {
            debugRenderer->setCameraFrustumDrawList(std::move(frustums));
        }
    }

    void RenderPassHandler::setAudioSphereDrawList(std::vector<mesh::AudioSphereRenderData>&& spheres)
    {
        if (debugRenderer)
        {
            debugRenderer->setAudioSphereDrawList(std::move(spheres));
        }
    }

    void RenderPassHandler::setUICanvasOutlineDrawList(std::vector<mesh::UICanvasOutlineRenderData>&& outlines)
    {
        if (debugRenderer)
        {
            debugRenderer->setUICanvasOutlineDrawList(std::move(outlines));
        }
    }

    void RenderPassHandler::setUICanvasImageDrawList(std::vector<mesh::UICanvasImageRenderData>&& images)
    {
        if (debugRenderer)
        {
            debugRenderer->setUICanvasImageDrawList(std::move(images));
        }
    }

    void RenderPassHandler::setPhysicsColliderDrawList(std::vector<mesh::PhysicsColliderRenderData>&& colliders)
    {
        if (debugRenderer)
        {
            debugRenderer->setPhysicsColliderDrawList(std::move(colliders));
        }
    }

    void RenderPassHandler::setLightGizmoDrawList(std::vector<mesh::LightGizmoRenderData>&& gizmos)
    {
        if (debugRenderer)
        {
            debugRenderer->setLightGizmoDrawList(std::move(gizmos));
        }
    }

    void RenderPassHandler::setShowPhysicsDebug(bool show)
    {
        if (debugRenderer)
        {
            debugRenderer->setShowPhysicsDebug(show);
        }
    }

    bool RenderPassHandler::getShowPhysicsDebug() const
    {
        if (debugRenderer)
        {
            return debugRenderer->getShowPhysicsDebug();
        }
        return false;
    }

    void RenderPassHandler::setShowClusterDebug(bool show)
    {
        if (debugRenderer)
        {
            debugRenderer->setShowClusterDebug(show);
        }
    }

    bool RenderPassHandler::getShowClusterDebug() const
    {
        if (debugRenderer)
        {
            return debugRenderer->getShowClusterDebug();
        }
        return false;
    }

    void RenderPassHandler::setClusterDebugData(mesh::ClusterDebugRenderData&& data)
    {
        if (debugRenderer)
        {
            debugRenderer->setClusterDebugData(std::move(data));
        }
    }

    void RenderPassHandler::setDebugCameraMatrices(const glm::mat4& view, const glm::mat4& projection)
    {
        currentView = view;
        currentProjection = projection;
    }

    // --- Camera/occlusion forwarding ---

    occlusion::CameraRenderData* RenderPassHandler::createCamera(occlusion::CameraId id, bool enableOcclusion)
    {
        return cameraOcclusionManager->createCamera(id, enableOcclusion);
    }

    void RenderPassHandler::removeCamera(occlusion::CameraId id)
    {
        cameraOcclusionManager->removeCamera(id);
    }

    void RenderPassHandler::setActiveCamera(occlusion::CameraId id)
    {
        cameraOcclusionManager->setActiveCamera(id);
    }

    occlusion::CameraId RenderPassHandler::getActiveCameraId() const
    {
        return cameraOcclusionManager->getActiveCameraId();
    }

    void RenderPassHandler::initHiZ(occlusion::CameraId cameraId, vk::Image depthImage, vk::ImageView depthView,
                                    vk::Format depthFormat)
    {
        cameraOcclusionManager->initCameraHiZ(cameraId, depthImage, depthView, depthFormat);
    }

    void RenderPassHandler::updateOcclusionObjects(occlusion::CameraId cameraId,
                                                   const std::vector<occlusion::GPUObjectData>& objects)
    {
        cameraOcclusionManager->updateOcclusionObjects(cameraId, objects);
    }

    void RenderPassHandler::updateOcclusionCamera(occlusion::CameraId cameraId, const glm::mat4& viewProj,
                                                  float nearPlane)
    {
        cameraOcclusionManager->updateCamera(cameraId, viewProj, nearPlane);
    }

    // --- Draw methods ---

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
            auto matPath = terrainRenderProvider->getTerrainMaterialPath();
            gpuDrivenRenderer->updateTerrain(visibleTiles, currentCameraPosition, matPath);
        }
    }

    void RenderPassHandler::drawSceneMeshes(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        bool hasDebugItems = debugRendererInitialized && debugRenderer->hasItemsToRender();
        bool hasVFX = vfxRuntimeProvider && vfxRuntimeProvider->isInitialized()
            && vfxRuntimeProvider->getInstanceCount() > 0;
        bool hasCustomShaderMeshes = !customShaderMeshDrawList.empty();

        if (hasVFX)
        {
            vfxRuntimeProvider->setCamera(currentView, currentProjection, currentCameraPosition, currentTime);
        }

        bool hasTerrainToRender = gpuDrivenRenderer && gpuDrivenRenderer->isTerrainRenderingEnabled()
            && terrainRenderProvider && terrainRenderProvider->hasActiveTerrain();

        bool needsMeshPass = meshPipelineInitialized && (!currentMeshDrawList.empty() || hasCustomShaderMeshes
            || hasDebugItems || hasVFX || hasTerrainToRender);

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

        if ((hasMeshesToRender || hasTerrainToRender) && gpuDrivenRendererInitialized && gpuDrivenRenderer->isEnabled())
        {
            drawGPUDrivenMeshPass(commandBuffer, imageIndex, debugRendererPtr, hasCustomShaderMeshes, hasVFX);
        }
        else if (!currentMeshDrawList.empty() || hasCustomShaderMeshes || hasDebugItems)
        {
            meshPipeline->recordCommandBuffer(commandBuffer, imageIndex, combinedMeshDrawList, currentFrustum,
                                              debugRendererPtr, currentView, currentProjection);

            if (hasVFX)
            {
                recordVFXInMeshPass(commandBuffer, meshPipeline.get(), vfxRuntimeProvider, imageIndex);
            }
        }
        else if (hasVFX)
        {
            recordVFXInMeshPass(commandBuffer, meshPipeline.get(), vfxRuntimeProvider, imageIndex);
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

        gpuDrivenRenderer->renderDraw(commandBuffer, iblDescriptorSet);

        if (gpuDrivenRenderer->isTerrainRenderingEnabled())
        {
            gpuDrivenRenderer->renderTerrainDraw(commandBuffer, iblDescriptorSet);
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

        if (hasVFX)
        {
            vfxRuntimeProvider->recordDrawCommands(commandBuffer);
        }

        meshPipeline->endRenderPass(commandBuffer);
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
                                           currentCameraPosition, currentView, currentTime);

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
