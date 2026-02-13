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
#include "../../render/ui/UITextRenderTypes.hpp"
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

        // --- UIRect wireframe outlines for all rect elements ---
        auto rectView = registry.view<components::UIRectComponent>();
        for (auto entity : rectView)
        {
            if (registry.all_of<components::NameComponent>(entity))
            {
                if (!registry.get<components::NameComponent>(entity).isActive)
                    continue;
            }

            // Skip canvas entities themselves (already drawn above)
            if (registry.all_of<components::UICanvasComponent>(entity))
                continue;

            const auto& rectComp = rectView.get<components::UIRectComponent>(entity);

            // Walk parent hierarchy to find canvas
            const components::UICanvasComponent* canvas = nullptr;
            entt::entity canvasEntity = entt::null;
            entt::entity current = entity;

            while (registry.all_of<components::ParentComponent>(current))
            {
                entt::entity parentEntity = registry.get<components::ParentComponent>(current).parent;
                if (parentEntity == entt::null || !registry.valid(parentEntity))
                    break;

                if (registry.all_of<components::UICanvasComponent>(parentEntity))
                {
                    canvas = &registry.get<components::UICanvasComponent>(parentEntity);
                    canvasEntity = parentEntity;
                    break;
                }
                current = parentEntity;
            }

            if (!canvas || canvasEntity == entt::null)
                continue;

            if (!registry.all_of<components::WorldTransformComponent>(canvasEntity))
                continue;

            const auto& canvasWorldTransform = registry.get<components::WorldTransformComponent>(canvasEntity);

            float canvasW = canvas->referenceWidth / canvas->pixelsPerUnit;
            float canvasH = canvas->referenceHeight / canvas->pixelsPerUnit;

            float anchorLeft = rectComp.anchorMin.x * canvas->referenceWidth;
            float anchorRight = rectComp.anchorMax.x * canvas->referenceWidth;
            float anchorBottom = rectComp.anchorMin.y * canvas->referenceHeight;
            float anchorTop = rectComp.anchorMax.y * canvas->referenceHeight;

            float w = (anchorRight - anchorLeft) + rectComp.sizeDelta.x;
            float h = (anchorTop - anchorBottom) + rectComp.sizeDelta.y;
            float cx = (anchorLeft + anchorRight) * 0.5f + rectComp.anchoredPosition.x;
            float cy = (anchorBottom + anchorTop) * 0.5f + rectComp.anchoredPosition.y;

            float localCX = cx / canvas->referenceWidth - 0.5f;
            float localCY = cy / canvas->referenceHeight - 0.5f;
            float normW = w / canvas->referenceWidth;
            float normH = h / canvas->referenceHeight;

            glm::mat4 canvasScaled = canvasWorldTransform.worldMatrix
                * glm::scale(glm::mat4(1.0f), glm::vec3(canvasW, canvasH, 1.0f));

            glm::mat4 rectModel = canvasScaled
                * glm::translate(glm::mat4(1.0f), glm::vec3(localCX, localCY, 0.002f))
                * glm::scale(glm::mat4(1.0f), glm::vec3(normW, normH, 1.0f));

            render::mesh::UICanvasOutlineRenderData renderData;
            renderData.modelMatrix = rectModel;
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

        const components::UICanvasComponent* findCanvasForEntity(
            entt::registry& registry, entt::entity entity)
        {
            entt::entity current = entity;
            while (registry.all_of<components::ParentComponent>(current))
            {
                entt::entity parent = registry.get<components::ParentComponent>(current).parent;
                if (parent == entt::null || !registry.valid(parent))
                    break;
                if (registry.all_of<components::UICanvasComponent>(parent))
                    return &registry.get<components::UICanvasComponent>(parent);
                current = parent;
            }
            if (registry.all_of<components::UICanvasComponent>(entity))
                return &registry.get<components::UICanvasComponent>(entity);
            return nullptr;
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

        prepareUILabels(ctx);
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

        // --- Scrollbar drag interaction ---
        bool anyScrollDragging = false;
        {
            auto scrollDragView = registry.view<components::UIScrollComponent, components::UIRectComponent>();

            // Phase 1: Process active drags
            for (auto scrollEntity : scrollDragView)
            {
                auto& scrollComp = registry.get<components::UIScrollComponent>(scrollEntity);
                if (!scrollComp.isDragging)
                    continue;

                anyScrollDragging = true;

                if (ctx.leftMouseDown)
                {
                    const auto* scrollCanvas = findCanvasForEntity(registry, scrollEntity);
                    if (!scrollCanvas) { scrollComp.isDragging = false; break; }

                    float vw = static_cast<float>(ctx.viewportWidth);
                    float vh = static_cast<float>(ctx.viewportHeight);
                    float sc = 1.0f;
                    if (scrollCanvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                        sc = std::min(vw / scrollCanvas->referenceWidth, vh / scrollCanvas->referenceHeight);

                    const auto& scrollRect = registry.get<components::UIRectComponent>(scrollEntity);
                    PixelRect vpRect = resolvePixelRect(scrollRect, vw, vh, sc);

                    if (scrollComp.dragAxis == 1) // vertical
                    {
                        float maxScrollY = std::max(0.0f, scrollComp.contentSize.y - scrollComp.viewportSize.y);
                        float ratio = scrollComp.viewportSize.y / scrollComp.contentSize.y;
                        float thumbH = std::max(20.0f, vpRect.h * ratio);
                        float scrollRange = vpRect.h - thumbH;
                        if (scrollRange > 0.0f)
                        {
                            float deltaMouseY = ctx.mousePosition.y - scrollComp.dragStartMousePos.y;
                            scrollComp.scrollOffset.y = glm::clamp(
                                scrollComp.dragStartScrollOffset.y + (deltaMouseY / scrollRange) * maxScrollY,
                                0.0f, maxScrollY);
                        }
                    }
                    else // horizontal (dragAxis == 0)
                    {
                        float maxScrollX = std::max(0.0f, scrollComp.contentSize.x - scrollComp.viewportSize.x);
                        float ratio = scrollComp.viewportSize.x / scrollComp.contentSize.x;
                        float thumbW = std::max(20.0f, vpRect.w * ratio);
                        float scrollRange = vpRect.w - thumbW;
                        if (scrollRange > 0.0f)
                        {
                            float deltaMouseX = ctx.mousePosition.x - scrollComp.dragStartMousePos.x;
                            scrollComp.scrollOffset.x = glm::clamp(
                                scrollComp.dragStartScrollOffset.x + (deltaMouseX / scrollRange) * maxScrollX,
                                0.0f, maxScrollX);
                        }
                    }
                }
                else
                {
                    scrollComp.isDragging = false;
                }
                break; // only one drag at a time
            }

            // Phase 2: Initiate new drag on mouse down over thumb
            if (!anyScrollDragging && ctx.leftMouseDown)
            {
                for (auto scrollEntity : scrollDragView)
                {
                    if (registry.all_of<components::NameComponent>(scrollEntity))
                        if (!registry.get<components::NameComponent>(scrollEntity).isActive)
                            continue;

                    const auto* scrollCanvas = findCanvasForEntity(registry, scrollEntity);
                    if (!scrollCanvas) continue;

                    float vw = static_cast<float>(ctx.viewportWidth);
                    float vh = static_cast<float>(ctx.viewportHeight);
                    float sc = 1.0f;
                    if (scrollCanvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                        sc = std::min(vw / scrollCanvas->referenceWidth, vh / scrollCanvas->referenceHeight);

                    auto& scrollComp = registry.get<components::UIScrollComponent>(scrollEntity);
                    const auto& scrollRect = registry.get<components::UIRectComponent>(scrollEntity);
                    PixelRect vpRect = resolvePixelRect(scrollRect, vw, vh, sc);

                    // Check vertical thumb hit
                    if (scrollComp.verticalScrollEnabled && scrollComp.contentSize.y > scrollComp.viewportSize.y
                        && scrollComp.verticalScrollbarVisibility != components::ScrollbarVisibility::Hidden)
                    {
                        float maxScrollY = std::max(0.0f, scrollComp.contentSize.y - scrollComp.viewportSize.y);
                        float ratio = scrollComp.viewportSize.y / scrollComp.contentSize.y;
                        float thumbH = std::max(20.0f, vpRect.h * ratio);
                        float scrollRange = vpRect.h - thumbH;
                        float thumbY = (maxScrollY > 0.0f)
                            ? vpRect.y + scrollRange * (scrollComp.scrollOffset.y / maxScrollY) : vpRect.y;
                        float thumbX = vpRect.x + vpRect.w - 8.0f;

                        if (ctx.mousePosition.x >= thumbX && ctx.mousePosition.x <= thumbX + 8.0f
                            && ctx.mousePosition.y >= thumbY && ctx.mousePosition.y <= thumbY + thumbH)
                        {
                            scrollComp.isDragging = true;
                            scrollComp.dragAxis = 1;
                            scrollComp.dragStartScrollOffset = scrollComp.scrollOffset;
                            scrollComp.dragStartMousePos = ctx.mousePosition;
                            break;
                        }
                    }

                    // Check horizontal thumb hit
                    if (scrollComp.horizontalScrollEnabled && scrollComp.contentSize.x > scrollComp.viewportSize.x
                        && scrollComp.horizontalScrollbarVisibility != components::ScrollbarVisibility::Hidden)
                    {
                        float maxScrollX = std::max(0.0f, scrollComp.contentSize.x - scrollComp.viewportSize.x);
                        float ratio = scrollComp.viewportSize.x / scrollComp.contentSize.x;
                        float thumbW = std::max(20.0f, vpRect.w * ratio);
                        float scrollRange = vpRect.w - thumbW;
                        float thumbX = (maxScrollX > 0.0f)
                            ? vpRect.x + scrollRange * (scrollComp.scrollOffset.x / maxScrollX) : vpRect.x;
                        float thumbY = vpRect.y + vpRect.h - 8.0f;

                        if (ctx.mousePosition.x >= thumbX && ctx.mousePosition.x <= thumbX + thumbW
                            && ctx.mousePosition.y >= thumbY && ctx.mousePosition.y <= thumbY + 8.0f)
                        {
                            scrollComp.isDragging = true;
                            scrollComp.dragAxis = 0;
                            scrollComp.dragStartScrollOffset = scrollComp.scrollOffset;
                            scrollComp.dragStartMousePos = ctx.mousePosition;
                            break;
                        }
                    }
                }
            }
        }

        // --- Apply mouse wheel scroll input ---
        if (!anyScrollDragging && (ctx.scrollDelta.y != 0.0f || ctx.scrollDelta.x != 0.0f))
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

                const auto* scrollCanvas = findCanvasForEntity(registry, scrollEntity);
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

                bool canScrollV = scrollComp.verticalScrollEnabled
                    && scrollComp.contentSize.y > scrollComp.viewportSize.y;
                bool canScrollH = scrollComp.horizontalScrollEnabled
                    && scrollComp.contentSize.x > scrollComp.viewportSize.x;

                if (canScrollV)
                    scrollComp.scrollOffset.y -= ctx.scrollDelta.y * sensitivity;

                if (canScrollH)
                {
                    // Use horizontal scroll delta, or route vertical wheel to horizontal
                    // when this container only scrolls horizontally
                    float hDelta = ctx.scrollDelta.x;
                    if (!canScrollV && ctx.scrollDelta.y != 0.0f)
                        hDelta = ctx.scrollDelta.y;
                    scrollComp.scrollOffset.x -= hDelta * sensitivity;
                }
            }
        }

        // --- Layout group pass: auto-position children ---
        {
            auto layoutView = registry.view<components::UILayoutGroupComponent,
                                            components::ChildrenComponent,
                                            components::UIRectComponent>();
            for (auto layoutEntity : layoutView)
            {
                if (registry.all_of<components::NameComponent>(layoutEntity))
                    if (!registry.get<components::NameComponent>(layoutEntity).isActive)
                        continue;

                const auto* layoutCanvas = findCanvasForEntity(registry, layoutEntity);
                if (!layoutCanvas)
                    continue;

                float vw = static_cast<float>(ctx.viewportWidth);
                float vh = static_cast<float>(ctx.viewportHeight);
                float sc = 1.0f;
                if (layoutCanvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                    sc = std::min(vw / layoutCanvas->referenceWidth, vh / layoutCanvas->referenceHeight);

                const auto& parentRect = registry.get<components::UIRectComponent>(layoutEntity);
                PixelRect pRect = resolvePixelRect(parentRect, vw, vh, sc);

                const auto& layoutComp = registry.get<components::UILayoutGroupComponent>(layoutEntity);
                const auto& children = registry.get<components::ChildrenComponent>(layoutEntity).children;

                bool isVertical = (layoutComp.direction == components::LayoutDirection::Vertical);
                bool isGrid = (layoutComp.direction == components::LayoutDirection::Grid);

                float cursorX = layoutComp.padding.x; // left
                float cursorY = layoutComp.padding.z;  // top
                int gridCol = 0;
                int cols = std::max(1, layoutComp.constraintCount);
                float rowHeight = 0.0f;

                // For non-grid: single cursor along main axis
                float cursor = isVertical ? layoutComp.padding.z : layoutComp.padding.x;

                for (auto child : children)
                {
                    if (!registry.valid(child) || !registry.all_of<components::UIRectComponent>(child))
                        continue;
                    if (registry.all_of<components::NameComponent>(child)
                        && !registry.get<components::NameComponent>(child).isActive)
                        continue;

                    auto& childRect = registry.get<components::UIRectComponent>(child);

                    // Compute child pixel size from its rect
                    float childW = (childRect.anchorMax.x - childRect.anchorMin.x) * vw + childRect.sizeDelta.x * sc;
                    float childH = ((1.0f - childRect.anchorMin.y) - (1.0f - childRect.anchorMax.y)) * vh + childRect.sizeDelta.y * sc;

                    // Compute target pixel position
                    float targetX, targetY;

                    if (isGrid)
                    {
                        float availW = pRect.w - layoutComp.padding.x - layoutComp.padding.y;
                        float cellW = (availW - layoutComp.spacing * (cols - 1)) / cols;

                        targetX = pRect.x + layoutComp.padding.x + gridCol * (cellW + layoutComp.spacing);
                        // Center child within cell
                        targetX += (cellW - childW) * 0.5f;
                        targetY = pRect.y + cursorY;

                        rowHeight = std::max(rowHeight, childH);
                        gridCol++;
                        if (gridCol >= cols)
                        {
                            gridCol = 0;
                            cursorY += rowHeight + layoutComp.spacing;
                            rowHeight = 0.0f;
                        }
                    }
                    else if (isVertical)
                    {
                        targetY = pRect.y + cursor;
                        float availW = pRect.w - layoutComp.padding.x - layoutComp.padding.y;
                        switch (layoutComp.childAlignment)
                        {
                        case components::ChildAlignment::Center:
                            targetX = pRect.x + layoutComp.padding.x + (availW - childW) * 0.5f;
                            break;
                        case components::ChildAlignment::End:
                            targetX = pRect.x + layoutComp.padding.x + availW - childW;
                            break;
                        default: // Start
                            targetX = pRect.x + layoutComp.padding.x;
                            break;
                        }
                        cursor += childH + layoutComp.spacing;
                    }
                    else // Horizontal
                    {
                        targetX = pRect.x + cursor;
                        float availH = pRect.h - layoutComp.padding.z - layoutComp.padding.w;
                        switch (layoutComp.childAlignment)
                        {
                        case components::ChildAlignment::Center:
                            targetY = pRect.y + layoutComp.padding.z + (availH - childH) * 0.5f;
                            break;
                        case components::ChildAlignment::End:
                            targetY = pRect.y + layoutComp.padding.z + availH - childH;
                            break;
                        default: // Start
                            targetY = pRect.y + layoutComp.padding.z;
                            break;
                        }
                        cursor += childW + layoutComp.spacing;
                    }

                    // Back-calculate anchoredPosition from target pixel position
                    float anchorCenterX = (childRect.anchorMin.x + childRect.anchorMax.x) * 0.5f * vw;
                    float anchorCenterY = ((1.0f - childRect.anchorMax.y) + (1.0f - childRect.anchorMin.y)) * 0.5f * vh;

                    childRect.anchoredPosition.x = (targetX + childRect.pivot.x * childW - anchorCenterX) / sc;
                    childRect.anchoredPosition.y = -((targetY + childRect.pivot.y * childH) - anchorCenterY) / sc;
                }
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

                const auto* scrollCanvas = findCanvasForEntity(registry, scrollEntity);
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

        // Helper lambda to emit one UIImage entity into drawList
        auto emitUIImage = [&](entt::entity entity, entt::entity scrollAncestor,
                               const components::UICanvasComponent* canvas)
        {
            const auto& imageComp = view.get<components::UIImageComponent>(entity);
            if (imageComp.texturePath.empty() && imageComp.colorTint.a < 0.01f)
                return;

            const auto& rectComp = view.get<components::UIRectComponent>(entity);

            float viewportW = static_cast<float>(ctx.viewportWidth);
            float viewportH = static_cast<float>(ctx.viewportHeight);
            float scale = 1.0f;
            if (canvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                scale = std::min(viewportW / canvas->referenceWidth, viewportH / canvas->referenceHeight);

            PixelRect rect = resolvePixelRect(rectComp, viewportW, viewportH, scale);

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
            else if (registry.all_of<components::UIScrollComponent>(entity))
            {
                auto it = scrollContainers.find(static_cast<uint32_t>(entity));
                if (it != scrollContainers.end())
                    scissor = it->second.scissorRect;
            }

            render::ui::UIImageRenderData renderData;
            renderData.texturePath = imageComp.texturePath.empty() ? "__white_1x1__" : imageComp.texturePath;
            renderData.position = glm::vec2(rect.x, rect.y);
            renderData.size = glm::vec2(rect.w, rect.h);
            renderData.colorTint = imageComp.colorTint;
            renderData.scissorRect = scissor;
            drawList.push_back(std::move(renderData));
        };

        // Pass 1: Scroll container backgrounds (must render before their children)
        for (auto entity : view)
        {
            if (!registry.all_of<components::UIScrollComponent>(entity))
                continue;

            if (registry.all_of<components::NameComponent>(entity))
                if (!registry.get<components::NameComponent>(entity).isActive)
                    continue;

            const auto* canvas = findCanvasForEntity(registry, entity);
            if (!canvas && registry.all_of<components::UICanvasComponent>(entity))
                canvas = &registry.get<components::UICanvasComponent>(entity);
            if (!canvas) continue;

            emitUIImage(entity, entt::null, canvas);
        }

        // Pass 2: All other UIImage entities (children of scroll containers, non-scroll elements)
        for (auto entity : view)
        {
            // Skip scroll containers (already emitted in pass 1)
            if (registry.all_of<components::UIScrollComponent>(entity))
                continue;

            if (registry.all_of<components::NameComponent>(entity))
                if (!registry.get<components::NameComponent>(entity).isActive)
                    continue;

            const auto& rectComp = view.get<components::UIRectComponent>(entity);

            // Walk parent hierarchy to find UICanvasComponent and nearest UIScrollComponent
            const components::UICanvasComponent* canvas = nullptr;
            entt::entity scrollAncestor = entt::null;
            entt::entity current = entity;
            while (registry.all_of<components::ParentComponent>(current))
            {
                entt::entity parentEntity = registry.get<components::ParentComponent>(current).parent;
                if (parentEntity == entt::null || !registry.valid(parentEntity))
                    break;

                if (scrollAncestor == entt::null
                    && registry.all_of<components::UIScrollComponent>(parentEntity))
                    scrollAncestor = parentEntity;

                if (registry.all_of<components::UICanvasComponent>(parentEntity))
                {
                    canvas = &registry.get<components::UICanvasComponent>(parentEntity);
                    break;
                }
                current = parentEntity;
            }

            if (!canvas && registry.all_of<components::UICanvasComponent>(entity))
                canvas = &registry.get<components::UICanvasComponent>(entity);

            if (!canvas) continue;

            emitUIImage(entity, scrollAncestor, canvas);
        }

        // --- Generate scrollbar draw data (rendered on top of content) ---
        {
            constexpr float SCROLLBAR_WIDTH = 8.0f;
            constexpr float SCROLLBAR_MIN_THUMB = 20.0f;
            const glm::vec4 TRACK_COLOR{0.2f, 0.2f, 0.2f, 0.3f};
            const glm::vec4 THUMB_COLOR{0.6f, 0.6f, 0.6f, 0.6f};
            const std::string whiteTex = "__white_1x1__";

            auto scrollBarView = registry.view<components::UIScrollComponent, components::UIRectComponent>();
            for (auto scrollEntity : scrollBarView)
            {
                if (registry.all_of<components::NameComponent>(scrollEntity))
                {
                    if (!registry.get<components::NameComponent>(scrollEntity).isActive)
                        continue;
                }

                const auto& scrollComp = registry.get<components::UIScrollComponent>(scrollEntity);

                const auto* scrollCanvas = findCanvasForEntity(registry, scrollEntity);
                if (!scrollCanvas)
                    continue;

                float vw = static_cast<float>(ctx.viewportWidth);
                float vh = static_cast<float>(ctx.viewportHeight);
                float sc = 1.0f;
                if (scrollCanvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                    sc = std::min(vw / scrollCanvas->referenceWidth, vh / scrollCanvas->referenceHeight);

                const auto& scrollRect = registry.get<components::UIRectComponent>(scrollEntity);
                PixelRect vpRect = resolvePixelRect(scrollRect, vw, vh, sc);

                glm::vec4 scrollScissor = scrollComp.computedScissorRect;

                // --- Vertical scrollbar ---
                bool showVertical = false;
                if (scrollComp.verticalScrollEnabled && scrollComp.contentSize.y > scrollComp.viewportSize.y)
                {
                    if (scrollComp.verticalScrollbarVisibility != components::ScrollbarVisibility::Hidden)
                        showVertical = true;
                }
                else if (scrollComp.verticalScrollbarVisibility == components::ScrollbarVisibility::AlwaysVisible)
                {
                    showVertical = true;
                }

                if (showVertical)
                {
                    render::ui::UIImageRenderData track;
                    track.texturePath = whiteTex;
                    track.position = glm::vec2(vpRect.x + vpRect.w - SCROLLBAR_WIDTH, vpRect.y);
                    track.size = glm::vec2(SCROLLBAR_WIDTH, vpRect.h);
                    track.colorTint = TRACK_COLOR;
                    track.scissorRect = scrollScissor;
                    drawList.push_back(std::move(track));

                    float maxScrollY = std::max(0.0f, scrollComp.contentSize.y - scrollComp.viewportSize.y);
                    float ratio = scrollComp.viewportSize.y / scrollComp.contentSize.y;
                    float thumbH = std::max(SCROLLBAR_MIN_THUMB, vpRect.h * ratio);
                    float scrollRange = vpRect.h - thumbH;
                    float thumbY = (maxScrollY > 0.0f)
                        ? vpRect.y + scrollRange * (scrollComp.scrollOffset.y / maxScrollY)
                        : vpRect.y;

                    render::ui::UIImageRenderData thumb;
                    thumb.texturePath = whiteTex;
                    thumb.position = glm::vec2(vpRect.x + vpRect.w - SCROLLBAR_WIDTH, thumbY);
                    thumb.size = glm::vec2(SCROLLBAR_WIDTH, thumbH);
                    thumb.colorTint = THUMB_COLOR;
                    thumb.scissorRect = scrollScissor;
                    drawList.push_back(std::move(thumb));
                }

                // --- Horizontal scrollbar ---
                bool showHorizontal = false;
                if (scrollComp.horizontalScrollEnabled && scrollComp.contentSize.x > scrollComp.viewportSize.x)
                {
                    if (scrollComp.horizontalScrollbarVisibility != components::ScrollbarVisibility::Hidden)
                        showHorizontal = true;
                }
                else if (scrollComp.horizontalScrollbarVisibility == components::ScrollbarVisibility::AlwaysVisible)
                {
                    showHorizontal = true;
                }

                if (showHorizontal)
                {
                    render::ui::UIImageRenderData track;
                    track.texturePath = whiteTex;
                    track.position = glm::vec2(vpRect.x, vpRect.y + vpRect.h - SCROLLBAR_WIDTH);
                    track.size = glm::vec2(vpRect.w, SCROLLBAR_WIDTH);
                    track.colorTint = TRACK_COLOR;
                    track.scissorRect = scrollScissor;
                    drawList.push_back(std::move(track));

                    float maxScrollX = std::max(0.0f, scrollComp.contentSize.x - scrollComp.viewportSize.x);
                    float ratio = scrollComp.viewportSize.x / scrollComp.contentSize.x;
                    float thumbW = std::max(SCROLLBAR_MIN_THUMB, vpRect.w * ratio);
                    float scrollRange = vpRect.w - thumbW;
                    float thumbX = (maxScrollX > 0.0f)
                        ? vpRect.x + scrollRange * (scrollComp.scrollOffset.x / maxScrollX)
                        : vpRect.x;

                    render::ui::UIImageRenderData thumb;
                    thumb.texturePath = whiteTex;
                    thumb.position = glm::vec2(thumbX, vpRect.y + vpRect.h - SCROLLBAR_WIDTH);
                    thumb.size = glm::vec2(thumbW, SCROLLBAR_WIDTH);
                    thumb.colorTint = THUMB_COLOR;
                    thumb.scissorRect = scrollScissor;
                    drawList.push_back(std::move(thumb));
                }
            }
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

    void FramePreparationSystem::prepareUILabels(const FrameContext& ctx)
    {
        auto* renderHandler = ctx.renderHandler;
        if (ctx.playModeActive)
        {
            prepareUILabelsScreenSpace(ctx);
        }
        else
        {
            // Editor mode: no screen-space label rendering yet
            renderHandler->setUITextDrawList({});
        }
    }

    void FramePreparationSystem::prepareUILabelsScreenSpace(const FrameContext& ctx)
    {
        auto* renderHandler = ctx.renderHandler;

        renderHandler->initUITextPipeline();

        if (!renderHandler->isUITextPipelineInitialized())
        {
            renderHandler->setUITextDrawList({});
            return;
        }

        if (ctx.viewportWidth == 0 || ctx.viewportHeight == 0)
        {
            renderHandler->setUITextDrawList({});
            return;
        }

        std::vector<render::ui::UITextRenderData> drawList;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::UILabelComponent, components::UIRectComponent>();

        float viewportW = static_cast<float>(ctx.viewportWidth);
        float viewportH = static_cast<float>(ctx.viewportHeight);

        // --- Pre-compute scroll container info (reads already-updated scrollOffset) ---
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

                const auto* scrollCanvas = findCanvasForEntity(registry, scrollEntity);
                if (!scrollCanvas)
                    continue;

                float vw = viewportW;
                float vh = viewportH;
                float sc = 1.0f;
                if (scrollCanvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                    sc = std::min(vw / scrollCanvas->referenceWidth, vh / scrollCanvas->referenceHeight);

                const auto& scrollRect = scrollView.get<components::UIRectComponent>(scrollEntity);
                PixelRect vpRect = resolvePixelRect(scrollRect, vw, vh, sc);

                const auto& scrollComp = registry.get<components::UIScrollComponent>(scrollEntity);

                float sx = std::max(0.0f, vpRect.x);
                float sy = std::max(0.0f, vpRect.y);
                float sw = std::max(0.0f, std::min(vpRect.x + vpRect.w, vw) - sx);
                float sh = std::max(0.0f, std::min(vpRect.y + vpRect.h, vh) - sy);
                glm::vec4 scissor(sx, sy, sw, sh);

                scrollContainers[static_cast<uint32_t>(scrollEntity)] = {scrollComp.scrollOffset, scissor};
            }
        }

        // --- Emit labels ---
        for (auto entity : view)
        {
            if (registry.all_of<components::NameComponent>(entity))
                if (!registry.get<components::NameComponent>(entity).isActive)
                    continue;

            const auto& labelComp = view.get<components::UILabelComponent>(entity);
            if (labelComp.text.empty() || labelComp.fontPath.empty())
                continue;

            const auto& rectComp = view.get<components::UIRectComponent>(entity);

            // Walk parent hierarchy for canvas + scroll ancestor
            const components::UICanvasComponent* canvas = nullptr;
            entt::entity scrollAncestor = entt::null;
            entt::entity current = entity;
            while (registry.all_of<components::ParentComponent>(current))
            {
                entt::entity parentEntity = registry.get<components::ParentComponent>(current).parent;
                if (parentEntity == entt::null || !registry.valid(parentEntity))
                    break;

                if (scrollAncestor == entt::null
                    && registry.all_of<components::UIScrollComponent>(parentEntity))
                    scrollAncestor = parentEntity;

                if (registry.all_of<components::UICanvasComponent>(parentEntity))
                {
                    canvas = &registry.get<components::UICanvasComponent>(parentEntity);
                    break;
                }
                current = parentEntity;
            }

            if (!canvas && registry.all_of<components::UICanvasComponent>(entity))
                canvas = &registry.get<components::UICanvasComponent>(entity);
            if (!canvas) continue;

            // Resolve pixel rect
            float scale = 1.0f;
            if (canvas->scaleMode == components::UIScaleMode::ScaleWithScreenSize)
                scale = std::min(viewportW / canvas->referenceWidth, viewportH / canvas->referenceHeight);

            PixelRect rect = resolvePixelRect(rectComp, viewportW, viewportH, scale);

            // Apply scroll offset + scissor
            glm::vec4 scissor{0.0f};
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

            // Build UITextRenderData
            render::ui::UITextRenderData renderData;
            renderData.fontPath = labelComp.fontPath;
            renderData.text = labelComp.text;
            renderData.fontSize = labelComp.fontSize;
            renderData.color = labelComp.color;
            renderData.lineSpacing = labelComp.lineSpacing;
            renderData.letterSpacing = labelComp.letterSpacing;
            renderData.wordWrap = labelComp.wordWrap;
            renderData.horizontalAlignment = static_cast<uint8_t>(labelComp.horizontalAlignment);
            renderData.verticalAlignment = static_cast<uint8_t>(labelComp.verticalAlignment);
            renderData.overflow = static_cast<uint8_t>(labelComp.overflow);
            renderData.position = glm::vec2(rect.x, rect.y);
            renderData.size = glm::vec2(rect.w, rect.h);
            renderData.scissorRect = scissor;
            drawList.push_back(std::move(renderData));
        }

        renderHandler->setUITextDrawList(std::move(drawList));
    }
}
