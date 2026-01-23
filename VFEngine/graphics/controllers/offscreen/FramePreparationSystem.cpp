#include "FramePreparationSystem.hpp"
#include "SceneBVHManager.hpp"
#include "CameraController.hpp"
#include "../../render/RenderPassHandler.hpp"
#include "../../render/mesh/StaticMeshPipeline.hpp"
#include "../../render/mesh/MeshTypes.hpp"
#include "../../render/billboard/BillboardTypes.hpp"
#include "../../render/billboard/BillboardPipeline.hpp"
#include "../../render/DebugRenderer.hpp"
#include "../../render/tools/FrustumDebugRenderer.hpp"
#include "../../render/tools/AudioSphereDebugRenderer.hpp"
#include "../../render/tools/PhysicsDebugRenderer.hpp"
#include "../../render/tools/LightGizmoDebugRenderer.hpp"
#include "../../render/gpudriven/GPUDrivenRenderer.hpp"
#include "../../render/occlusion/CameraOcclusionManager.hpp"
#include "../../animation/RuntimeAnimatorSystem.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "resource/ResourceManager.hpp"
#include "../../render/material/MaterialPBRExtractor.hpp"

namespace controllers::offscreen
{
    const render::mesh::ExtractedPBRValues* FramePreparationSystem::getCachedPBRValues(
        const std::string& materialPath)
    {
        if (materialPath.empty())
        {
            return nullptr;
        }

        auto it = pbrCache.find(materialPath);
        if (it != pbrCache.end())
        {
            return &it->second;
        }

        auto [inserted, success] = pbrCache.emplace(materialPath,
                                                    render::mesh::MaterialPBRExtractor::extractPBRFromPath(
                                                        materialPath));
        return &inserted->second;
    }

    void FramePreparationSystem::populateMaterialInfo(render::mesh::SubMeshMaterialInfo& matInfo,
                                                      const std::string& materialPath)
    {
        matInfo.materialPath = materialPath;

        const auto* pbrValues = getCachedPBRValues(materialPath);
        if (pbrValues)
        {
            matInfo.albedo = pbrValues->albedo;
            matInfo.metallic = pbrValues->metallic;
            matInfo.roughness = pbrValues->roughness;
            matInfo.ao = pbrValues->ao;
            matInfo.emission = pbrValues->emission;
            matInfo.blendMode = static_cast<uint8_t>(pbrValues->blendMode);
            matInfo.iblDiffuse = pbrValues->iblDiffuse;
            matInfo.iblSpecular = pbrValues->iblSpecular;
        }
    }

    void FramePreparationSystem::invalidateMaterialCache(const std::string& materialPath)
    {
        pbrCache.erase(materialPath);
    }

