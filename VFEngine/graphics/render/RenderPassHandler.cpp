#include "RenderPassHandler.hpp"
#include "../core/Device.hpp"
#include "../core/SwapChain.hpp"
#include "ClearColor.hpp"
#include "IBL.hpp"
#include "DebugRenderer.hpp"
#include "mesh/StaticMeshPipeline.hpp"
#include "mesh/MeshTypes.hpp"
#include "mesh/MeshGPUCache.hpp"
#include "billboard/BillboardPipeline.hpp"
#include "billboard/BillboardTypes.hpp"
#include "occlusion/CameraRenderData.hpp"
#include "tools/AudioSphereDebugRenderer.hpp"
#include "gpudriven/GPUDrivenRenderer.hpp"
#include "material/MaterialTextureCache.hpp"
#include "print/Logger.hpp"

namespace render
{
    RenderPassHandler::RenderPassHandler(core::Device& device, core::SwapChain& swapChain,
                                         core::OffscreenResources& offscreenResources) : device{device},
        swapChain{swapChain}, offscreenResources{offscreenResources}
        , clearColor{std::make_unique<ClearColor>(device, swapChain, offscreenResources)}
        , iblRenderer{std::make_unique<IBL>(device, swapChain, offscreenResources)}
        , meshPipeline{std::make_unique<mesh::StaticMeshPipeline>(device, swapChain, offscreenResources)}
        , billboardPipeline{std::make_unique<billboard::BillboardPipeline>(device, swapChain, offscreenResources)}
        , cameraOcclusionManager{std::make_unique<occlusion::CameraOcclusionManager>(device, swapChain)}
        , debugRenderer{std::make_unique<DebugRenderer>(device, swapChain)}
        , gpuDrivenRenderer{std::make_unique<gpudriven::GPUDrivenRenderer>(device, swapChain)}
    {
    }

    RenderPassHandler::~RenderPassHandler() = default;

