#include "OffScreenController.hpp"
#include "../core/VulkanContext.hpp"
#include "../render/OffScreenViewPort.hpp"
#include "../render/IBL.hpp"
#include "../render/RenderPassHandler.hpp"
#include "../render/mesh/StaticMeshPipeline.hpp"
#include "../render/mesh/MeshTypes.hpp"
#include "../render/occlusion/OcclusionCullingManager.hpp"
#include "../render/billboard/BillboardTypes.hpp"
#include "../render/billboard/BillboardPipeline.hpp"
#include "../render/mesh/FrustumDebugRenderer.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "material/MaterialTypes.hpp"
#include "resource/ResourceManager.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include "../../services/events/EventTypes.hpp"
#include "../../services/events/MaterialEvents.hpp"
#include "../../services/events/SceneEvents.hpp"
#include "print/Logger.hpp"

namespace controllers
{
    OffScreenController::OffScreenController()
        : swapChain{ *core::VulkanContext::getSwapChain() }
        , device{ *core::VulkanContext::getDevice() }
        , offScreen{ std::make_unique<render::OffScreenViewPort>(device, swapChain) }
    {
    }

    OffScreenController::~OffScreenController()
    {
        // Unsubscribe from notifications
        if (materialSavedSubscription && materialSavedSubscription->isValid()) {
            events::EventDispatcher::instance().unsubscribe(*materialSavedSubscription);
        }
        if (meshDataChangedSubscription && meshDataChangedSubscription->isValid()) {
            events::EventDispatcher::instance().unsubscribe(*meshDataChangedSubscription);
        }
        if (entityDeletedSubscription && entityDeletedSubscription->isValid()) {
            events::EventDispatcher::instance().unsubscribe(*entityDeletedSubscription);
        }
        if (entityStaticChangedSubscription && entityStaticChangedSubscription->isValid()) {
            events::EventDispatcher::instance().unsubscribe(*entityStaticChangedSubscription);
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

        // Subscribe to mesh data changes (mesh added/changed/removed on entity)
        // This is a structural change - requires full rebuild
        auto meshChangedToken = events::EventDispatcher::instance().subscribe<events::scene::MeshDataChangedNotification>(
            [this](const events::scene::MeshDataChangedNotification& notification) {
                // Check if entity is static or dynamic and mark appropriate tree for structural rebuild
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = static_cast<entt::entity>(notification.entity.id);
                if (registry.valid(entity) && registry.all_of<components::TransformComponent>(entity)) {
                    const auto& transform = registry.get<components::TransformComponent>(entity);
                    if (transform.isStatic) {
                        sceneBVH.markStaticDirty();
                    } else {
                        sceneBVH.markDynamicDirty();
                    }
                }
            });
        meshDataChangedSubscription = std::make_unique<events::SubscriptionToken>(meshChangedToken);

        // Subscribe to entity deleted notification to update BVH
        // This is a structural change - requires full rebuild
        auto deletedToken = events::EventDispatcher::instance().subscribe<events::scene::EntityDeletedNotification>(
            [this](const events::scene::EntityDeletedNotification& notification) {
                // Mark appropriate BVH for structural rebuild based on which tree the entity was in
                uint32_t entityId = static_cast<uint32_t>(notification.entity.id);
                if (sceneBVH.isStaticEntity(entityId)) {
                    sceneBVH.markStaticDirty();
                } else {
                    sceneBVH.markDynamicDirty();
                }
            });
        entityDeletedSubscription = std::make_unique<events::SubscriptionToken>(deletedToken);

       
        auto staticChangedToken = events::EventDispatcher::instance().subscribe<events::scene::EntityStaticChangedNotification>(
            [this](const events::scene::EntityStaticChangedNotification&) {
                // Defer rebuild - entity moves between static/dynamic trees
                // Both trees need structural rebuild, batched with other changes
                sceneBVH.markDirty();
            });
        entityStaticChangedSubscription = std::make_unique<events::SubscriptionToken>(staticChangedToken);
    }

    void OffScreenController::recreate()
    {
        offScreen->recreate();
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
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::CameraComponent, components::WorldTransformComponent>();

        for (auto entity : view)
        {
            auto& camComp = view.get<components::CameraComponent>(entity);
            if (!camComp.isRegistered)
            {
                createCamera(camComp.cameraId, camComp.enableOcclusionCulling);
                camComp.isRegistered = true;
            }
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
        auto* activeCamera = cameraManager->getCamera(cameraManager->getActiveCameraId());
        const math::Frustum* activeFrustum = activeCamera ? &activeCamera->frustum : nullptr;
        bool frustumReady = activeFrustum && activeFrustum->isInitialized();

        std::vector<render::mesh::MeshRenderData> meshDrawList;
        auto& registry = scene::EntityRegistry::getRegistry();

       
        if (sceneBVH.isStaticDirty() && frustumReady)
        {
            sceneBVH.rebuildStaticBVH();
        }
        
        static int dynamicBvhCooldown = 0;

        if (dynamicBvhCooldown > 0)
        {
            --dynamicBvhCooldown;
        }
        else if (!sceneBVH.needsDynamicRebuild())
        {
            // Only check dynamic entities
            auto dynamicView = registry.view<components::TransformComponent, components::MeshComponent>();
            for (auto entity : dynamicView)
            {
                const auto& transform = dynamicView.get<components::TransformComponent>(entity);
                
                if (!transform.isStatic && transform.isDirty)
                {
                    // Mark specific entity dirty for incremental refit
                    sceneBVH.markDynamicEntityDirty(static_cast<uint32_t>(entity));
                }
            }
        }

        // Update dynamic BVH
        if (sceneBVH.isDynamicDirty() && frustumReady)
        {
            sceneBVH.updateDynamicBVH();
            dynamicBvhCooldown = 5; 
        }

        // Use BVH for spatial culling if available
        if (sceneBVH.isBuilt() && frustumReady)
        {
            std::vector<uint32_t> visibleEntities;
            sceneBVH.queryFrustum(*activeFrustum, visibleEntities);
            
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
                // Skip debug rendering in Play mode or when debug rendering is disabled
                renderData.showBoundingBox = (playModeActive || !showDebugRendering) ? false : meshComp.showBoundingBox;

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
                // Skip debug rendering in Play mode or when debug rendering is disabled
                renderData.showBoundingBox = (playModeActive || !showDebugRendering) ? false : meshComp.showBoundingBox;

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
        sceneBVH.rebuildAll();
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

        if (playModeActive || !showBillboardIcons || !renderHandler->isBillboardPipelineInitialized())
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

        // Skip frustum visualization in Play mode or when debug rendering is disabled
        if (playModeActive || !showDebugRendering)
        {
            renderHandler->setCameraFrustumDrawList({});
            return;
        }

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
        auto* activeCamera = cameraManager->getCamera(cameraManager->getActiveCameraId());

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

            // Determine occlusion flags based on material blend mode
            uint32_t flags = render::occlusion::OcclusionFlags::None;

            // Check if entity has a material and get its blend mode
            if (registry.all_of<components::MaterialComponent>(entity))
            {
                const auto& matComp = registry.get<components::MaterialComponent>(entity);
                // Check default material first, then any submesh materials
                std::string materialToCheck = matComp.defaultMaterial;
                if (!materialToCheck.empty())
                {
                    material::BlendMode blendMode = meshPipeline->getMaterialBlendMode(materialToCheck);
                    if (blendMode != material::BlendMode::Opaque)
                    {
                        flags |= render::occlusion::OcclusionFlags::Transparent;
                    }
                }
            }

            render::occlusion::GPUObjectData obj;
            obj.aabbMin = glm::vec4(localAABB->min, static_cast<float>(entity));
            // Store flags as uint reinterpreted as float
            obj.aabbMax = glm::vec4(localAABB->max, glm::uintBitsToFloat(flags));
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

        // BVH statistics
        stats.staticBvhEntityCount = sceneBVH.getStaticEntityCount();
        stats.dynamicBvhEntityCount = sceneBVH.getDynamicEntityCount();
        stats.staticBvhNodeCount = sceneBVH.getStaticNodeCount();
        stats.dynamicBvhNodeCount = sceneBVH.getDynamicNodeCount();

        return stats;
    }
}