    void FramePreparationSystem::prepareMeshes(const FrameContext& ctx)
    {
        auto* renderHandler = ctx.renderHandler;
        auto* meshPipeline = renderHandler->getMeshPipeline();

        if (!meshPipeline)
        {
            renderHandler->setMeshDrawList({});
            return;
        }

        // Update runtime animators during play mode
        if (ctx.playModeActive)
        {
            auto& animatorSystem = animation::RuntimeAnimatorSystem::instance();
            animatorSystem.syncWithRegistry();
            animatorSystem.updateAll(ctx.deltaTime);
        }

        auto* cameraManager = renderHandler->getCameraOcclusionManager();
        auto* activeCamera = cameraManager->getCamera(cameraManager->getActiveCameraId());
        const math::Frustum* activeFrustum = activeCamera ? &activeCamera->frustum : nullptr;
        bool frustumReady = activeFrustum && activeFrustum->isInitialized();

        std::vector<render::mesh::MeshRenderData> meshDrawList;
        auto& registry = scene::EntityRegistry::getRegistry();

        if (ctx.bvhManager->isStaticDirty() && frustumReady)
        {
            ctx.bvhManager->rebuildStaticBVH();
        }

        static int dynamicBvhCooldown = 0;

        if (dynamicBvhCooldown > 0)
        {
            --dynamicBvhCooldown;
        }
        else if (!ctx.bvhManager->needsDynamicRebuild())
        {
            auto dynamicView = registry.view<components::TransformComponent, components::MeshComponent>();
            for (auto entity : dynamicView)
            {
                const auto& transform = dynamicView.get<components::TransformComponent>(entity);

                if (!transform.isStatic && transform.isDirty)
                {
                    ctx.bvhManager->markDynamicEntityDirty(static_cast<uint32_t>(entity));
                }
            }
        }

        if (ctx.bvhManager->isDynamicDirty() && frustumReady)
        {
            ctx.bvhManager->updateDynamicBVH();
            dynamicBvhCooldown = 5;
        }

        auto* gpuDrivenRenderer = renderHandler->getGPUDrivenRenderer();
        bool useGPUDrivenCulling = renderHandler->isGPUDrivenRendererInitialized()
            && gpuDrivenRenderer
            && gpuDrivenRenderer->isEnabled();

        auto buildRenderData = [&](entt::entity entity, const components::MeshComponent& meshComp,
                                   const components::WorldTransformComponent& worldTransform) ->
            render::mesh::MeshRenderData
        {
            render::mesh::MeshRenderData renderData;
            renderData.entity = entity;  // Track source entity for animation lookup
            renderData.meshPath = meshComp.meshPath;
            renderData.modelMatrix = worldTransform.worldMatrix;

            renderData.albedo = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            renderData.metallic = 0.0f;
            renderData.roughness = 0.5f;
            renderData.ao = 1.0f;
            renderData.emission = 0.0f;
            renderData.showBoundingBox = (!ctx.playModeActive && ctx.showDebugRendering)
                                             ? meshComp.showBoundingBox
                                             : false;

            if (registry.all_of<components::MaterialComponent>(entity))
            {
                const auto& materialComp = registry.get<components::MaterialComponent>(entity);
                renderData.defaultMaterialPath = materialComp.defaultMaterial;

                const auto* pbrValues = getCachedPBRValues(materialComp.defaultMaterial);
                if (pbrValues)
                {
                    renderData.albedo = pbrValues->albedo;
                    renderData.metallic = pbrValues->metallic;
                    renderData.roughness = pbrValues->roughness;
                    renderData.ao = pbrValues->ao;
                    renderData.emission = pbrValues->emission;
                }

                for (const auto& [submeshName, materialPath] : materialComp.subMeshMaterials)
                {
                    render::mesh::SubMeshMaterialInfo matInfo;
                    populateMaterialInfo(matInfo, materialPath);
                    renderData.submeshMaterials[submeshName] = matInfo;
                }
            }

            return renderData;
        };

        if (useGPUDrivenCulling)
        {
            auto view = registry.view<components::MeshComponent, components::WorldTransformComponent>();

            for (auto entity : view)
            {
                if (registry.all_of<components::NameComponent>(entity))
                {
                    const auto& nameComp = registry.get<components::NameComponent>(entity);
                    if (!nameComp.isActive)
                    {
                        continue;
                    }
                }

                const auto& meshComp = view.get<components::MeshComponent>(entity);
                const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

                if (meshComp.meshPath.empty() || !meshPipeline->isMeshLoaded(meshComp.meshPath))
                {
                    continue;
                }

                meshDrawList.push_back(buildRenderData(entity, meshComp, worldTransform));
            }
        }
        else if (ctx.bvhManager->isBuilt() && frustumReady)
        {
            std::vector<uint32_t> visibleEntities;
            ctx.bvhManager->queryFrustum(*activeFrustum, visibleEntities);

            for (uint32_t entityId : visibleEntities)
            {
                auto entity = static_cast<entt::entity>(entityId);

                if (!registry.valid(entity))
                {
                    continue;
                }

                if (registry.all_of<components::NameComponent>(entity))
                {
                    const auto& nameComp = registry.get<components::NameComponent>(entity);
                    if (!nameComp.isActive)
                    {
                        continue;
                    }
                }

                const auto& meshComp = registry.get<components::MeshComponent>(entity);
                const auto& worldTransform = registry.get<components::WorldTransformComponent>(entity);

                if (meshComp.meshPath.empty() || !meshPipeline->isMeshLoaded(meshComp.meshPath))
                {
                    continue;
                }

                meshDrawList.push_back(buildRenderData(entity, meshComp, worldTransform));
            }
        }
        else
        {
            auto view = registry.view<components::MeshComponent, components::WorldTransformComponent>();

            for (auto entity : view)
            {
                if (registry.all_of<components::NameComponent>(entity))
                {
                    const auto& nameComp = registry.get<components::NameComponent>(entity);
                    if (!nameComp.isActive)
                    {
                        continue;
                    }
                }

                const auto& meshComp = view.get<components::MeshComponent>(entity);
                const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

                if (meshComp.meshPath.empty() || !meshPipeline->isMeshLoaded(meshComp.meshPath))
                {
                    continue;
                }

                if (frustumReady)
                {
                    const math::AABB* boundingBox = meshPipeline->getMeshBoundingBox(meshComp.meshPath);
                    if (boundingBox && !activeFrustum->intersectsAABB(*boundingBox, worldTransform.worldMatrix))
                    {
                        continue;
                    }
                }

                meshDrawList.push_back(buildRenderData(entity, meshComp, worldTransform));
            }
        }

        renderHandler->setMeshDrawList(std::move(meshDrawList));
        renderHandler->setCurrentFrustum(&ctx.cameraController->getCurrentFrustum());
        ctx.bvhManager->updateOcclusionCullingData(renderHandler);
    }

