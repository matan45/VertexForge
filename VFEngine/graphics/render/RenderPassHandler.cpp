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
#include "text/TextPipeline.hpp"
#include "text/TextTypes.hpp"
#include "ui/UIRenderPipeline.hpp"
#include "ui/UIRenderTypes.hpp"
#include "ui/UITextPipeline.hpp"
#include "ui/UITextRenderTypes.hpp"
#include "occlusion/CameraOcclusionManager.hpp"
#include "tools/AudioSphereDebugRenderer.hpp"
#include "tools/PhysicsDebugRenderer.hpp"
#include "tools/LightGizmoDebugRenderer.hpp"
#include "tools/ClusterDebugRenderer.hpp"
#include "gpudriven/GPUDrivenRenderer.hpp"
#include "gpudriven/TerrainRaycastPipeline.hpp"
#include "postprocess/PostProcessPipeline.hpp"
#include "material/MaterialTextureCache.hpp"
#include "../../services/providers/IVFXRuntimeProvider.hpp"
#include "../../services/providers/ITerrainRenderProvider.hpp"
#include "terrain/TerrainTile.hpp"
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
        , textPipeline{std::make_unique<text::TextPipeline>(device, swapChain, offscreenResources)}
        , uiPipeline{std::make_unique<ui::UIRenderPipeline>(device, swapChain, offscreenResources)}
        , uiTextPipeline{std::make_unique<ui::UITextPipeline>(device, swapChain, offscreenResources, textPipeline->getFontCache())}
        , cameraOcclusionManager{std::make_unique<occlusion::CameraOcclusionManager>(device, swapChain)}
        , debugRenderer{std::make_unique<DebugRenderer>(device, swapChain)}
        , gpuDrivenRenderer{std::make_unique<gpudriven::GPUDrivenRenderer>(device, swapChain)}
        , terrainRaycastPipeline{std::make_unique<gpudriven::TerrainRaycastPipeline>(device)}
        , postProcessPipeline{std::make_unique<postprocess::PostProcessPipeline>(device, swapChain, offscreenResources)}
    {
    }

    RenderPassHandler::~RenderPassHandler()
    {
        if (materialChangeCallbackId)
        {
            material::MaterialManager::instance().unregisterChangeCallback(materialChangeCallbackId);
        }
    }

    void RenderPassHandler::init()
    {
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
                        std::erase_if(customShaderRequirementCache, [](const auto& pair)
                        {
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

    void RenderPassHandler::initTextPipeline()
    {
        if (textPipelineInitialized)
        {
            return;
        }

        textPipeline->init();
        textPipelineInitialized = true;
    }

    void RenderPassHandler::setTextDrawList(std::vector<text::TextRenderData>&& textEntities)
    {
        currentTextDrawList = std::move(textEntities);
        if (textPipelineInitialized && textPipeline)
        {
            textPipeline->setTextDrawList(currentTextDrawList);
        }
    }

    void RenderPassHandler::appendTextDrawList(std::vector<text::TextRenderData>&& textEntities)
    {
        currentTextDrawList.insert(currentTextDrawList.end(),
                                   std::make_move_iterator(textEntities.begin()),
                                   std::make_move_iterator(textEntities.end()));
        if (textPipelineInitialized && textPipeline)
        {
            textPipeline->setTextDrawList(currentTextDrawList);
        }
    }

    void RenderPassHandler::initUIRenderPipeline()
    {
        if (uiPipelineInitialized)
        {
            return;
        }

        uiPipeline->init();
        uiPipelineInitialized = true;
    }

    void RenderPassHandler::setUIImageDrawList(std::vector<ui::UIImageRenderData>&& images)
    {
        currentUIImageDrawList = std::move(images);
        if (uiPipelineInitialized && uiPipeline)
        {
            uiPipeline->setUIImageDrawList(currentUIImageDrawList);
        }
    }

    void RenderPassHandler::initUITextPipeline()
    {
        if (uiTextPipelineInitialized)
        {
            return;
        }

        // Ensure TextPipeline is initialized (font cache must be ready)
        initTextPipeline();

        uiTextPipeline->init();
        uiTextPipelineInitialized = true;
    }

    void RenderPassHandler::setUITextDrawList(std::vector<ui::UITextRenderData>&& labels)
    {
        currentUITextDrawList = std::move(labels);
        if (uiTextPipelineInitialized && uiTextPipeline)
        {
            uiTextPipeline->setUITextDrawList(currentUITextDrawList);
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

    void RenderPassHandler::setDeletionQueue(core::DeferredDeletionQueue* queue)
    {
        if (gpuDrivenRenderer && gpuDrivenRendererInitialized)
        {
            gpuDrivenRenderer->setDeletionQueue(queue);
        }

        if (textPipeline)
        {
            textPipeline->setDeletionQueue(queue);
        }
        if (uiPipeline)
        {
            uiPipeline->setDeletionQueue(queue);
        }
        if (uiTextPipeline)
        {
            uiTextPipeline->setDeletionQueue(queue);
        }
        if (billboardPipeline)
        {
            billboardPipeline->setDeletionQueue(queue);
        }
    }

    void RenderPassHandler::readBackLightOcclusionResults()
    {
        if (gpuDrivenRenderer && gpuDrivenRendererInitialized)
        {
            gpuDrivenRenderer->readBackLightOcclusionResults();
        }
    }

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

    void RenderPassHandler::setVFXRuntimeProvider(services::IVFXRuntimeProvider* provider)
    {
        vfxRuntimeProvider = provider;
    }

    void RenderPassHandler::setTerrainRenderProvider(services::ITerrainRenderProvider* provider)
    {
        terrainRenderProvider = provider;

        if (provider && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setTileDataLoader(
                [provider](terrain::TerrainTile& tile, uint8_t lod) -> bool {
                    return provider->ensureTileLODData(tile, lod);
                });
            gpuDrivenRenderer->setTileRAMEvictor(
                [provider](terrain::TerrainTile& tile) {
                    provider->releaseTileRAMData(tile);
                });
        }
    }

    void RenderPassHandler::clearTerrainData()
    {
        if (gpuDrivenRenderer)
        {
            gpuDrivenRenderer->clearTerrainData();
        }
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

        if (textPipelineInitialized)
        {
            textPipeline->recreate();
        }

        if (uiPipelineInitialized)
        {
            uiPipeline->recreate();
        }

        if (uiTextPipelineInitialized)
        {
            uiTextPipeline->recreate();
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

        if (terrainRaycastPipeline && terrainRaycastPipeline->isInitialized())
        {
            terrainRaycastPipeline->updateDepthImageView(offscreenResources.depthImage.depthImageView);
        }

        if (postProcessPipeline && postProcessPipeline->isInitialized())
        {
            postProcessPipeline->recreate();
        }
    }

    void RenderPassHandler::cleanUp() const
    {
        if (terrainRaycastPipeline)
        {
            terrainRaycastPipeline->cleanup();
        }

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

        if (textPipelineInitialized)
        {
            textPipeline->cleanUp();
        }

        if (uiPipelineInitialized)
        {
            uiPipeline->cleanUp();
        }

        if (uiTextPipelineInitialized)
        {
            uiTextPipeline->cleanUp();
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

        if (postProcessPipeline)
        {
            postProcessPipeline->cleanup();
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
        bool hasVFX = vfxRuntimeProvider && vfxRuntimeProvider->isInitialized() && vfxRuntimeProvider->
            getInstanceCount() > 0;
        bool hasCustomShaderMeshes = !customShaderMeshDrawList.empty();

        if (hasVFX)
        {
            vfxRuntimeProvider->setCamera(currentView, currentProjection, currentCameraPosition, currentTime);
        }
        bool hasTerrainToRender = gpuDrivenRenderer && gpuDrivenRenderer->isTerrainRenderingEnabled() &&
                                  terrainRenderProvider && terrainRenderProvider->hasActiveTerrain();

        bool needsMeshPass = meshPipelineInitialized && (!currentMeshDrawList.empty() || hasCustomShaderMeshes ||
            hasDebugItems || hasVFX || hasTerrainToRender);

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

            if (terrainRenderProvider && terrainRenderProvider->hasActiveTerrain() && currentFrustum)
            {
                auto visibleTiles = terrainRenderProvider->getVisibleTiles(*currentFrustum, currentCameraPosition);
                auto matPath = terrainRenderProvider->getTerrainMaterialPath();
                gpuDrivenRenderer->updateTerrain(visibleTiles, currentCameraPosition, matPath);
            }
        }

        if (needsMeshPass)
        {
            render::DebugRenderer* debugRendererPtr = hasDebugItems ? debugRenderer.get() : nullptr;

            if (hasVFX)
            {
                vfxRuntimeProvider->recordComputeCommands(commandBuffer);
            }

            bool hasMeshesToRender = !currentMeshDrawList.empty();

            if ((hasMeshesToRender || hasTerrainToRender) && gpuDrivenRendererInitialized && gpuDrivenRenderer->isEnabled())
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

        if (textPipelineInitialized && !currentTextDrawList.empty())
        {
            textPipeline->recordCommandBuffer(commandBuffer, imageIndex);
        }

        occlusion::CameraId activeCameraId = cameraOcclusionManager->getActiveCameraId();

        if (cameraOcclusionManager->isHiZInitialized(activeCameraId))
        {
            cameraOcclusionManager->generateHiZ(activeCameraId, commandBuffer);

            if (cameraOcclusionManager->isOcclusionInitialized(activeCameraId))
            {
                cameraOcclusionManager->runOcclusionCulling(activeCameraId, commandBuffer);
            }

            if (terrainRaycastPipeline && terrainRaycastPipeline->isInitialized())
            {
                // Transition depth image to shader-read for the raycast compute shader.
                // generateHiZ() transitions it back to attachment layout, so we must re-transition.
                vk::ImageMemoryBarrier toShaderRead{};
                toShaderRead.oldLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
                toShaderRead.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                toShaderRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toShaderRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toShaderRead.image = offscreenResources.depthImage.depthImage;
                toShaderRead.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth |
                    vk::ImageAspectFlagBits::eStencil;
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
                toAttachment.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth |
                    vk::ImageAspectFlagBits::eStencil;
                toAttachment.subresourceRange.baseMipLevel = 0;
                toAttachment.subresourceRange.levelCount = 1;
                toAttachment.subresourceRange.baseArrayLayer = 0;
                toAttachment.subresourceRange.layerCount = 1;
                toAttachment.srcAccessMask = vk::AccessFlagBits::eShaderRead;
                toAttachment.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead |
                    vk::AccessFlagBits::eDepthStencilAttachmentWrite;

                commandBuffer.pipelineBarrier(
                    vk::PipelineStageFlagBits::eComputeShader,
                    vk::PipelineStageFlagBits::eEarlyFragmentTests,
                    {}, {}, {}, toAttachment);
            }
        }

        // Pass camera data for depth-based effects (DoF)
        postProcessPipeline->setCameraData(currentNearPlane, currentFarPlane,
                                           currentCameraPosition, currentView, currentTime);

        // Compute sun screen position for god rays
        if (gpuDrivenRendererInitialized)
        {
            auto* lbm = gpuDrivenRenderer->getLightBufferManager();
            auto sunDir = lbm->getFirstDirectionalLightDirection();
            if (sunDir)
            {
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
            else
            {
                postProcessPipeline->setSunData({0.5f, 0.5f}, false);
            }
        }

        // Post-processing chain (ping-pong effects, then blit back to scene color)
        postProcessPipeline->execute(commandBuffer, imageIndex);

        // UI overlay pass (after post-processing, renders on top of everything)
        if (uiPipelineInitialized && !currentUIImageDrawList.empty())
        {
            uiPipeline->recordCommandBuffer(commandBuffer, imageIndex);
        }

        // UI text overlay (after UI images, text renders on top)
        if (uiTextPipelineInitialized && !currentUITextDrawList.empty())
        {
            uiTextPipeline->recordCommandBuffer(commandBuffer, imageIndex);
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
