#include "OffScreenController.hpp"
#include "../core/VulkanContext.hpp"
#include "../imguiPass/OffScreenViewPort.hpp"
#include "../render/IBL.hpp"
#include "../render/RenderPassHandler.hpp"
#include "../render/mesh/StaticMeshPipeline.hpp"
#include "../render/mesh/MeshTypes.hpp"
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
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler->isMeshPipelineInitialized())
        {
            renderHandler->getMeshPipeline()->updateCameraUBO(view, projection, cameraPos, time);
        }

        // Update frustum for culling
        currentFrustum.extractFromMatrix(projection * view);
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

    void OffScreenController::prepareFrameMeshes()
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        auto* meshPipeline = renderHandler->getMeshPipeline();

        if (!meshPipeline)
        {
            renderHandler->setMeshDrawList({});
            return;
        }

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
        if (sceneBVH.isDirty() && currentFrustum.isInitialized())
        {
            sceneBVH.rebuild();
            bvhCooldown = 30;  // Wait ~0.5 sec before checking again
        }

        // Use BVH for spatial culling if available
        if (sceneBVH.isBuilt() && currentFrustum.isInitialized())
        {
            // Query BVH for visible entities
            std::vector<uint32_t> visibleEntities;
            sceneBVH.queryFrustum(currentFrustum, visibleEntities);

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

                // Frustum culling fallback
                if (currentFrustum.isInitialized())
                {
                    const math::AABB* boundingBox = meshPipeline->getMeshBoundingBox(meshComp.meshPath);
                    if (boundingBox && !currentFrustum.intersectsAABB(*boundingBox, worldTransform.worldMatrix))
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
    }

    void OffScreenController::rebuildBVH()
    {
        sceneBVH.rebuild();
    }

    void OffScreenController::markBVHDirty()
    {
        sceneBVH.markDirty();
    }

    void* OffScreenController::render()
    {
        return offScreen->render();
    }
}