    void FramePreparationSystem::prepareBillboards(const FrameContext& ctx)
    {
        auto* renderHandler = ctx.renderHandler;

        renderHandler->initBillboardPipeline();

        if (ctx.playModeActive || !ctx.showBillboardIcons || !renderHandler->isBillboardPipelineInitialized())
        {
            renderHandler->setBillboardDrawList({});
            return;
        }

        std::vector<render::billboard::BillboardRenderData> billboardDrawList;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::BillboardComponent, components::WorldTransformComponent>();

        for (auto entity : view)
        {
            if (registry.all_of<components::NameComponent>(entity))
            {
                const auto& nameComp = registry.get<components::NameComponent>(entity);
                if (!nameComp.isActive)
                {
                    continue;
                }
            }

            const auto& billboard = view.get<components::BillboardComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

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

    void FramePreparationSystem::prepareCameraFrustums(const FrameContext& ctx)
    {
        auto* renderHandler = ctx.renderHandler;

        if (ctx.playModeActive || !ctx.showDebugRendering)
        {
            renderHandler->setCameraFrustumDrawList({});
            return;
        }

        std::vector<render::mesh::CameraFrustumRenderData> frustumDrawList;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::CameraComponent, components::WorldTransformComponent>();

        for (auto entity : view)
        {
            if (registry.all_of<components::NameComponent>(entity))
            {
                const auto& nameComp = registry.get<components::NameComponent>(entity);
                if (!nameComp.isActive)
                {
                    continue;
                }
            }

            const auto& cameraComp = view.get<components::CameraComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

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

    void FramePreparationSystem::prepareAudioSpheres(const FrameContext& ctx)
    {
        auto* renderHandler = ctx.renderHandler;

        if (ctx.playModeActive || !ctx.showDebugRendering)
        {
            renderHandler->setAudioSphereDrawList({});
            return;
        }

        std::vector<render::mesh::AudioSphereRenderData> audioSphereDrawList;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::AudioSource3DComponent, components::WorldTransformComponent>();

        for (auto entity : view)
        {
            if (registry.all_of<components::NameComponent>(entity))
            {
                const auto& nameComp = registry.get<components::NameComponent>(entity);
                if (!nameComp.isActive)
                {
                    continue;
                }
            }

            const auto& audioComp = view.get<components::AudioSource3DComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            if (!audioComp.showDebugSpheres)
            {
                continue;
            }

            render::mesh::AudioSphereRenderData renderData;
            renderData.position = glm::vec3(worldTransform.worldMatrix[3]);
            renderData.minDistance = audioComp.minDistance;
            renderData.maxDistance = audioComp.maxDistance;
            renderData.showDebugSpheres = audioComp.showDebugSpheres;

            audioSphereDrawList.push_back(renderData);
        }

        if (!audioSphereDrawList.empty())
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

        renderHandler->setAudioSphereDrawList(std::move(audioSphereDrawList));
    }

    void FramePreparationSystem::prepareGrid(const FrameContext& ctx)
    {
        if (!ctx.showGrid || ctx.playModeActive)
        {
            return;
        }

        auto* renderHandler = ctx.renderHandler;

        if (!renderHandler->isMeshPipelineInitialized())
        {
            renderHandler->initMeshPipeline();
        }

        if (!renderHandler->isDebugRendererInitialized())
        {
            renderHandler->initDebugRenderer();
            renderHandler->getDebugRenderer()->setShowGrid(true);
        }
    }

    void FramePreparationSystem::preparePhysicsColliders(const FrameContext& ctx)
    {
        auto* renderHandler = ctx.renderHandler;

        if (!ctx.showPhysicsDebug)
        {
            renderHandler->setPhysicsColliderDrawList({});
            return;
        }

        std::vector<render::mesh::PhysicsColliderRenderData> colliderDrawList;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::ColliderComponent, components::WorldTransformComponent>();

        for (auto entity : view)
        {
            if (registry.all_of<components::NameComponent>(entity))
            {
                const auto& nameComp = registry.get<components::NameComponent>(entity);
                if (!nameComp.isActive)
                {
                    continue;
                }
            }

            const auto& colliderComp = view.get<components::ColliderComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            render::mesh::PhysicsColliderRenderData renderData;

            // Size is passed separately to PhysicsDebugRenderer which scales unit geometry
            glm::mat4 offsetMatrix = glm::translate(glm::mat4(1.0f), colliderComp.offset);
            renderData.worldMatrix = worldTransform.worldMatrix * offsetMatrix;

            renderData.shape = colliderComp.shape;
            renderData.size = colliderComp.size;
            renderData.radius = colliderComp.size.x;
            renderData.height = colliderComp.height;
            renderData.isTrigger = colliderComp.isTrigger;

            if ((colliderComp.shape == components::ColliderShape::ConvexMesh ||
                colliderComp.shape == components::ColliderShape::TriangleMesh))
            {
                if (!colliderComp.meshPath.empty())
                {
                    renderData.meshPath = colliderComp.meshPath;
                }
                else if (registry.all_of<components::MeshComponent>(entity))
                {
                    const auto& meshComp = registry.get<components::MeshComponent>(entity);
                    renderData.meshPath = meshComp.meshPath;
                }
            }

            if (registry.all_of<components::RigidBodyComponent>(entity))
            {
                const auto& rbComp = registry.get<components::RigidBodyComponent>(entity);
                renderData.bodyType = static_cast<uint8_t>(rbComp.type);
            }
            else
            {
                renderData.bodyType = 0;
            }

            colliderDrawList.push_back(renderData);
        }

        if (!colliderDrawList.empty())
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

        renderHandler->setPhysicsColliderDrawList(std::move(colliderDrawList));
    }

    void FramePreparationSystem::collectDirectionalLightGizmos(
        std::vector<render::mesh::LightGizmoRenderData>& drawList)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::DirectionalLightComponent, components::WorldTransformComponent>();

        for (auto entity : view)
        {
            if (registry.all_of<components::NameComponent>(entity))
            {
                const auto& nameComp = registry.get<components::NameComponent>(entity);
                if (!nameComp.isActive)
                {
                    continue;
                }
            }

            const auto& lightComp = view.get<components::DirectionalLightComponent>(entity);
            if (!lightComp.showGizmo)
            {
                continue;
            }

            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            render::mesh::LightGizmoRenderData renderData;
            renderData.type = render::mesh::LightGizmoType::Directional;
            renderData.worldMatrix = worldTransform.worldMatrix;
            renderData.color = lightComp.color;

            drawList.push_back(renderData);
        }
    }

    void FramePreparationSystem::collectPointLightGizmos(
        std::vector<render::mesh::LightGizmoRenderData>& drawList)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::PointLightComponent, components::WorldTransformComponent>();

        for (auto entity : view)
        {
            if (registry.all_of<components::NameComponent>(entity))
            {
                const auto& nameComp = registry.get<components::NameComponent>(entity);
                if (!nameComp.isActive)
                {
                    continue;
                }
            }

            const auto& lightComp = view.get<components::PointLightComponent>(entity);
            if (!lightComp.showGizmo)
            {
                continue;
            }

            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            render::mesh::LightGizmoRenderData renderData;
            renderData.type = render::mesh::LightGizmoType::Point;
            renderData.worldMatrix = worldTransform.worldMatrix;
            renderData.color = lightComp.color;
            renderData.radius = lightComp.radius;

            drawList.push_back(renderData);
        }
    }

    void FramePreparationSystem::collectSpotLightGizmos(
        std::vector<render::mesh::LightGizmoRenderData>& drawList)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::SpotLightComponent, components::WorldTransformComponent>();

        for (auto entity : view)
        {
            if (registry.all_of<components::NameComponent>(entity))
            {
                const auto& nameComp = registry.get<components::NameComponent>(entity);
                if (!nameComp.isActive)
                {
                    continue;
                }
            }

            const auto& lightComp = view.get<components::SpotLightComponent>(entity);
            if (!lightComp.showGizmo)
            {
                continue;
            }

            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            render::mesh::LightGizmoRenderData renderData;
            renderData.type = render::mesh::LightGizmoType::Spot;
            renderData.worldMatrix = worldTransform.worldMatrix;
            renderData.color = lightComp.color;
            renderData.innerAngle = lightComp.innerAngle;
            renderData.outerAngle = lightComp.outerAngle;
            renderData.range = lightComp.range;

            drawList.push_back(renderData);
        }
    }

    void FramePreparationSystem::prepareLightGizmos(const FrameContext& ctx)
    {
        auto* renderHandler = ctx.renderHandler;

        if (ctx.playModeActive || !ctx.showDebugRendering)
        {
            renderHandler->setLightGizmoDrawList({});
            return;
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        size_t estimatedCount =
            registry.view<components::DirectionalLightComponent>().size() +
            registry.view<components::PointLightComponent>().size() +
            registry.view<components::SpotLightComponent>().size();

        std::vector<render::mesh::LightGizmoRenderData> lightGizmoDrawList;
        lightGizmoDrawList.reserve(estimatedCount);

        collectDirectionalLightGizmos(lightGizmoDrawList);
        collectPointLightGizmos(lightGizmoDrawList);
        collectSpotLightGizmos(lightGizmoDrawList);

        if (!lightGizmoDrawList.empty())
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

        renderHandler->setLightGizmoDrawList(std::move(lightGizmoDrawList));
    }
}
