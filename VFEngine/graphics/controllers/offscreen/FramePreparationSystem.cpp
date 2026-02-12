#include "FramePreparationSystem.hpp"
#include "SceneBVHManager.hpp"
#include "LightBVHManager.hpp"
#include "CameraController.hpp"
#include "../../render/RenderPassHandler.hpp"
#include "../../render/mesh/StaticMeshPipeline.hpp"
#include "../../render/mesh/MeshTypes.hpp"
#include "../../render/billboard/BillboardTypes.hpp"
#include "../../render/billboard/BillboardPipeline.hpp"
#include "../../render/text/TextTypes.hpp"
#include "../../render/text/TextPipeline.hpp"
#include "../../render/DebugRenderer.hpp"
#include "../../render/tools/FrustumDebugRenderer.hpp"
#include "../../render/tools/AudioSphereDebugRenderer.hpp"
#include "../../render/tools/PhysicsDebugRenderer.hpp"
#include "../../render/tools/LightGizmoDebugRenderer.hpp"
#include "../../render/tools/ClusterDebugRenderer.hpp"
#include "../../render/tools/UICanvasDebugRenderer.hpp"
#include "../../render/tools/UICanvasImageRenderer.hpp"
#include "../../render/ui/UIRenderTypes.hpp"
#include "../../render/gpudriven/GPUDrivenRenderer.hpp"
#include "../../render/lighting/ClusterGridManager.hpp"
#include "../../render/occlusion/CameraOcclusionManager.hpp"
#include "../../animation/RuntimeAnimatorSystem.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "resource/ResourceManager.hpp"
#include "../../render/material/MaterialPBRExtractor.hpp"
#include <algorithm>
#include <limits>

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
            renderData.entity = entity;
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

        if (useGPUDrivenCulling && ctx.lightBvhManager && frustumReady)
        {
            ctx.lightBvhManager->update();

            std::vector<uint32_t> visibleLights;
            ctx.lightBvhManager->queryFrustum(*activeFrustum, visibleLights);
            renderHandler->setVisibleLightsFromBVH(visibleLights);
        }
        else if (useGPUDrivenCulling)
        {
            renderHandler->clearVisibleLights();
        }

        renderHandler->setMeshDrawList(std::move(meshDrawList));
        renderHandler->setCurrentFrustum(&ctx.cameraController->getCurrentFrustum());
        ctx.bvhManager->updateOcclusionCullingData(renderHandler);
    }

    void FramePreparationSystem::prepareBillboards(const FrameContext& ctx)
    {
        auto* renderHandler = ctx.renderHandler;

        renderHandler->initBillboardPipeline();

        if (!renderHandler->isBillboardPipelineInitialized())
        {
            renderHandler->setBillboardDrawList({});
            return;
        }

        bool showEditorIcons = !ctx.playModeActive && ctx.showBillboardIcons;

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

            // Editor-only billboards (debug icons) only show in editor mode
            if (billboard.editorOnly && !showEditorIcons)
            {
                continue;
            }

            // Non-editor billboards (custom textured) always render
            render::billboard::BillboardRenderData renderData;
            renderData.worldPosition = glm::vec3(worldTransform.worldMatrix[3]);
            renderData.atlasIndex = billboard.getEffectiveAtlasIndex();
            renderData.size = billboard.size;
            renderData.sizeMode = static_cast<uint32_t>(billboard.sizeMode);
            renderData.entityId = static_cast<uint32_t>(entity);
            renderData.colorTint = billboard.colorTint;
            renderData.texturePath = billboard.texturePath;

            billboardDrawList.push_back(renderData);
        }

        renderHandler->setBillboardDrawList(std::move(billboardDrawList));
    }

    void FramePreparationSystem::prepareText(const FrameContext& ctx)
    {
        auto* renderHandler = ctx.renderHandler;

        renderHandler->initTextPipeline();

        if (!renderHandler->isTextPipelineInitialized())
        {
            renderHandler->setTextDrawList({});
            return;
        }

        std::vector<render::text::TextRenderData> textDrawList;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::TextComponent, components::WorldTransformComponent>();

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

            const auto& textComp = view.get<components::TextComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            if (textComp.fontPath.empty() || textComp.text.empty())
            {
                continue;
            }

            render::text::TextRenderData renderData;
            renderData.fontPath = textComp.fontPath;
            renderData.text = textComp.text;
            renderData.worldPosition = glm::vec3(worldTransform.worldMatrix[3]);
            renderData.fontSize = textComp.fontSize;
            renderData.color = textComp.color;
            renderData.renderMode = 1; // WorldSpace only
            renderData.entityId = static_cast<uint32_t>(entity);
            renderData.lineSpacing = textComp.lineSpacing;
            renderData.letterSpacing = textComp.letterSpacing;
            renderData.maxWidth = textComp.maxWidth;

            textDrawList.push_back(std::move(renderData));
        }

        renderHandler->setTextDrawList(std::move(textDrawList));
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

        auto terrainDebugView = registry.view<components::TerrainTileColliderDebugComponent>();
        for (auto entity : terrainDebugView)
        {
            const auto& debugComp = terrainDebugView.get<components::TerrainTileColliderDebugComponent>(entity);
            if (debugComp.debugData.vertices.empty() || debugComp.debugData.lineIndices.empty())
                continue;

            render::mesh::PhysicsColliderRenderData renderData;
            renderData.worldMatrix = glm::mat4(1.0f); // identity - vertices are world-space
            renderData.shape = types::ColliderShape::HeightField;
            renderData.bodyType = 0; // static
            renderData.heightfieldVertices = &debugComp.debugData.vertices;
            renderData.heightfieldLineIndices = &debugComp.debugData.lineIndices;
            renderData.heightfieldCacheKey = std::to_string(static_cast<uint32_t>(entity)) + "_" + std::to_string(debugComp.tileX) + "_" + std::to_string(debugComp.tileZ);
            renderData.heightfieldVersion = debugComp.debugData.version;

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

        if (ctx.lightBvhManager)
        {
            ctx.lightBvhManager->update();
        }

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

    void FramePreparationSystem::prepareClusterDebug(const FrameContext& ctx)
    {
        auto* renderHandler = ctx.renderHandler;
        if (!renderHandler)
        {
            return;
        }

        if (ctx.playModeActive || !ctx.showDebugRendering || !ctx.showClusterDebug)
        {
            renderHandler->setShowClusterDebug(false);
            return;
        }

        if (!ctx.cameraController)
        {
            renderHandler->setShowClusterDebug(false);
            return;
        }

        renderHandler->setShowClusterDebug(true);

        auto* gpuRenderer = renderHandler->getGPUDrivenRenderer();
        if (!gpuRenderer)
        {
            return;
        }

        auto* clusterGridManager = gpuRenderer->getClusterGridManager();
        if (!clusterGridManager || !clusterGridManager->isInitialized())
        {
            return;
        }

        glm::mat4 viewMatrix = ctx.cameraController->getCurrentViewMatrix();

        render::mesh::ClusterDebugRenderData debugData;
        debugData.clusterAABBs = clusterGridManager->getClusterAABBs();
        debugData.invViewMatrix = glm::inverse(viewMatrix);

        auto& registry = scene::EntityRegistry::getRegistry();

        auto pointView = registry.view<components::PointLightComponent, components::WorldTransformComponent>();
        for (auto entity : pointView)
        {
            const auto& lightComp = pointView.get<components::PointLightComponent>(entity);

            if (lightComp.showGizmo)
            {
                const auto& worldTransform = pointView.get<components::WorldTransformComponent>(entity);
                glm::vec3 worldPos = glm::vec3(worldTransform.worldMatrix[3]);
                glm::vec3 viewPos = glm::vec3(viewMatrix * glm::vec4(worldPos, 1.0f));

                auto affectedClusters = clusterGridManager->getClusterIndicesForPointLight(viewPos, lightComp.radius);
                debugData.highlightedClusterIndices.insert(
                    debugData.highlightedClusterIndices.end(),
                    affectedClusters.begin(),
                    affectedClusters.end()
                );
            }
        }

        auto spotView = registry.view<components::SpotLightComponent, components::WorldTransformComponent>();
        for (auto entity : spotView)
        {
            const auto& lightComp = spotView.get<components::SpotLightComponent>(entity);

            if (lightComp.showGizmo)
            {
                const auto& worldTransform = spotView.get<components::WorldTransformComponent>(entity);
                glm::vec3 worldPos = glm::vec3(worldTransform.worldMatrix[3]);
                glm::vec3 worldDir = glm::normalize(glm::vec3(worldTransform.worldMatrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));

                glm::vec3 viewPos = glm::vec3(viewMatrix * glm::vec4(worldPos, 1.0f));
                glm::vec3 viewDir = glm::normalize(glm::vec3(viewMatrix * glm::vec4(worldDir, 0.0f)));

                float outerAngleCos = std::cos(glm::radians(lightComp.outerAngle));
                auto affectedClusters = clusterGridManager->getClusterIndicesForSpotLight(
                    viewPos, viewDir, lightComp.range, outerAngleCos);
                debugData.highlightedClusterIndices.insert(
                    debugData.highlightedClusterIndices.end(),
                    affectedClusters.begin(),
                    affectedClusters.end()
                );
            }
        }

        debugData.showAllClusters = debugData.highlightedClusterIndices.empty();

        renderHandler->setClusterDebugData(std::move(debugData));
    }

    void FramePreparationSystem::prepareUICanvasOutlines(const FrameContext& ctx)
    {
        auto* renderHandler = ctx.renderHandler;

        if (ctx.playModeActive || !ctx.showDebugRendering)
        {
            renderHandler->setUICanvasOutlineDrawList({});
            return;
        }

        std::vector<render::mesh::UICanvasOutlineRenderData> drawList;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::UICanvasComponent, components::WorldTransformComponent>();

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

            const auto& canvasComp = view.get<components::UICanvasComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            float width = canvasComp.referenceWidth / canvasComp.pixelsPerUnit;
            float height = canvasComp.referenceHeight / canvasComp.pixelsPerUnit;

            render::mesh::UICanvasOutlineRenderData renderData;
            renderData.modelMatrix = worldTransform.worldMatrix
                * glm::scale(glm::mat4(1.0f), glm::vec3(width, height, 1.0f));

            drawList.push_back(renderData);
        }

        if (!drawList.empty())
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

        renderHandler->setUICanvasOutlineDrawList(std::move(drawList));
    }

    namespace
    {
        struct PixelRect
        {
            float x, y, w, h;
        };

        PixelRect resolvePixelRect(
            const components::UIRectComponent& rectComp,
            float parentW, float parentH, float scale)
        {
            float anchorLeftPx  = rectComp.anchorMin.x * parentW;
            float anchorRightPx = rectComp.anchorMax.x * parentW;
            float anchorTopPx   = (1.0f - rectComp.anchorMax.y) * parentH;
            float anchorBotPx   = (1.0f - rectComp.anchorMin.y) * parentH;

            float w = (anchorRightPx - anchorLeftPx) + rectComp.sizeDelta.x * scale;
            float h = (anchorBotPx - anchorTopPx) + rectComp.sizeDelta.y * scale;

            float cx = (anchorLeftPx + anchorRightPx) * 0.5f + rectComp.anchoredPosition.x * scale;
            float cy = (anchorTopPx + anchorBotPx) * 0.5f - rectComp.anchoredPosition.y * scale;

            float posX = cx - rectComp.pivot.x * w;
            float posY = cy - rectComp.pivot.y * h;

            return {posX, posY, w, h};
        }
    } // anonymous namespace

    void FramePreparationSystem::prepareUIImages(const FrameContext& ctx)
    {
        auto* renderHandler = ctx.renderHandler;

        if (ctx.playModeActive)
        {
            // Play mode: screen-space overlay via UIRenderPipeline
            renderHandler->setUICanvasImageDrawList({});
            prepareUIImagesScreenSpace(ctx);
        }
        else
        {
            // Editor mode: world-space quads on canvas via UICanvasImageRenderer
            renderHandler->setUIImageDrawList({});
            prepareUIImagesWorldSpace(ctx);
        }
    }

    void FramePreparationSystem::prepareUIImagesScreenSpace(const FrameContext& ctx)
    {
        auto* renderHandler = ctx.renderHandler;

        renderHandler->initUIRenderPipeline();

        if (!renderHandler->isUIRenderPipelineInitialized())
        {
            renderHandler->setUIImageDrawList({});
            return;
        }

        if (ctx.viewportWidth == 0 || ctx.viewportHeight == 0)
        {
            renderHandler->setUIImageDrawList({});
            return;
        }

        std::vector<render::ui::UIImageRenderData> drawList;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::UIImageComponent, components::UIRectComponent>();

        // --- Apply mouse wheel scroll input ---
        if (ctx.scrollDelta.y != 0.0f || ctx.scrollDelta.x != 0.0f)
        {
            auto scrollInputView = registry.view<components::UIScrollComponent, components::UIRectComponent>();

            // Find the innermost scroll container under the mouse cursor
            entt::entity targetScroll = entt::null;
            float smallestArea = std::numeric_limits<float>::max();

            for (auto scrollEntity : scrollInputView)
            {
                if (registry.all_of<components::NameComponent>(scrollEntity))
                {
                    if (!registry.get<components::NameComponent>(scrollEntity).isActive)
                        continue;
                }

                // Find canvas for scale computation
                const components::UICanvasComponent* scrollCanvas = nullptr;
                entt::entity cur = scrollEntity;
                while (registry.all_of<components::ParentComponent>(cur))
                {
                    entt::entity p = registry.get<components::ParentComponent>(cur).parent;
                    if (p == entt::null || !registry.valid(p))
                        break;
                    if (registry.all_of<components::UICanvasComponent>(p))
                    {
                        scrollCanvas = &registry.get<components::UICanvasComponent>(p);
                        break;
                    }
                    cur = p;
                }
                if (!scrollCanvas && registry.all_of<components::UICanvasComponent>(scrollEntity))
                    scrollCanvas = &registry.get<components::UICanvasComponent>(scrollEntity);
                if (!scrollCanvas)
                    continue;

                float vw = static_cast<float>(ctx.viewportWidth);
                float vh = static_cast<float>(ctx.viewportHeight);
                float sc = 1.0f;
                if (scrollCanvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                    sc = std::min(vw / scrollCanvas->referenceWidth, vh / scrollCanvas->referenceHeight);

                const auto& scrollRect = registry.get<components::UIRectComponent>(scrollEntity);
                PixelRect vpRect = resolvePixelRect(scrollRect, vw, vh, sc);

                // Hit test: is mouse inside this scroll container's viewport?
                if (ctx.mousePosition.x >= vpRect.x && ctx.mousePosition.x <= vpRect.x + vpRect.w
                    && ctx.mousePosition.y >= vpRect.y && ctx.mousePosition.y <= vpRect.y + vpRect.h)
                {
                    float area = vpRect.w * vpRect.h;
                    if (area < smallestArea)
                    {
                        smallestArea = area;
                        targetScroll = scrollEntity;
                    }
                }
            }

            // Apply scroll delta to the target container
            if (targetScroll != entt::null)
            {
                auto& scrollComp = registry.get<components::UIScrollComponent>(targetScroll);
                float sensitivity = scrollComp.scrollSensitivity * 20.0f;

                if (scrollComp.verticalScrollEnabled)
                    scrollComp.scrollOffset.y -= ctx.scrollDelta.y * sensitivity;
                if (scrollComp.horizontalScrollEnabled)
                    scrollComp.scrollOffset.x -= ctx.scrollDelta.x * sensitivity;
            }
        }

        // --- Pre-compute scroll container info ---
        struct ScrollContainerInfo
        {
            glm::vec2 scrollOffset{0.0f, 0.0f};
            glm::vec4 scissorRect{0.0f, 0.0f, 0.0f, 0.0f};
        };
        std::unordered_map<uint32_t, ScrollContainerInfo> scrollContainers;
        {
            auto scrollView = registry.view<components::UIScrollComponent, components::UIRectComponent>();
            for (auto scrollEntity : scrollView)
            {
                if (registry.all_of<components::NameComponent>(scrollEntity))
                {
                    if (!registry.get<components::NameComponent>(scrollEntity).isActive)
                        continue;
                }

                // Find canvas (same parent-walk pattern)
                const components::UICanvasComponent* scrollCanvas = nullptr;
                entt::entity cur = scrollEntity;
                while (registry.all_of<components::ParentComponent>(cur))
                {
                    entt::entity p = registry.get<components::ParentComponent>(cur).parent;
                    if (p == entt::null || !registry.valid(p))
                        break;
                    if (registry.all_of<components::UICanvasComponent>(p))
                    {
                        scrollCanvas = &registry.get<components::UICanvasComponent>(p);
                        break;
                    }
                    cur = p;
                }
                if (!scrollCanvas && registry.all_of<components::UICanvasComponent>(scrollEntity))
                    scrollCanvas = &registry.get<components::UICanvasComponent>(scrollEntity);
                if (!scrollCanvas)
                    continue;

                float vw = static_cast<float>(ctx.viewportWidth);
                float vh = static_cast<float>(ctx.viewportHeight);
                float sc = 1.0f;
                if (scrollCanvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                    sc = std::min(vw / scrollCanvas->referenceWidth, vh / scrollCanvas->referenceHeight);

                const auto& scrollRect = scrollView.get<components::UIRectComponent>(scrollEntity);
                PixelRect vpRect = resolvePixelRect(scrollRect, vw, vh, sc);

                // Compute content bounding box from direct children
                float minX = 0.0f, minY = 0.0f, maxX = vpRect.w, maxY = vpRect.h;
                if (registry.all_of<components::ChildrenComponent>(scrollEntity))
                {
                    bool first = true;
                    for (auto child : registry.get<components::ChildrenComponent>(scrollEntity).children)
                    {
                        if (!registry.valid(child) || !registry.all_of<components::UIRectComponent>(child))
                            continue;
                        if (registry.all_of<components::NameComponent>(child)
                            && !registry.get<components::NameComponent>(child).isActive)
                            continue;

                        PixelRect cr = resolvePixelRect(
                            registry.get<components::UIRectComponent>(child), vw, vh, sc);
                        float relX = cr.x - vpRect.x;
                        float relY = cr.y - vpRect.y;

                        if (first)
                        {
                            minX = relX;
                            minY = relY;
                            maxX = relX + cr.w;
                            maxY = relY + cr.h;
                            first = false;
                        }
                        else
                        {
                            minX = std::min(minX, relX);
                            minY = std::min(minY, relY);
                            maxX = std::max(maxX, relX + cr.w);
                            maxY = std::max(maxY, relY + cr.h);
                        }
                    }
                }

                float contentW = maxX - std::min(minX, 0.0f);
                float contentH = maxY - std::min(minY, 0.0f);

                auto& scrollComp = registry.get<components::UIScrollComponent>(scrollEntity);
                float maxScrollX = std::max(0.0f, contentW - vpRect.w);
                float maxScrollY = std::max(0.0f, contentH - vpRect.h);
                scrollComp.scrollOffset.x = scrollComp.horizontalScrollEnabled
                    ? glm::clamp(scrollComp.scrollOffset.x, 0.0f, maxScrollX) : 0.0f;
                scrollComp.scrollOffset.y = scrollComp.verticalScrollEnabled
                    ? glm::clamp(scrollComp.scrollOffset.y, 0.0f, maxScrollY) : 0.0f;

                scrollComp.contentSize = glm::vec2(contentW, contentH);
                scrollComp.viewportSize = glm::vec2(vpRect.w, vpRect.h);

                // Scissor rect clamped to screen bounds
                float sx = std::max(0.0f, vpRect.x);
                float sy = std::max(0.0f, vpRect.y);
                float sw = std::max(0.0f, std::min(vpRect.x + vpRect.w, vw) - sx);
                float sh = std::max(0.0f, std::min(vpRect.y + vpRect.h, vh) - sy);
                glm::vec4 scissor(sx, sy, sw, sh);
                scrollComp.computedScissorRect = scissor;

                scrollContainers[static_cast<uint32_t>(scrollEntity)] = {scrollComp.scrollOffset, scissor};
            }
        }

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

            const auto& imageComp = view.get<components::UIImageComponent>(entity);
            if (imageComp.texturePath.empty())
            {
                continue;
            }

            const auto& rectComp = view.get<components::UIRectComponent>(entity);

            // Walk parent hierarchy to find UICanvasComponent and nearest UIScrollComponent
            const components::UICanvasComponent* canvas = nullptr;
            entt::entity scrollAncestor = entt::null;
            entt::entity current = entity;
            while (registry.all_of<components::ParentComponent>(current))
            {
                entt::entity parentEntity = registry.get<components::ParentComponent>(current).parent;
                if (parentEntity == entt::null || !registry.valid(parentEntity))
                {
                    break;
                }

                if (scrollAncestor == entt::null
                    && registry.all_of<components::UIScrollComponent>(parentEntity))
                {
                    scrollAncestor = parentEntity;
                }

                if (registry.all_of<components::UICanvasComponent>(parentEntity))
                {
                    canvas = &registry.get<components::UICanvasComponent>(parentEntity);
                    break;
                }
                current = parentEntity;
            }

            if (!canvas && registry.all_of<components::UICanvasComponent>(entity))
            {
                canvas = &registry.get<components::UICanvasComponent>(entity);
            }

            if (!canvas)
            {
                continue;
            }

            // Resolve anchors to pixel coordinates
            float viewportW = static_cast<float>(ctx.viewportWidth);
            float viewportH = static_cast<float>(ctx.viewportHeight);

            float scale = 1.0f;
            if (canvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
            {
                scale = std::min(viewportW / canvas->referenceWidth,
                                 viewportH / canvas->referenceHeight);
            }

            PixelRect rect = resolvePixelRect(rectComp, viewportW, viewportH, scale);

            // Apply scroll container offset and scissor
            glm::vec4 scissor{0.0f, 0.0f, 0.0f, 0.0f};
            if (scrollAncestor != entt::null)
            {
                auto it = scrollContainers.find(static_cast<uint32_t>(scrollAncestor));
                if (it != scrollContainers.end())
                {
                    rect.x -= it->second.scrollOffset.x;
                    rect.y -= it->second.scrollOffset.y;
                    scissor = it->second.scissorRect;
                }
            }

            render::ui::UIImageRenderData renderData;
            renderData.texturePath = imageComp.texturePath;
            renderData.position = glm::vec2(rect.x, rect.y);
            renderData.size = glm::vec2(rect.w, rect.h);
            renderData.colorTint = imageComp.colorTint;
            renderData.scissorRect = scissor;

            drawList.push_back(std::move(renderData));
        }

        renderHandler->setUIImageDrawList(std::move(drawList));
    }

    void FramePreparationSystem::prepareUIImagesWorldSpace(const FrameContext& ctx)
    {
        auto* renderHandler = ctx.renderHandler;

        std::vector<render::mesh::UICanvasImageRenderData> drawList;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::UIImageComponent, components::UIRectComponent>();

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

            const auto& imageComp = view.get<components::UIImageComponent>(entity);
            if (imageComp.texturePath.empty())
            {
                continue;
            }

            const auto& rectComp = view.get<components::UIRectComponent>(entity);

            // Walk parent hierarchy to find canvas entity with WorldTransformComponent
            const components::UICanvasComponent* canvas = nullptr;
            entt::entity canvasEntity = entt::null;
            entt::entity current = entity;

            while (registry.all_of<components::ParentComponent>(current))
            {
                entt::entity parentEntity = registry.get<components::ParentComponent>(current).parent;
                if (parentEntity == entt::null || !registry.valid(parentEntity))
                {
                    break;
                }

                if (registry.all_of<components::UICanvasComponent>(parentEntity))
                {
                    canvas = &registry.get<components::UICanvasComponent>(parentEntity);
                    canvasEntity = parentEntity;
                    break;
                }
                current = parentEntity;
            }

            if (!canvas && registry.all_of<components::UICanvasComponent>(entity))
            {
                canvas = &registry.get<components::UICanvasComponent>(entity);
                canvasEntity = entity;
            }

            if (!canvas || canvasEntity == entt::null)
            {
                continue;
            }

            // Need canvas world transform for positioning in 3D space
            if (!registry.all_of<components::WorldTransformComponent>(canvasEntity))
            {
                continue;
            }

            const auto& canvasWorldTransform = registry.get<components::WorldTransformComponent>(canvasEntity);

            // Canvas world dimensions
            float canvasW = canvas->referenceWidth / canvas->pixelsPerUnit;
            float canvasH = canvas->referenceHeight / canvas->pixelsPerUnit;

            // Image rect in canvas pixels (referenceWidth x referenceHeight is the parent)
            float anchorLeft = rectComp.anchorMin.x * canvas->referenceWidth;
            float anchorRight = rectComp.anchorMax.x * canvas->referenceWidth;
            float anchorBottom = rectComp.anchorMin.y * canvas->referenceHeight;
            float anchorTop = rectComp.anchorMax.y * canvas->referenceHeight;

            float w = (anchorRight - anchorLeft) + rectComp.sizeDelta.x;
            float h = (anchorTop - anchorBottom) + rectComp.sizeDelta.y;
            float cx = (anchorLeft + anchorRight) * 0.5f + rectComp.anchoredPosition.x;
            float cy = (anchorBottom + anchorTop) * 0.5f + rectComp.anchoredPosition.y;

            // Normalize to canvas space (0..1), then shift to unit quad space (-0.5..0.5)
            float localCX = cx / canvas->referenceWidth - 0.5f;
            float localCY = cy / canvas->referenceHeight - 0.5f;
            float normW = w / canvas->referenceWidth;
            float normH = h / canvas->referenceHeight;

            // Model matrix: canvas world transform * canvas size * image offset * image size
            glm::mat4 canvasScaled = canvasWorldTransform.worldMatrix
                * glm::scale(glm::mat4(1.0f), glm::vec3(canvasW, canvasH, 1.0f));

            glm::mat4 imageModel = canvasScaled
                * glm::translate(glm::mat4(1.0f), glm::vec3(localCX, localCY, 0.001f))
                * glm::scale(glm::mat4(1.0f), glm::vec3(normW, normH, 1.0f));

            render::mesh::UICanvasImageRenderData renderData;
            renderData.modelMatrix = imageModel;
            renderData.texturePath = imageComp.texturePath;
            renderData.colorTint = imageComp.colorTint;

            drawList.push_back(std::move(renderData));
        }

        if (!drawList.empty())
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

        renderHandler->setUICanvasImageDrawList(std::move(drawList));
    }
}
