#include "OffScreenController.hpp"
#include "../core/VulkanContext.hpp"
#include "../imguiPass/OffScreenViewPort.hpp"
#include "../render/IBL.hpp"
#include "../render/RenderPassHandler.hpp"
#include "../render/mesh/StaticMeshPipeline.hpp"
#include "../render/mesh/MeshTypes.hpp"
#include "../render/occlusion/CameraRenderData.hpp"
#include "../render/billboard/BillboardPipeline.hpp"
#include "../render/billboard/BillboardTypes.hpp"
#include "../render/mesh/FrustumDebugRenderer.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "resource/ResourceManager.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include "../../services/events/EventTypes.hpp"
#include "../../services/events/MaterialEvents.hpp"
#include "print/Logger.hpp"

namespace controllers
{
    OffScreenController::OffScreenController()
        : swapChain{ *core::VulkanContext::getSwapChain() }
        , device{ *core::VulkanContext::getDevice() }
        , offScreen{ std::make_unique<imguiPass::OffScreenViewPort>(device, swapChain) }
    {
    }

    OffScreenController::~OffScreenController()
    {
        // Unsubscribe from material notifications
        if (materialSavedSubscription && materialSavedSubscription->isValid()) {
            events::EventDispatcher::instance().unsubscribe(*materialSavedSubscription);
        }
    }

    void OffScreenController::init()
    {
        offScreen->init();

        // Set up BVH mesh bounds callback
        sceneBVH.setMeshBoundsCallback([this](const std::string& meshPath) -> const math::AABB* {
            auto* meshPipeline = offScreen->getRenderPassHandler()->getMeshPipeline();
            if (meshPipeline)
            {
                return meshPipeline->getMeshBoundingBox(meshPath);
            }
            return nullptr;
        });

        auto token = events::EventDispatcher::instance().subscribe<events::material::MaterialFileSavedNotification>(
            [this](const events::material::MaterialFileSavedNotification& notification) {

                resource::ResourceManager::invalidateMaterialCache(notification.materialPath);

                // Invalidate GPU shader/pipeline cache
                auto* renderHandler = offScreen->getRenderPassHandler();
                if (renderHandler && renderHandler->isMeshPipelineInitialized()) {
                    renderHandler->getMeshPipeline()->invalidateMaterialCache(notification.materialPath);
                }
            });
        materialSavedSubscription = std::make_unique<events::SubscriptionToken>(token);
    }

    void OffScreenController::cleanUp() const
    {
        offScreen->cleanUp();
    }

    void OffScreenController::iblSet(std::string_view iblPath)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        
        renderHandler->getIBL()->init(iblPath);

