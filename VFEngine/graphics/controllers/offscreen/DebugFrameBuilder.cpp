#include "DebugFrameBuilder.hpp"
#include "FramePreparationSystem.hpp"  // for FrameContext
#include "LightBVHManager.hpp"
#include "CameraController.hpp"
#include "../../render/RenderPassHandler.hpp"
#include "../../render/mesh/StaticMeshPipeline.hpp"
#include "../../render/DebugRenderer.hpp"
#include "../../render/tools/FrustumDebugRenderer.hpp"
#include "../../render/tools/AudioSphereDebugRenderer.hpp"
#include "../../render/tools/PhysicsDebugRenderer.hpp"
#include "../../render/tools/LightGizmoDebugRenderer.hpp"
#include "../../render/tools/ClusterDebugRenderer.hpp"
#include "../../render/tools/UICanvasDebugRenderer.hpp"
#include "../../render/gpudriven/GPUDrivenRenderer.hpp"
#include "../../render/lighting/ClusterGridManager.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/render/DebugDrawEvents.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace controllers::offscreen
{
    void DebugFrameBuilder::prepareCameraFrustums(const FrameContext& ctx)
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

    void DebugFrameBuilder::prepareAudioSpheres(const FrameContext& ctx)
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

            if (!audioComp.showDebugSpheres && !audioComp.showDebugCone)
            {
                continue;
            }

            render::mesh::AudioSphereRenderData renderData;
            renderData.position = glm::vec3(worldTransform.worldMatrix[3]);
            renderData.minDistance = audioComp.minDistance;
            renderData.maxDistance = audioComp.maxDistance;
            renderData.showDebugSpheres = audioComp.showDebugSpheres;
            renderData.innerConeAngle = audioComp.innerConeAngle;
            renderData.outerConeAngle = audioComp.outerConeAngle;
            renderData.showDebugCone = audioComp.showDebugCone;

            if (registry.all_of<components::TransformComponent>(entity))
            {
                const auto& transform = registry.get<components::TransformComponent>(entity);
                float yawRad = glm::radians(transform.rotation.y);
                float pitchRad = glm::radians(transform.rotation.x);
                glm::vec3 forward;
                forward.x = -std::sin(yawRad) * std::cos(pitchRad);
                forward.y = std::sin(pitchRad);
                forward.z = -std::cos(yawRad) * std::cos(pitchRad);
                renderData.direction = glm::normalize(forward);
            }

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

    void DebugFrameBuilder::prepareReverbZones(const FrameContext& ctx)
    {
        if (ctx.playModeActive || !ctx.showDebugRendering)
        {
            return;
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        auto& dispatcher = events::EventDispatcher::instance();
        auto view = registry.view<components::ReverbZoneComponent, components::WorldTransformComponent>();

        for (auto entity : view)
        {
            if (registry.all_of<components::NameComponent>(entity))
            {
                const auto& nameComp = registry.get<components::NameComponent>(entity);
                if (!nameComp.isActive) continue;
            }

            const auto& zone = view.get<components::ReverbZoneComponent>(entity);
            if (!zone.showDebugVolume) continue;

            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);
            glm::vec3 position = glm::vec3(worldTransform.worldMatrix[3]);

            glm::vec4 innerColor(0.0f, 0.8f, 0.8f, 1.0f); // Cyan for zone boundary
            glm::vec4 outerColor(0.0f, 0.4f, 0.4f, 1.0f); // Darker cyan for falloff

            if (zone.shape == components::ReverbZoneShape::Sphere)
            {
                // Inner zone boundary
                events::debugdraw::DrawSphereCommand innerCmd;
                innerCmd.center = position;
                innerCmd.radius = zone.radius;
                innerCmd.color = innerColor;
                dispatcher.execute(innerCmd);

                // Outer falloff boundary
                if (zone.falloffDistance > 0.0f)
                {
                    events::debugdraw::DrawSphereCommand outerCmd;
                    outerCmd.center = position;
                    outerCmd.radius = zone.radius + zone.falloffDistance;
                    outerCmd.color = outerColor;
                    dispatcher.execute(outerCmd);
                }
            }
            else // Box
            {
                // Inner zone boundary
                events::debugdraw::DrawBoxCommand innerCmd;
                innerCmd.center = position;
                innerCmd.halfExtents = zone.halfExtents;
                innerCmd.color = innerColor;
                dispatcher.execute(innerCmd);

                // Outer falloff boundary
                if (zone.falloffDistance > 0.0f)
                {
                    events::debugdraw::DrawBoxCommand outerCmd;
                    outerCmd.center = position;
                    outerCmd.halfExtents = zone.halfExtents + glm::vec3(zone.falloffDistance);
                    outerCmd.color = outerColor;
                    dispatcher.execute(outerCmd);
                }
            }
        }
    }

    void DebugFrameBuilder::prepareGrid(const FrameContext& ctx)
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

    void DebugFrameBuilder::collectStandardColliders(
        std::vector<render::mesh::PhysicsColliderRenderData>& drawList)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::ColliderComponent, components::WorldTransformComponent>();

        for (auto entity : view)
        {
            if (registry.all_of<components::NameComponent>(entity))
            {
                if (!registry.get<components::NameComponent>(entity).isActive)
                    continue;
            }

            const auto& colliderComp = view.get<components::ColliderComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            render::mesh::PhysicsColliderRenderData renderData;
            renderData.worldMatrix = worldTransform.worldMatrix
                * glm::translate(glm::mat4(1.0f), colliderComp.offset);
            renderData.shape = colliderComp.shape;
            renderData.size = colliderComp.size;
            renderData.radius = colliderComp.size.x;
            renderData.height = colliderComp.height;
            renderData.isTrigger = colliderComp.isTrigger;

            if (colliderComp.shape == components::ColliderShape::ConvexMesh ||
                colliderComp.shape == components::ColliderShape::TriangleMesh)
            {
                if (colliderComp.meshRef.isValid())
                    renderData.meshPath = colliderComp.meshRef.resolve();
                else if (registry.all_of<components::MeshComponent>(entity))
                    renderData.meshPath = registry.get<components::MeshComponent>(entity).meshRef.resolve();
            }

            renderData.bodyType = registry.all_of<components::RigidBodyComponent>(entity)
                ? static_cast<uint8_t>(registry.get<components::RigidBodyComponent>(entity).type)
                : 0;

            drawList.push_back(renderData);
        }
    }

    void DebugFrameBuilder::collectTerrainColliders(
        std::vector<render::mesh::PhysicsColliderRenderData>& drawList)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto terrainDebugView = registry.view<components::TerrainTileColliderDebugComponent>();

        for (auto entity : terrainDebugView)
        {
            const auto& debugComp = terrainDebugView.get<components::TerrainTileColliderDebugComponent>(entity);
            if (debugComp.debugData.vertices.empty() || debugComp.debugData.lineIndices.empty())
                continue;

            render::mesh::PhysicsColliderRenderData renderData;
            renderData.worldMatrix = glm::mat4(1.0f);
            renderData.shape = types::ColliderShape::HeightField;
            renderData.bodyType = 0;
            renderData.heightfieldVertices = &debugComp.debugData.vertices;
            renderData.heightfieldLineIndices = &debugComp.debugData.lineIndices;
            renderData.heightfieldCacheKey = std::to_string(static_cast<uint32_t>(entity)) + "_"
                + std::to_string(debugComp.tileX) + "_" + std::to_string(debugComp.tileZ);
            renderData.heightfieldVersion = debugComp.debugData.version;

            drawList.push_back(renderData);
        }
    }

    void DebugFrameBuilder::preparePhysicsColliders(const FrameContext& ctx)
    {
        auto* renderHandler = ctx.renderHandler;

        if (!ctx.showPhysicsDebug)
        {
            renderHandler->setPhysicsColliderDrawList({});
            return;
        }

        std::vector<render::mesh::PhysicsColliderRenderData> colliderDrawList;
        collectStandardColliders(colliderDrawList);
        collectTerrainColliders(colliderDrawList);
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

    void DebugFrameBuilder::collectDirectionalLightGizmos(
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

    void DebugFrameBuilder::collectPointLightGizmos(
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

    void DebugFrameBuilder::collectSpotLightGizmos(
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

    void DebugFrameBuilder::prepareLightGizmos(const FrameContext& ctx)
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

    void DebugFrameBuilder::collectLightClusterHighlights(
        const FrameContext& ctx,
        render::mesh::ClusterDebugRenderData& debugData)
    {
        glm::mat4 viewMatrix = ctx.cameraController->getCurrentViewMatrix();

        auto* clusterGridManager = ctx.renderHandler->getGPUDrivenRenderer()->getClusterGridManager();
        auto& registry = scene::EntityRegistry::getRegistry();

        auto pointView = registry.view<components::PointLightComponent, components::WorldTransformComponent>();
        for (auto entity : pointView)
        {
            const auto& lightComp = pointView.get<components::PointLightComponent>(entity);
            if (!lightComp.showGizmo)
                continue;

            const auto& worldTransform = pointView.get<components::WorldTransformComponent>(entity);
            glm::vec3 viewPos = glm::vec3(viewMatrix * glm::vec4(glm::vec3(worldTransform.worldMatrix[3]), 1.0f));

            auto affected = clusterGridManager->getClusterIndicesForPointLight(viewPos, lightComp.radius);
            debugData.highlightedClusterIndices.insert(
                debugData.highlightedClusterIndices.end(), affected.begin(), affected.end());
        }

        auto spotView = registry.view<components::SpotLightComponent, components::WorldTransformComponent>();
        for (auto entity : spotView)
        {
            const auto& lightComp = spotView.get<components::SpotLightComponent>(entity);
            if (!lightComp.showGizmo)
                continue;

            const auto& worldTransform = spotView.get<components::WorldTransformComponent>(entity);
            glm::vec3 worldPos = glm::vec3(worldTransform.worldMatrix[3]);
            glm::vec3 worldDir = glm::normalize(glm::vec3(worldTransform.worldMatrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));

            glm::vec3 viewPos = glm::vec3(viewMatrix * glm::vec4(worldPos, 1.0f));
            glm::vec3 viewDir = glm::normalize(glm::vec3(viewMatrix * glm::vec4(worldDir, 0.0f)));

            float outerAngleCos = std::cos(glm::radians(lightComp.outerAngle));
            auto affected = clusterGridManager->getClusterIndicesForSpotLight(
                viewPos, viewDir, lightComp.range, outerAngleCos);
            debugData.highlightedClusterIndices.insert(
                debugData.highlightedClusterIndices.end(), affected.begin(), affected.end());
        }
    }

    void DebugFrameBuilder::prepareClusterDebug(const FrameContext& ctx)
    {
        auto* renderHandler = ctx.renderHandler;
        if (!renderHandler)
            return;

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
            return;

        auto* clusterGridManager = gpuRenderer->getClusterGridManager();
        if (!clusterGridManager || !clusterGridManager->isInitialized())
            return;

        glm::mat4 viewMatrix = ctx.cameraController->getCurrentViewMatrix();

        render::mesh::ClusterDebugRenderData debugData;
        debugData.clusterAABBs = clusterGridManager->getClusterAABBs();
        debugData.invViewMatrix = glm::inverse(viewMatrix);

        collectLightClusterHighlights(ctx, debugData);

        debugData.showAllClusters = debugData.highlightedClusterIndices.empty();
        renderHandler->setClusterDebugData(std::move(debugData));
    }

    void DebugFrameBuilder::collectUIRectOutlines(
        std::vector<render::mesh::UICanvasOutlineRenderData>& drawList)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto rectView = registry.view<components::UIRectComponent>();

        for (auto entity : rectView)
        {
            if (registry.all_of<components::NameComponent>(entity))
            {
                if (!registry.get<components::NameComponent>(entity).isActive)
                    continue;
            }

            if (registry.all_of<components::UICanvasComponent>(entity))
                continue;

            const auto& rectComp = rectView.get<components::UIRectComponent>(entity);

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
    }

    void DebugFrameBuilder::prepareUICanvasOutlines(const FrameContext& ctx)
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
                    continue;
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

        collectUIRectOutlines(drawList);

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
}
