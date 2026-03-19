#include "DebugFrameBuilder.hpp"
#include "FramePreparationSystem.hpp"
#include "LightBVHManager.hpp"
#include "CameraController.hpp"
#include "../../render/RenderPassHandler.hpp"
#include "../../render/DebugRenderer.hpp"
#include "../../render/gpudriven/GPUDrivenRenderer.hpp"
#include "../../render/lighting/ClusterGridManager.hpp"
#include "../../render/tools/ClusterDebugRenderer.hpp"
#include "scene/EntityRegistry.hpp"
#include "scene/Entity.hpp"
#include "components/Components.hpp"
#include <cmath>

namespace controllers::offscreen
{
    void DebugFrameBuilder::collectDirectionalLightGizmos(
        std::vector<render::mesh::LightGizmoRenderData>& drawList)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::DirectionalLightComponent, components::WorldTransformComponent>();

        for (auto entity : view)
        {
            if (!scene::Entity::isEffectivelyActive(registry, entity)) continue;

            const auto& lightComp = view.get<components::DirectionalLightComponent>(entity);
            if (!lightComp.showGizmo) continue;

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
            if (!scene::Entity::isEffectivelyActive(registry, entity)) continue;

            const auto& lightComp = view.get<components::PointLightComponent>(entity);
            if (!lightComp.showGizmo) continue;

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
            if (!scene::Entity::isEffectivelyActive(registry, entity)) continue;

            const auto& lightComp = view.get<components::SpotLightComponent>(entity);
            if (!lightComp.showGizmo) continue;

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

        if (ctx.lightBvhManager) ctx.lightBvhManager->update();

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
            if (!renderHandler->isMeshPipelineInitialized()) renderHandler->initMeshPipeline();
            if (!renderHandler->isDebugRendererInitialized()) renderHandler->initDebugRenderer();
        }

        renderHandler->setLightGizmoDrawList(std::move(lightGizmoDrawList));
    }

    void DebugFrameBuilder::collectLightClusterHighlights(
        const FrameContext& ctx, render::mesh::ClusterDebugRenderData& debugData)
    {
        glm::mat4 viewMatrix = ctx.cameraController->getCurrentViewMatrix();
        auto* clusterGridManager = ctx.renderHandler->getGPUDrivenRenderer()->getClusterGridManager();
        auto& registry = scene::EntityRegistry::getRegistry();

        auto pointView = registry.view<components::PointLightComponent, components::WorldTransformComponent>();
        for (auto entity : pointView)
        {
            const auto& lightComp = pointView.get<components::PointLightComponent>(entity);
            if (!lightComp.showGizmo) continue;

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
            if (!lightComp.showGizmo) continue;

            const auto& worldTransform = spotView.get<components::WorldTransformComponent>(entity);
            glm::vec3 worldPos = glm::vec3(worldTransform.worldMatrix[3]);
            glm::vec3 worldDir = glm::normalize(glm::vec3(worldTransform.worldMatrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));

            glm::vec3 viewPos = glm::vec3(viewMatrix * glm::vec4(worldPos, 1.0f));
            glm::vec3 viewDir = glm::normalize(glm::vec3(viewMatrix * glm::vec4(worldDir, 0.0f)));

            float outerAngleCos = std::cos(glm::radians(lightComp.outerAngle));
            auto affected = clusterGridManager->getClusterIndicesForSpotLight(viewPos, viewDir, lightComp.range, outerAngleCos);
            debugData.highlightedClusterIndices.insert(
                debugData.highlightedClusterIndices.end(), affected.begin(), affected.end());
        }
    }

    void DebugFrameBuilder::prepareClusterDebug(const FrameContext& ctx)
    {
        auto* renderHandler = ctx.renderHandler;
        if (!renderHandler) return;

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
        if (!gpuRenderer) return;

        auto* clusterGridManager = gpuRenderer->getClusterGridManager();
        if (!clusterGridManager || !clusterGridManager->isInitialized()) return;

        glm::mat4 viewMatrix = ctx.cameraController->getCurrentViewMatrix();

        render::mesh::ClusterDebugRenderData debugData;
        debugData.clusterAABBs = clusterGridManager->getClusterAABBs();
        debugData.invViewMatrix = glm::inverse(viewMatrix);

        collectLightClusterHighlights(ctx, debugData);

        debugData.showAllClusters = debugData.highlightedClusterIndices.empty();
        renderHandler->setClusterDebugData(std::move(debugData));
    }
}
