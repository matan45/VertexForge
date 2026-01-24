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
#include "occlusion/CameraOcclusionManager.hpp"
#include "tools/AudioSphereDebugRenderer.hpp"
#include "tools/PhysicsDebugRenderer.hpp"
#include "gpudriven/GPUDrivenRenderer.hpp"
#include "material/MaterialTextureCache.hpp"
#include "../../services/providers/IVFXRuntimeProvider.hpp"
#include "resource/ResourceManager.hpp"
#include "material/MaterialTypes.hpp"
#include "print/Logger.hpp"
#include <queue>
#include <unordered_set>

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
        // Note: Callback registration moved to init() for exception safety.
        // If constructor body threw after registering callback, destructor wouldn't
        // be called and the callback would leak.
    }

    RenderPassHandler::~RenderPassHandler()
    {
        if (materialChangeCallbackId) {
            material::MaterialManager::instance().unregisterChangeCallback(materialChangeCallbackId);
        }
    }

    void RenderPassHandler::init()
    {
        clearColor->init();

        // Register callback to invalidate custom shader cache when materials change
        // Done in init() rather than constructor for exception safety - if init() fails,
        // destructor will still be called and properly unregister the callback
        if (!materialChangeCallbackId) {
            materialChangeCallbackId = material::MaterialManager::instance().registerChangeCallback(
                [this](const std::string& materialPath) {
                    customShaderRequirementCache.erase(materialPath);

                    // When a parent material changes, invalidate all instance entries
                    // since we can't easily track which instances use this parent
                    if (!material::isInstanceFile(materialPath)) {
                        std::erase_if(customShaderRequirementCache, [](const auto& pair) {
                            return material::isInstanceFile(pair.first);
                        });
                    }
                });
        }
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

        if (vfxRuntimeProvider && !vfxRuntimeProvider->isInitialized())
        {
            vfxRuntimeProvider->init(meshPipeline->getRenderPass());
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

        vk::DescriptorSetLayout iblLayout = meshPipeline->getIBLDescriptorSetLayout();
        vk::RenderPass renderPass = meshPipeline->getRenderPass();

        gpuDrivenRenderer->init(iblLayout, renderPass);

        auto& texCache = meshPipeline->getMaterialTextureCache();
        gpuDrivenRenderer->setMaterialTextureCache(&texCache);

        if (texCache.hasDefaultTexture())
        {
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

        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->updateRenderPass(
                meshPipeline->getRenderPass(),
                meshPipeline->getIBLDescriptorSetLayout());
        }

        if (vfxRuntimeProvider && vfxRuntimeProvider->isInitialized())
        {
            vfxRuntimeProvider->recreate(meshPipeline->getRenderPass());
        }
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

        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->updateRenderPass(
                meshPipeline->getRenderPass(),
                meshPipeline->getIBLDescriptorSetLayout());
        }

        if (vfxRuntimeProvider && vfxRuntimeProvider->isInitialized())
        {
            vfxRuntimeProvider->recreate(meshPipeline->getRenderPass());
        }
    }

    void RenderPassHandler::setMeshDrawList(std::vector<mesh::MeshRenderData>&& meshes)
    {
        currentMeshDrawList.clear();
        customShaderMeshDrawList.clear();

        std::unordered_set<std::string> uniqueMaterials;
        for (const auto& mesh : meshes)
        {
            if (!mesh.defaultMaterialPath.empty())
            {
                uniqueMaterials.insert(mesh.defaultMaterialPath);
            }
            for (const auto& [submeshName, matInfo] : mesh.submeshMaterials)
            {
                if (!matInfo.materialPath.empty())
                {
                    uniqueMaterials.insert(matInfo.materialPath);
                }
            }
        }

        std::unordered_set<std::string> customShaderMaterials;
        for (const auto& matPath : uniqueMaterials)
        {
            if (materialRequiresCustomShader(matPath))
            {
                customShaderMaterials.insert(matPath);
            }
        }

        for (auto& mesh : meshes)
        {
            bool needsCustomShader = false;

            if (!mesh.defaultMaterialPath.empty() &&
                customShaderMaterials.contains(mesh.defaultMaterialPath))
            {
                needsCustomShader = true;
            }

            if (!needsCustomShader)
            {
                for (const auto& [submeshName, matInfo] : mesh.submeshMaterials)
                {
                    if (!matInfo.materialPath.empty() &&
                        customShaderMaterials.contains(matInfo.materialPath))
                    {
                        needsCustomShader = true;
                        break;
                    }
                }
            }

            if (needsCustomShader)
            {
                customShaderMeshDrawList.push_back(std::move(mesh));
            }
            else
            {
                currentMeshDrawList.push_back(std::move(mesh));
            }
        }

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
            for (const auto& mesh : customShaderMeshDrawList)
            {
                if (mesh.showBoundingBox)
                {
                    hasBoundingBoxes = true;
                    break;
                }
            }
            debugRenderer->setHasBoundingBoxes(hasBoundingBoxes);
        }

        combinedMeshDrawList.clear();
        combinedMeshDrawList.reserve(currentMeshDrawList.size() + customShaderMeshDrawList.size());
        combinedMeshDrawList.insert(combinedMeshDrawList.end(),
                                    currentMeshDrawList.begin(), currentMeshDrawList.end());
        combinedMeshDrawList.insert(combinedMeshDrawList.end(),
                                    customShaderMeshDrawList.begin(), customShaderMeshDrawList.end());
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

        if (!meshPipelineInitialized)
        {
            return;
        }

        debugRenderer->init(meshPipeline->getRenderPass());
        debugRendererInitialized = true;
    }

    void RenderPassHandler::setGPUDrivenCameraData(const glm::vec3& cameraPos, float nearPlane, float farPlane,
                                                   float time)
    {
        currentCameraPosition = cameraPos;
        currentNearPlane = nearPlane;
        currentFarPlane = farPlane;
        currentTime = time;
    }

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

    void RenderPassHandler::setPhysicsColliderDrawList(std::vector<mesh::PhysicsColliderRenderData>&& colliders)
    {
        if (debugRenderer)
        {
            debugRenderer->setPhysicsColliderDrawList(std::move(colliders));
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

    void RenderPassHandler::setVFXRuntimeProvider(services::IVFXRuntimeProvider* provider)
    {
        vfxRuntimeProvider = provider;
    }

    void RenderPassHandler::setDebugCameraMatrices(const glm::mat4& view, const glm::mat4& projection)
    {
        currentView = view;
        currentProjection = projection;
    }

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

    void RenderPassHandler::recreate()
    {
        iblRenderer->recreate();
        clearColor->recreate();

        if (meshPipelineInitialized)
        {
            meshPipeline->recreate();

            if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
            {
                gpuDrivenRenderer->updateRenderPass(meshPipeline->getRenderPass());
            }

            if (vfxRuntimeProvider && vfxRuntimeProvider->isInitialized())
            {
                vfxRuntimeProvider->recreate(meshPipeline->getRenderPass());
            }
        }

        if (debugRendererInitialized)
        {
            debugRenderer->recreate(meshPipeline->getRenderPass());
        }

        if (billboardPipelineInitialized)
        {
            billboardPipeline->recreate();
        }

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

        bool hasDebugItems = debugRendererInitialized && debugRenderer->hasItemsToRender();
        bool hasVFX = vfxRuntimeProvider && vfxRuntimeProvider->isInitialized() && vfxRuntimeProvider->getInstanceCount() > 0;
        bool hasCustomShaderMeshes = !customShaderMeshDrawList.empty();

        if (hasVFX)
        {
            vfxRuntimeProvider->setCamera(currentView, currentProjection, currentCameraPosition, currentTime);
        }
        bool needsMeshPass = meshPipelineInitialized && (!currentMeshDrawList.empty() || hasCustomShaderMeshes || hasDebugItems || hasVFX);

        if (gpuDrivenRendererInitialized && meshPipelineInitialized)
        {
            gpuDrivenRenderer->updateScene(
                currentMeshDrawList,
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
            render::DebugRenderer* debugRendererPtr = hasDebugItems ? debugRenderer.get() : nullptr;

            if (hasVFX)
            {
                vfxRuntimeProvider->recordComputeCommands(commandBuffer);
            }

            if (!currentMeshDrawList.empty() && gpuDrivenRendererInitialized && gpuDrivenRenderer->isEnabled())
            {
                updateGPUDrivenHiZ();

                gpuDrivenRenderer->dispatchCompute(commandBuffer);

                vk::DescriptorSet iblDescriptorSet = meshPipeline->getIBLDescriptorSet(imageIndex);

                meshPipeline->beginRenderPass(commandBuffer, imageIndex);

                gpuDrivenRenderer->renderDraw(commandBuffer, iblDescriptorSet);

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
            else if (!currentMeshDrawList.empty() || hasCustomShaderMeshes || hasDebugItems)
            {
                meshPipeline->recordCommandBuffer(commandBuffer, imageIndex, combinedMeshDrawList, currentFrustum,
                                                  debugRendererPtr, currentView, currentProjection);

                if (hasVFX)
                {
                    meshPipeline->beginRenderPass(commandBuffer, imageIndex);
                    vfxRuntimeProvider->recordDrawCommands(commandBuffer);
                    meshPipeline->endRenderPass(commandBuffer);
                }
            }
            else if (hasVFX)
            {
                meshPipeline->beginRenderPass(commandBuffer, imageIndex);
                vfxRuntimeProvider->recordDrawCommands(commandBuffer);
                meshPipeline->endRenderPass(commandBuffer);
            }
        }

        if (billboardPipelineInitialized && !currentBillboardDrawList.empty())
        {
            billboardPipeline->recordCommandBuffer(commandBuffer, imageIndex);
        }

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

    bool RenderPassHandler::materialRequiresCustomShader(const std::string& materialPath) const
    {
        if (materialPath.empty())
        {
            return false;
        }

        auto it = customShaderRequirementCache.find(materialPath);
        if (it != customShaderRequirementCache.end())
        {
            return it->second;
        }

        bool result = computeMaterialRequiresCustomShader(materialPath);
        customShaderRequirementCache[materialPath] = result;
        return result;
    }

    bool RenderPassHandler::computeMaterialRequiresCustomShader(const std::string& materialPath)
    {
        if (materialPath.empty())
        {
            return false;
        }

        std::string parentPath = materialPath;

        if (material::isInstanceFile(materialPath))
        {
            auto instanceData = resource::ResourceManager::loadMaterialInstance(materialPath);
            if (instanceData && !instanceData->parentMaterialPath.empty())
            {
                parentPath = instanceData->parentMaterialPath;
            }
            else
            {
                return false;
            }
        }

        auto matData = resource::ResourceManager::loadMaterial(parentPath);
        if (!matData)
        {
            return false;
        }

        uint32_t timeNodeId = 0;
        bool hasTimeNode = false;
        for (const auto& node : matData->graph.nodes)
        {
            if (node.type == material::NodeType::Time)
            {
                timeNodeId = node.id;
                hasTimeNode = true;
                break;
            }
        }

        if (!hasTimeNode)
        {
            return false;
        }

        std::unordered_set<uint32_t> visitedNodes;
        std::queue<uint32_t> nodesToVisit;
        nodesToVisit.push(timeNodeId);

        while (!nodesToVisit.empty())
        {
            uint32_t currentNodeId = nodesToVisit.front();
            nodesToVisit.pop();

            if (visitedNodes.contains(currentNodeId))
            {
                continue;
            }
            visitedNodes.insert(currentNodeId);

            for (const auto& link : matData->graph.links)
            {
                if (link.sourceNodeId == currentNodeId)
                {
                    for (const auto& node : matData->graph.nodes)
                    {
                        if (node.id == link.targetNodeId)
                        {
                            if (node.type == material::NodeType::PBROutput)
                            {
                                return true;
                            }

                            if (node.type == material::NodeType::TextureSample &&
                                link.targetPin == "UV")
                            {
                                return true;
                            }

                            nodesToVisit.push(link.targetNodeId);
                            break;
                        }
                    }
                }
            }
        }

        return false;
    }
}