        // If mesh pipeline was initialized, reinitialize it with the new IBL textures
        if (renderHandler->isMeshPipelineInitialized())
        {
            renderHandler->reinitMeshPipelineWithIBL();
        }
    }

    void OffScreenController::iblSetCameraMatrices(const glm::mat4& view, const glm::mat4& projection)
    {
        render::IBL* ibl = offScreen->getRenderPassHandler()->getIBL();
        ibl->setCameraMatrices(view, projection);
    }

    void OffScreenController::iblRemove()
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        
        renderHandler->getIBL()->remove();

        // If mesh pipeline was initialized with IBL textures, reinitialize with defaults
        if (renderHandler->isMeshPipelineInitialized())
        {
            renderHandler->reinitMeshPipelineWithDefaults();
        }
    }

    std::string OffScreenController::meshLoad(std::string_view meshPath)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();

        // Initialize mesh pipeline if IBL is ready but mesh pipeline isn't initialized yet
        renderHandler->initMeshPipeline();

        auto* meshPipeline = renderHandler->getMeshPipeline();
        if (!meshPipeline)
        {
            return "";
        }

        return meshPipeline->loadMesh(meshPath);
    }

    void OffScreenController::meshUnload(const std::string& meshId)
    {
        auto* meshPipeline = offScreen->getRenderPassHandler()->getMeshPipeline();
        if (meshPipeline)
        {
            meshPipeline->unloadMesh(meshId);
        }
    }

    void OffScreenController::meshUpdateCamera(const glm::mat4& view, const glm::mat4& projection,
                                               const glm::vec3& cameraPos, float time)
    {
        // Backward compatible: update main camera
        meshUpdateCamera(render::occlusion::MAIN_CAMERA_ID, view, projection, cameraPos, time);
    }

    void OffScreenController::meshUpdateCamera(render::occlusion::CameraId cameraId,
                                               const glm::mat4& view, const glm::mat4& projection,
                                               const glm::vec3& cameraPos, float time)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();

        // Update mesh pipeline UBO only for the active camera (the one being rendered)
        if (cameraId == renderHandler->getActiveCameraId() && renderHandler->isMeshPipelineInitialized())
        {
            renderHandler->getMeshPipeline()->updateCameraUBO(view, projection, cameraPos, time);
            
            if (!renderHandler->isDebugRendererInitialized())
            {
                renderHandler->initDebugRenderer();
            }
        }
        
        renderHandler->setDebugCameraMatrices(view, projection);
        
        if (renderHandler->isBillboardPipelineInitialized())
        {
            renderHandler->getBillboardPipeline()->updateCameraUBO(view, projection, cameraPos);
        }

        // Get or create camera data
        auto* cameraManager = renderHandler->getCameraOcclusionManager();
        auto* cameraData = cameraManager->getCamera(cameraId);
        if (!cameraData)
        {
            return;
        }

        // Update camera's frustum
        cameraData->frustum.extractFromMatrix(projection * view);
        cameraData->viewProj = projection * view;
        cameraData->nearPlane = currentNearPlane;

        // Update occlusion camera data
        cameraManager->updateCamera(cameraId, projection * view, currentNearPlane);

        // Initialize occlusion culling for this camera if not already done
        if (cameraData->useOcclusionCulling && !cameraData->occlusionInitialized)
        {
            if (cameraManager->isHiZInitialized(cameraId))
            {
                cameraManager->initCameraOcclusionCulling(cameraId);
            }
        }

        // Keep backward compatibility for main camera ready flag
        if (cameraId == render::occlusion::MAIN_CAMERA_ID)
        {
            occlusionCullingReady = cameraData->occlusionInitialized;
            currentViewProj = projection * view;
            currentFrustum = cameraData->frustum;
        }
    }

    bool OffScreenController::isMeshLoaded(const std::string& meshPath) const
    {
        auto* meshPipeline = offScreen->getRenderPassHandler()->getMeshPipeline();
        return meshPipeline && meshPipeline->isMeshLoaded(meshPath);
    }

    std::vector<std::string> OffScreenController::getLoadedMeshes() const
    {
        auto* meshPipeline = offScreen->getRenderPassHandler()->getMeshPipeline();
        if (!meshPipeline)
        {
            return {};
        }
        return meshPipeline->getLoadedMeshIds();
    }

    std::optional<services::MeshBounds> OffScreenController::getMeshBoundingBox(const std::string& meshPath) const
    {
        auto* meshPipeline = offScreen->getRenderPassHandler()->getMeshPipeline();
        if (!meshPipeline)
        {
            return std::nullopt;
        }

        const math::AABB* aabb = meshPipeline->getMeshBoundingBox(meshPath);
        if (!aabb)
        {
            return std::nullopt;
        }

        services::MeshBounds bounds;
        bounds.min = aabb->min;
        bounds.max = aabb->max;
        return bounds;
    }

    void OffScreenController::prepareCameras()
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        auto* cameraManager = renderHandler->getCameraOcclusionManager();

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::CameraComponent, components::WorldTransformComponent>();

        for (auto entity : view)
        {
            auto& camComp = view.get<components::CameraComponent>(entity);

            // Register camera with occlusion system if not already registered
            // Note: We only register scene cameras here, we don't update their matrices
            // because the editor camera (main camera 0) is controlled by EditorCamera/ViewPort
            // Scene cameras will be used when switching to game/runtime mode
            if (!camComp.isRegistered)
            {
                loggerInfo("Registering scene camera {} (entity {}) with occlusion culling {}",
                          camComp.cameraId, static_cast<uint32_t>(entity),
                          camComp.enableOcclusionCulling ? "enabled" : "disabled");
                // Scene cameras get registered but don't override the main editor camera
                // They'll be used in runtime mode when setActiveCamera is called
                createCamera(camComp.cameraId, camComp.enableOcclusionCulling);
                camComp.isRegistered = true;
            }
            // Don't call meshUpdateCamera here - let the editor camera control rendering
            // Scene camera matrices will be updated when they become the active camera
        }
    }

    void OffScreenController::prepareFrameMeshes()
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        auto* meshPipeline = renderHandler->getMeshPipeline();

        if (!meshPipeline)
        {
            renderHandler->setMeshDrawList({});
            return;
        }

        // Get active camera's frustum for culling
        auto* cameraManager = renderHandler->getCameraOcclusionManager();
        auto* activeCamera = cameraManager->getActiveCamera();
        const math::Frustum* activeFrustum = activeCamera ? &activeCamera->frustum : nullptr;
        bool frustumReady = activeFrustum && activeFrustum->isInitialized();

        std::vector<render::mesh::MeshRenderData> meshDrawList;
        auto& registry = scene::EntityRegistry::getRegistry();

        // Check if any transforms are dirty - mark BVH dirty when entities move
        // Use cooldown to avoid rebuilding every frame during continuous movement
        static int bvhCooldown = 0;

        if (bvhCooldown > 0)
        {
            --bvhCooldown;
        }
        else if (!sceneBVH.isDirty())
        {
            auto transformView = registry.view<components::TransformComponent, components::MeshComponent>();
            for (auto entity : transformView)
            {
                const auto& transform = transformView.get<components::TransformComponent>(entity);
                if (transform.isDirty)
                {
                    sceneBVH.markDirty();
                    break;
                }
            }
        }

        // Rebuild BVH only when marked dirty and frustum is ready
        if (sceneBVH.isDirty() && frustumReady)
        {
            sceneBVH.rebuild();
            bvhCooldown = 30;  // Wait ~0.5 sec before checking again
        }

        // Use BVH for spatial culling if available
        if (sceneBVH.isBuilt() && frustumReady)
        {
            // Query BVH for visible entities using active camera's frustum
            std::vector<uint32_t> visibleEntities;
            sceneBVH.queryFrustum(*activeFrustum, visibleEntities);

            // Process only visible entities
            for (uint32_t entityId : visibleEntities)
            {
                auto entity = static_cast<entt::entity>(entityId);

                if (!registry.valid(entity))
                {
                    continue;
                }

                const auto& meshComp = registry.get<components::MeshComponent>(entity);
                const auto& worldTransform = registry.get<components::WorldTransformComponent>(entity);

                if (meshComp.meshPath.empty() || !meshPipeline->isMeshLoaded(meshComp.meshPath))
                {
                    continue;
                }

                render::mesh::MeshRenderData renderData;
                renderData.meshPath = meshComp.meshPath;
                renderData.modelMatrix = worldTransform.worldMatrix;

                // Default PBR values
                renderData.albedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
                renderData.metallic = 0.0f;
                renderData.roughness = 0.5f;
                renderData.ao = 1.0f;
                renderData.emission = 0.0f;
                renderData.showBoundingBox = meshComp.showBoundingBox;

                // Check for MaterialComponent
                if (registry.all_of<components::MaterialComponent>(entity))
                {
                    const auto& materialComp = registry.get<components::MaterialComponent>(entity);
                    renderData.defaultMaterialPath = materialComp.defaultMaterial;

                    for (const auto& [submeshName, materialPath] : materialComp.subMeshMaterials)
                    {
                        render::mesh::SubMeshMaterialInfo matInfo;
                        matInfo.materialPath = materialPath;
                        renderData.submeshMaterials[submeshName] = matInfo;
                    }
                }

                meshDrawList.push_back(renderData);
            }
        }
        else
        {
            // Fallback: iterate all entities (BVH not ready or no frustum)
            auto view = registry.view<components::MeshComponent, components::WorldTransformComponent>();

            for (auto entity : view)
            {
                const auto& meshComp = view.get<components::MeshComponent>(entity);
                const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

                if (meshComp.meshPath.empty())
                {
                    continue;
                }

                if (!meshPipeline->isMeshLoaded(meshComp.meshPath))
                {
                    continue;
                }

                // Frustum culling fallback using active camera's frustum
                if (frustumReady)
                {
                    const math::AABB* boundingBox = meshPipeline->getMeshBoundingBox(meshComp.meshPath);
                    if (boundingBox && !activeFrustum->intersectsAABB(*boundingBox, worldTransform.worldMatrix))
                    {
                        continue;
                    }
                }

                render::mesh::MeshRenderData renderData;
                renderData.meshPath = meshComp.meshPath;
                renderData.modelMatrix = worldTransform.worldMatrix;

                renderData.albedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
                renderData.metallic = 0.0f;
                renderData.roughness = 0.5f;
                renderData.ao = 1.0f;
                renderData.emission = 0.0f;
                renderData.showBoundingBox = meshComp.showBoundingBox;

                if (registry.all_of<components::MaterialComponent>(entity))
                {
                    const auto& materialComp = registry.get<components::MaterialComponent>(entity);
                    renderData.defaultMaterialPath = materialComp.defaultMaterial;

                    for (const auto& [submeshName, materialPath] : materialComp.subMeshMaterials)
                    {
                        render::mesh::SubMeshMaterialInfo matInfo;
                        matInfo.materialPath = materialPath;
                        renderData.submeshMaterials[submeshName] = matInfo;
                    }
                }

                meshDrawList.push_back(renderData);
            }
        }

        renderHandler->setMeshDrawList(std::move(meshDrawList));
        renderHandler->setCurrentFrustum(&currentFrustum);

        // Update occlusion culling data for next frame
        updateOcclusionCullingData();
    }

    void OffScreenController::rebuildBVH()
    {
        sceneBVH.rebuild();
    }

    void OffScreenController::markBVHDirty()
    {
        sceneBVH.markDirty();
    }

    void OffScreenController::createCamera(render::occlusion::CameraId id, bool enableOcclusion)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        renderHandler->createCamera(id, enableOcclusion);
    }

    void OffScreenController::removeCamera(render::occlusion::CameraId id)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        renderHandler->removeCamera(id);
    }

    void OffScreenController::setActiveCamera(render::occlusion::CameraId id)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        auto previousId = renderHandler->getActiveCameraId();
        if (previousId != id)
        {
            loggerInfo("Switching active camera from {} to {}", previousId, id);
        }
        renderHandler->setActiveCamera(id);
    }

    render::occlusion::CameraId OffScreenController::getActiveCameraId() const
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        return renderHandler->getActiveCameraId();
    }

    void OffScreenController::prepareFrameBillboards()
    {
        auto* renderHandler = offScreen->getRenderPassHandler();

        // Lazy initialize billboard pipeline if needed
        renderHandler->initBillboardPipeline();

        if (!showBillboardIcons || !renderHandler->isBillboardPipelineInitialized())
        {
            renderHandler->setBillboardDrawList({});
            return;
        }

        std::vector<render::billboard::BillboardRenderData> billboardDrawList;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::BillboardComponent, components::WorldTransformComponent>();

        for (auto entity : view)
        {
            const auto& billboard = view.get<components::BillboardComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            // Skip non-editor billboards in editor mode
            if (!billboard.editorOnly)
            {
                continue;
            }

            render::billboard::BillboardRenderData renderData;
            renderData.worldPosition = glm::vec3(worldTransform.worldMatrix[3]);
            renderData.atlasIndex = billboard.getEffectiveAtlasIndex();
            renderData.size = billboard.size;
            renderData.sizeMode = static_cast<uint32_t>(billboard.sizeMode);
            renderData.entityId = static_cast<uint32_t>(entity);
            renderData.colorTint = billboard.colorTint;

            billboardDrawList.push_back(renderData);
        }

        renderHandler->setBillboardDrawList(std::move(billboardDrawList));
    }

    void OffScreenController::prepareFrameCameraFrustums()
    {
        auto* renderHandler = offScreen->getRenderPassHandler();

        std::vector<render::mesh::CameraFrustumRenderData> frustumDrawList;
        
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::CameraComponent, components::WorldTransformComponent>();

        for (auto entity : view)
        {
            const auto& cameraComp = view.get<components::CameraComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            // Skip cameras without frustum visualization enabled
            if (!cameraComp.showFrustum)
            {
                continue;
            }

            render::mesh::CameraFrustumRenderData renderData;
            renderData.projectionMatrix = cameraComp.projectionMatrix;
            renderData.worldMatrix = worldTransform.worldMatrix;
            renderData.showFrustum = cameraComp.showFrustum;

            frustumDrawList.push_back(renderData);
        }

        // If we have frustums to render, ensure mesh pipeline and debug renderer are initialized
        if (!frustumDrawList.empty())
        {
            if (!renderHandler->isMeshPipelineInitialized())
            {
                renderHandler->initMeshPipeline();
            }
            if (!renderHandler->isDebugRendererInitialized())
            {
                renderHandler->initDebugRenderer();
            }
        }

        renderHandler->setCameraFrustumDrawList(std::move(frustumDrawList));
    }

    bool OffScreenController::loadBillboardAtlas(const std::string& atlasPath)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();

        // Initialize billboard pipeline if not already done
        renderHandler->initBillboardPipeline();

        auto* billboardPipeline = renderHandler->getBillboardPipeline();
        if (!billboardPipeline)
        {
            return false;
        }

        return billboardPipeline->loadAtlas(atlasPath);
    }

    void* OffScreenController::render()
    {
        return offScreen->render();
    }

    void OffScreenController::updateOcclusionCullingData()
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        auto* cameraManager = renderHandler->getCameraOcclusionManager();
        auto* activeCamera = cameraManager->getActiveCamera();

        // Check if active camera has occlusion culling enabled and initialized
        if (!activeCamera || !activeCamera->useOcclusionCulling || !activeCamera->occlusionInitialized)
        {
            return;
        }

        auto* meshPipeline = renderHandler->getMeshPipeline();
        if (!meshPipeline)
        {
            return;
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::MeshComponent, components::WorldTransformComponent>();

        std::vector<render::occlusion::GPUObjectData> objectData;
        objectData.reserve(view.size_hint());

        for (auto entity : view)
        {
            const auto& meshComp = view.get<components::MeshComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            if (meshComp.meshPath.empty())
            {
                continue;
            }

            const math::AABB* localAABB = meshPipeline->getMeshBoundingBox(meshComp.meshPath);
            if (!localAABB || !localAABB->isValid())
            {
                continue;
            }

            render::occlusion::GPUObjectData obj;
            obj.aabbMin = glm::vec4(localAABB->min, static_cast<float>(entity));
            obj.aabbMax = glm::vec4(localAABB->max, 0.0f);
            obj.modelMatrix = worldTransform.worldMatrix;

            objectData.push_back(obj);
        }

        if (!objectData.empty())
        {
            render::occlusion::CameraId activeCameraId = cameraManager->getActiveCameraId();
            renderHandler->updateOcclusionObjects(activeCameraId, objectData);
            renderHandler->updateOcclusionCamera(activeCameraId, activeCamera->viewProj, activeCamera->nearPlane);
        }
    }

    services::CullingDebugStats OffScreenController::getCullingStats() const
    {
        services::CullingDebugStats stats;

        auto* renderHandler = offScreen->getRenderPassHandler();
        auto* cameraManager = renderHandler->getCameraOcclusionManager();

        if (!cameraManager)
        {
            return stats;
        }

        stats.activeCameraId = cameraManager->getActiveCameraId();

        // Get total mesh entities count
        auto& registry = scene::EntityRegistry::getRegistry();
        uint32_t totalMeshEntities = static_cast<uint32_t>(registry.view<components::MeshComponent>().size());

        // Iterate over all registered cameras
        for (const auto& [cameraId, cameraData] : cameraManager->getAllCameras())
        {
            services::CameraCullingStats camStats;
            camStats.cameraId = cameraId;
            camStats.isActive = (cameraId == stats.activeCameraId);
            camStats.occlusionEnabled = cameraData->useOcclusionCulling;
            camStats.occlusionInitialized = cameraData->occlusionInitialized;
            camStats.frustumReady = cameraData->frustum.isInitialized();
            camStats.bvhBuilt = sceneBVH.isBuilt();
            camStats.totalMeshEntities = totalMeshEntities;

            // Get visibility results for this camera if occlusion is active
            if (camStats.occlusionInitialized && cameraData->occlusionManager)
            {
                auto visibilityResults = cameraManager->getVisibilityResults(cameraId);
                if (!visibilityResults.empty())
                {
                    uint32_t visibleCount = 0;
                    for (uint32_t v : visibilityResults)
                    {
                        if (v != 0) ++visibleCount;
                    }
                    camStats.visibleAfterOcclusionCull = visibleCount;
                    camStats.occludedCount = static_cast<uint32_t>(visibilityResults.size()) - visibleCount;
                }
            }

            // Estimate visible after frustum cull (approximate - uses total if no occlusion)
            camStats.visibleAfterFrustumCull = camStats.visibleAfterOcclusionCull > 0
                ? camStats.visibleAfterOcclusionCull + camStats.occludedCount
                : totalMeshEntities;

            stats.cameraStats.push_back(camStats);
        }

        return stats;
    }
}
