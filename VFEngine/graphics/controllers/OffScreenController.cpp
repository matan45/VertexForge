#include "OffScreenController.hpp"
#include "../core/VulkanContext.hpp"
#include "../imguiPass/OffScreenViewPort.hpp"
#include "../render/IBL.hpp"
#include "../render/RenderPassHandler.hpp"
#include "../render/mesh/StaticMeshPipeline.hpp"
#include "../render/mesh/MeshTypes.hpp"
#include "../render/billboard/BillboardPipeline.hpp"
#include "../render/billboard/BillboardTypes.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "resource/ResourceManager.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include "../../services/events/EventTypes.hpp"
#include "../../services/events/MaterialEvents.hpp"

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

        // Update billboard camera if pipeline is initialized
        if (renderHandler->isBillboardPipelineInitialized())
        {
            renderHandler->getBillboardPipeline()->updateCameraUBO(view, projection, cameraPos);
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

        // Iterate all entities with MeshComponent and WorldTransformComponent
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::MeshComponent, components::WorldTransformComponent>();

        for (auto entity : view)
        {
            const auto& meshComp = view.get<components::MeshComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            // Skip if mesh path is empty
            if (meshComp.meshPath.empty())
            {
                continue;
            }

            // Skip meshes that aren't loaded yet - they will be preloaded via
            // MeshDataChangedNotification subscription when mesh data is set on entities
            if (!meshPipeline->isMeshLoaded(meshComp.meshPath))
            {
                continue;
            }

            // Frustum culling - skip meshes outside the camera frustum (only if frustum is initialized)
            if (currentFrustum.isInitialized())
            {
                const math::AABB* boundingBox = meshPipeline->getMeshBoundingBox(meshComp.meshPath);
                if (boundingBox && !currentFrustum.intersectsAABB(*boundingBox, worldTransform.worldMatrix))
                {
                    continue;  // Mesh is outside frustum, skip rendering
                }
            }

            render::mesh::MeshRenderData renderData;
            renderData.meshPath = meshComp.meshPath;
            renderData.modelMatrix = worldTransform.worldMatrix;

            // Default PBR values (used if no material assigned)
            renderData.albedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            renderData.metallic = 0.0f;
            renderData.roughness = 0.5f;
            renderData.ao = 1.0f;
            renderData.emission = 0.0f;
            renderData.showBoundingBox = meshComp.showBoundingBox;

            // Check for MaterialComponent and populate material assignments
            if (registry.all_of<components::MaterialComponent>(entity))
            {
                const auto& materialComp = registry.get<components::MaterialComponent>(entity);

                // Set default material path
                renderData.defaultMaterialPath = materialComp.defaultMaterial;

                // Copy per-submesh material assignments
                for (const auto& [submeshName, materialPath] : materialComp.subMeshMaterials)
                {
                    render::mesh::SubMeshMaterialInfo matInfo;
                    matInfo.materialPath = materialPath;
                    renderData.submeshMaterials[submeshName] = matInfo;
                }
            }

            meshDrawList.push_back(renderData);
        }

        renderHandler->setMeshDrawList(std::move(meshDrawList));
        renderHandler->setCurrentFrustum(&currentFrustum);
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

        // Iterate all entities with BillboardComponent and WorldTransformComponent
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
            // Extract position from world matrix (translation column)
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
}