    void RenderPassHandler::init()
    {
        clearColor->init();
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

        // Auto-initialize GPU-driven renderer now that mesh pipeline is ready
        // Skip for material preview to allow custom per-material shaders
        if (enableGPUDriven)
        {
            initGPUDrivenRenderer();
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

        // Get IBL descriptor set layout and render pass from mesh pipeline
        vk::DescriptorSetLayout iblLayout = meshPipeline->getIBLDescriptorSetLayout();
        vk::RenderPass renderPass = meshPipeline->getRenderPass();

        gpuDrivenRenderer->init(iblLayout, renderPass);

        // Set material texture cache for texture loading in GPU-driven path
        auto& texCache = meshPipeline->getMaterialTextureCache();
        gpuDrivenRenderer->setMaterialTextureCache(&texCache);

        // Set default texture for bindless array (1x1 white fallback)
        if (texCache.hasDefaultTexture()) {
            gpuDrivenRenderer->setDefaultTexture(texCache.getDefaultView(), texCache.getDefaultSampler());
        }

        gpuDrivenRenderer->setEnabled(true);
        gpuDrivenRendererInitialized = true;
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
    }

    void RenderPassHandler::setMeshDrawList(std::vector<mesh::MeshRenderData>&& meshes)
    {
        currentMeshDrawList = std::move(meshes);

        // Check if any meshes have showBoundingBox enabled for debug rendering
        if (debugRendererInitialized && debugRenderer)
        {
            bool hasBoundingBoxes = false;
            for (const auto& mesh : currentMeshDrawList)
            {
                if (mesh.showBoundingBox)
                {
                    hasBoundingBoxes = true;
                    break;
                }
            }
            debugRenderer->setHasBoundingBoxes(hasBoundingBoxes);
        }
    }

    void RenderPassHandler::initBillboardPipeline()
    {
        if (billboardPipelineInitialized)
        {
            return;
        }

        billboardPipeline->init();
        billboardPipelineInitialized = true;
    }

    void RenderPassHandler::setBillboardDrawList(std::vector<billboard::BillboardRenderData>&& billboards)
    {
        currentBillboardDrawList = std::move(billboards);
        if (billboardPipelineInitialized && billboardPipeline)
        {
            billboardPipeline->setBillboardList(currentBillboardDrawList);
        }
    }

    void RenderPassHandler::initDebugRenderer()
    {
        if (debugRendererInitialized)
        {
            return;
        }

        // Debug renderer needs mesh pipeline's render pass for proper depth testing
        if (!meshPipelineInitialized)
        {
            return;
        }

        debugRenderer->init(meshPipeline->getRenderPass());
        debugRendererInitialized = true;
    }

    void RenderPassHandler::setGPUDrivenCameraData(const glm::vec3& cameraPos, float nearPlane, float farPlane, float time)
    {
        currentCameraPosition = cameraPos;
        currentNearPlane = nearPlane;
        currentFarPlane = farPlane;
        currentTime = time;
    }

    void RenderPassHandler::setGPUDrivenOcclusionCullingEnabled(bool enabled)
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setOcclusionCullingEnabled(enabled);
        }
    }

    bool RenderPassHandler::isGPUDrivenOcclusionCullingEnabled() const
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            return gpuDrivenRenderer->isOcclusionCullingEnabled();
        }
        return false;
    }

    void RenderPassHandler::updateGPUDrivenHiZ() const
    {
        if (!gpuDrivenRendererInitialized || !gpuDrivenRenderer)
        {
            return;
        }

        // Get active camera's Hi-Z buffer
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

        // Pass Hi-Z pyramid to GPU-driven renderer
        gpuDrivenRenderer->updateHiZPyramid(
            camera->hiZBuffer->getHiZImageView(),
            camera->hiZBuffer->getHiZSampler(),
            camera->hiZBuffer->getMipLevels()
        );
    }

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

    void RenderPassHandler::setDebugCameraMatrices(const glm::mat4& view, const glm::mat4& projection)
    {
        currentView = view;
        currentProjection = projection;
    }

    // Camera management
    occlusion::CameraRenderData* RenderPassHandler::createCamera(occlusion::CameraId id, bool enableOcclusion)
    {
        return cameraOcclusionManager->createCamera(id, enableOcclusion);
    }

    occlusion::CameraRenderData* RenderPassHandler::getCamera(occlusion::CameraId id)
    {
        return cameraOcclusionManager->getCamera(id);
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

    bool RenderPassHandler::isHiZInitialized(occlusion::CameraId cameraId) const
    {
        return cameraOcclusionManager->isHiZInitialized(cameraId);
    }

    void RenderPassHandler::initOcclusionCulling(occlusion::CameraId cameraId)
    {
        cameraOcclusionManager->initCameraOcclusionCulling(cameraId);
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

    std::vector<uint32_t> RenderPassHandler::getOcclusionVisibility(occlusion::CameraId cameraId)
    {
        return cameraOcclusionManager->getVisibilityResults(cameraId);
    }

    bool RenderPassHandler::isOcclusionCullingInitialized(occlusion::CameraId cameraId) const
    {
        return cameraOcclusionManager->isOcclusionInitialized(cameraId);
    }

    void RenderPassHandler::recreate()
    {
        iblRenderer->recreate();
        clearColor->recreate();

        if (meshPipelineInitialized)
        {
            meshPipeline->recreate();
        }

        if (debugRendererInitialized)
        {
            debugRenderer->recreate(meshPipeline->getRenderPass());
        }

        if (billboardPipelineInitialized)
        {
            billboardPipeline->recreate();
        }

        // Recreate Hi-Z for all cameras
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
    }

    void RenderPassHandler::cleanUp() const
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->cleanup();
        }

        if (cameraOcclusionManager)
        {
            cameraOcclusionManager->cleanup();
        }

        if (billboardPipelineInitialized)
        {
            billboardPipeline->cleanUp();
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

        meshPipeline->cleanUp();
        iblRenderer->cleanUp();
        clearColor->cleanUp();
    }

    void RenderPassHandler::draw(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const
    {
        clearColor->recordCommandBuffer(commandBuffer, imageIndex);
        iblRenderer->recordCommandBuffer(commandBuffer, imageIndex);

        // Determine if we need to run the mesh render pass (for meshes or debug rendering)
        bool hasDebugItems = debugRendererInitialized && debugRenderer->hasItemsToRender();
        bool needsMeshPass = meshPipelineInitialized && (!currentMeshDrawList.empty() || hasDebugItems);

        // Separate opaque and translucent meshes
        std::vector<mesh::MeshRenderData> opaqueObjects;
        std::vector<mesh::MeshRenderData> translucentObjects;

        for (const auto& mesh : currentMeshDrawList)
        {
            // Consider meshes translucent if they have alpha significantly < 1
            // Use threshold to handle floating point imprecision
            if (mesh.albedo.a < 0.99f)
            {
                translucentObjects.push_back(mesh);
            }
            else
            {
                opaqueObjects.push_back(mesh);
            }
        }

        // Always update GPU-driven scene data (even when empty to reset stats)
        if (gpuDrivenRendererInitialized && meshPipelineInitialized)
        {
            const mesh::MeshGPUCache& meshCache = meshPipeline->getMeshGPUCache();
            gpuDrivenRenderer->updateScene(
                opaqueObjects,
                meshCache,
                currentView,
                currentProjection,
                currentCameraPosition,
                currentNearPlane,
                currentFarPlane,
                currentTime
            );
        }

        if (needsMeshPass)
        {
            // Get mesh cache reference from mesh pipeline for merged buffer access
            const mesh::MeshGPUCache& meshCache = meshPipeline->getMeshGPUCache();

            render::DebugRenderer* debugRendererPtr = hasDebugItems ? debugRenderer.get() : nullptr;

            // GPU-driven render for opaque objects
            if (!opaqueObjects.empty() && gpuDrivenRendererInitialized && gpuDrivenRenderer->isEnabled())
            {
                // Update Hi-Z pyramid from previous frame (for occlusion culling)
                updateGPUDrivenHiZ();

                // Dispatch compute shader BEFORE render pass
                gpuDrivenRenderer->dispatchCompute(commandBuffer);

                // Get IBL descriptor set from mesh pipeline
                vk::DescriptorSet iblDescriptorSet = meshPipeline->getIBLDescriptorSet(imageIndex);

                // Begin the render pass for GPU-driven rendering
                meshPipeline->beginRenderPass(commandBuffer, imageIndex);

                // Draw commands INSIDE render pass
                gpuDrivenRenderer->renderDraw(commandBuffer, iblDescriptorSet);

                // Render translucent objects using CPU path within the same render pass
                if (!translucentObjects.empty())
                {
                    meshPipeline->renderTranslucentInPass(commandBuffer, imageIndex, translucentObjects, currentFrustum);
                }

                // Render debug items if any
                if (debugRendererPtr)
                {
                    debugRendererPtr->render(commandBuffer, currentMeshDrawList, currentView, currentProjection,
                        [this](const std::string& meshId) { return meshPipeline->getMesh(meshId); });
                }

                meshPipeline->endRenderPass(commandBuffer);
            }
            else if (!opaqueObjects.empty() || !translucentObjects.empty() || hasDebugItems)
            {
                // CPU fallback path - render all objects (opaque + translucent)
                // This is used when GPU-driven rendering is not available (e.g., material preview)
                std::vector<mesh::MeshRenderData> allObjects;
                allObjects.reserve(opaqueObjects.size() + translucentObjects.size());
                allObjects.insert(allObjects.end(), opaqueObjects.begin(), opaqueObjects.end());
                allObjects.insert(allObjects.end(), translucentObjects.begin(), translucentObjects.end());

                meshPipeline->recordCommandBuffer(commandBuffer, imageIndex, allObjects, currentFrustum,
                                                  debugRendererPtr, currentView, currentProjection);
            }
        }

        if (billboardPipelineInitialized && !currentBillboardDrawList.empty())
        {
            billboardPipeline->recordCommandBuffer(commandBuffer, imageIndex);
        }

        // Generate Hi-Z pyramid and run occlusion culling for the active camera
        occlusion::CameraId activeCameraId = cameraOcclusionManager->getActiveCameraId();

        if (cameraOcclusionManager->isHiZInitialized(activeCameraId))
        {
            cameraOcclusionManager->generateHiZ(activeCameraId, commandBuffer);

            if (cameraOcclusionManager->isOcclusionInitialized(activeCameraId))
            {
                cameraOcclusionManager->runOcclusionCulling(activeCameraId, commandBuffer);
            }
        }
    }
}
