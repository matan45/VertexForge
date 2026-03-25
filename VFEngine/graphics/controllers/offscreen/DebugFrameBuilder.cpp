#include "DebugFrameBuilder.hpp"
#include "FramePreparationSystem.hpp"
#include "CameraController.hpp"
#include "../../render/RenderPassHandler.hpp"
#include "../../render/DebugRenderer.hpp"
#include "../../render/tools/FrustumDebugRenderer.hpp"
#include "../../render/tools/AudioSphereDebugRenderer.hpp"
#include "../../render/tools/PhysicsDebugRenderer.hpp"
#include "../../render/tools/UICanvasDebugRenderer.hpp"
#include "scene/EntityRegistry.hpp"
#include "scene/Entity.hpp"
#include "components/Components.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/render/DebugDrawEvents.hpp"
#include <glm/gtc/matrix_transform.hpp>
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
            if (!scene::Entity::isEffectivelyActive(registry, entity)) continue;
            const auto& cameraComp = view.get<components::CameraComponent>(entity);
            if (!cameraComp.showFrustum) continue;
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            render::mesh::CameraFrustumRenderData renderData;
            renderData.projectionMatrix = cameraComp.projectionMatrix;
            renderData.worldMatrix = worldTransform.worldMatrix;
            renderData.showFrustum = cameraComp.showFrustum;
            frustumDrawList.push_back(renderData);
        }

        if (!frustumDrawList.empty())
        {
            if (!renderHandler->isMeshPipelineInitialized()) renderHandler->initMeshPipeline();
            if (!renderHandler->isDebugRendererInitialized()) renderHandler->initDebugRenderer();
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
            if (!scene::Entity::isEffectivelyActive(registry, entity)) continue;
            const auto& audioComp = view.get<components::AudioSource3DComponent>(entity);
            if (!audioComp.showDebugSpheres && !audioComp.showDebugCone) continue;
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

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
            if (!renderHandler->isMeshPipelineInitialized()) renderHandler->initMeshPipeline();
            if (!renderHandler->isDebugRendererInitialized()) renderHandler->initDebugRenderer();
        }
        renderHandler->setAudioSphereDrawList(std::move(audioSphereDrawList));
    }

    void DebugFrameBuilder::prepareReverbZones(const FrameContext& ctx)
    {
        if (ctx.playModeActive || !ctx.showDebugRendering) return;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto& dispatcher = events::EventDispatcher::instance();
        auto view = registry.view<components::ReverbZoneComponent, components::WorldTransformComponent>();

        for (auto entity : view)
        {
            if (!scene::Entity::isEffectivelyActive(registry, entity)) continue;
            const auto& zone = view.get<components::ReverbZoneComponent>(entity);
            if (!zone.showDebugVolume) continue;
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);
            glm::vec3 position = glm::vec3(worldTransform.worldMatrix[3]);

            glm::vec4 innerColor(0.0f, 0.8f, 0.8f, 1.0f);
            glm::vec4 outerColor(0.0f, 0.4f, 0.4f, 1.0f);

            if (zone.shape == components::ReverbZoneShape::Sphere)
            {
                events::debugdraw::DrawSphereCommand innerCmd;
                innerCmd.center = position;
                innerCmd.radius = zone.radius;
                innerCmd.color = innerColor;
                dispatcher.execute(innerCmd);

                if (zone.falloffDistance > 0.0f)
                {
                    events::debugdraw::DrawSphereCommand outerCmd;
                    outerCmd.center = position;
                    outerCmd.radius = zone.radius + zone.falloffDistance;
                    outerCmd.color = outerColor;
                    dispatcher.execute(outerCmd);
                }
            }
            else
            {
                events::debugdraw::DrawBoxCommand innerCmd;
                innerCmd.center = position;
                innerCmd.halfExtents = zone.halfExtents;
                innerCmd.color = innerColor;
                dispatcher.execute(innerCmd);

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

    void DebugFrameBuilder::prepareFogVolumes(const FrameContext& ctx)
    {
        if (ctx.playModeActive || !ctx.showDebugRendering) return;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto& dispatcher = events::EventDispatcher::instance();
        auto view = registry.view<components::FogVolumeComponent, components::WorldTransformComponent>();

        for (auto entity : view)
        {
            if (!scene::Entity::isEffectivelyActive(registry, entity)) continue;
            const auto& fog = view.get<components::FogVolumeComponent>(entity);
            if (!fog.showGizmo) continue;
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);
            glm::vec3 position = glm::vec3(worldTransform.worldMatrix[3]);

            glm::vec4 innerColor(0.4f, 0.7f, 1.0f, 1.0f);
            glm::vec4 outerColor(0.2f, 0.35f, 0.5f, 1.0f);

            if (fog.shape == components::FogVolumeShape::Sphere)
            {
                // Use max half-extent as radius for sphere visualization
                float radius = glm::max(fog.halfExtents.x, glm::max(fog.halfExtents.y, fog.halfExtents.z));
                events::debugdraw::DrawSphereCommand innerCmd;
                innerCmd.center = position;
                innerCmd.radius = radius;
                innerCmd.color = innerColor;
                dispatcher.execute(innerCmd);

                if (fog.edgeFalloff > 0.0f)
                {
                    events::debugdraw::DrawSphereCommand outerCmd;
                    outerCmd.center = position;
                    outerCmd.radius = radius * (1.0f + fog.edgeFalloff);
                    outerCmd.color = outerColor;
                    dispatcher.execute(outerCmd);
                }
            }
            else
            {
                // Box and Cylinder both use box gizmo for bounds
                events::debugdraw::DrawBoxCommand innerCmd;
                innerCmd.center = position;
                innerCmd.halfExtents = fog.halfExtents;
                innerCmd.color = innerColor;
                dispatcher.execute(innerCmd);

                if (fog.edgeFalloff > 0.0f)
                {
                    events::debugdraw::DrawBoxCommand outerCmd;
                    outerCmd.center = position;
                    outerCmd.halfExtents = fog.halfExtents * (1.0f + fog.edgeFalloff);
                    outerCmd.color = outerColor;
                    dispatcher.execute(outerCmd);
                }
            }
        }
    }

    void DebugFrameBuilder::prepareGrid(const FrameContext& ctx)
    {
        if (!ctx.showGrid || ctx.playModeActive) return;

        auto* renderHandler = ctx.renderHandler;
        if (!renderHandler->isMeshPipelineInitialized()) renderHandler->initMeshPipeline();
        if (!renderHandler->isDebugRendererInitialized())
        {
            renderHandler->initDebugRenderer();
            renderHandler->getDebugRenderer()->setShowGrid(true);
        }
    }

    void DebugFrameBuilder::collectStandardColliders(std::vector<render::mesh::PhysicsColliderRenderData>& drawList)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::ColliderComponent, components::WorldTransformComponent>();

        for (auto entity : view)
        {
            if (!scene::Entity::isEffectivelyActive(registry, entity)) continue;
            const auto& colliderComp = view.get<components::ColliderComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            render::mesh::PhysicsColliderRenderData renderData;
            renderData.worldMatrix = worldTransform.worldMatrix * glm::translate(glm::mat4(1.0f), colliderComp.offset);
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
                ? static_cast<uint8_t>(registry.get<components::RigidBodyComponent>(entity).type) : 0;
            drawList.push_back(renderData);
        }
    }

    void DebugFrameBuilder::collectTerrainColliders(std::vector<render::mesh::PhysicsColliderRenderData>& drawList)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto terrainDebugView = registry.view<components::TerrainTileColliderDebugComponent>();

        for (auto entity : terrainDebugView)
        {
            const auto& debugComp = terrainDebugView.get<components::TerrainTileColliderDebugComponent>(entity);
            if (debugComp.debugData.vertices.empty() || debugComp.debugData.lineIndices.empty()) continue;

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
            if (!renderHandler->isMeshPipelineInitialized()) renderHandler->initMeshPipeline();
            if (!renderHandler->isDebugRendererInitialized()) renderHandler->initDebugRenderer();
        }
        renderHandler->setPhysicsColliderDrawList(std::move(colliderDrawList));
    }

    void DebugFrameBuilder::collectUIRectOutlines(std::vector<render::mesh::UICanvasOutlineRenderData>& drawList)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto rectView = registry.view<components::UIRectComponent>();

        for (auto entity : rectView)
        {
            if (!scene::Entity::isEffectivelyActive(registry, entity)) continue;
            if (registry.all_of<components::UICanvasComponent>(entity)) continue;

            const auto& rectComp = rectView.get<components::UIRectComponent>(entity);

            const components::UICanvasComponent* canvas = nullptr;
            entt::entity canvasEntity = entt::null;
            entt::entity current = entity;

            while (registry.all_of<components::ParentComponent>(current))
            {
                entt::entity parentEntity = registry.get<components::ParentComponent>(current).parent;
                if (parentEntity == entt::null || !registry.valid(parentEntity)) break;
                if (registry.all_of<components::UICanvasComponent>(parentEntity))
                {
                    canvas = &registry.get<components::UICanvasComponent>(parentEntity);
                    canvasEntity = parentEntity;
                    break;
                }
                current = parentEntity;
            }

            if (!canvas || canvasEntity == entt::null) continue;
            if (!registry.all_of<components::WorldTransformComponent>(canvasEntity)) continue;

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

            drawList.push_back({rectModel});
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
            if (!scene::Entity::isEffectivelyActive(registry, entity)) continue;
            const auto& canvasComp = view.get<components::UICanvasComponent>(entity);
            const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

            float width = canvasComp.referenceWidth / canvasComp.pixelsPerUnit;
            float height = canvasComp.referenceHeight / canvasComp.pixelsPerUnit;

            drawList.push_back({worldTransform.worldMatrix * glm::scale(glm::mat4(1.0f), glm::vec3(width, height, 1.0f))});
        }

        collectUIRectOutlines(drawList);

        if (!drawList.empty())
        {
            if (!renderHandler->isMeshPipelineInitialized()) renderHandler->initMeshPipeline();
            if (!renderHandler->isDebugRendererInitialized()) renderHandler->initDebugRenderer();
        }
        renderHandler->setUICanvasOutlineDrawList(std::move(drawList));
    }
}
