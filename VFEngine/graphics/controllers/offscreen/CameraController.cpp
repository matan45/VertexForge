#include "CameraController.hpp"
#include "../../render/RenderPassHandler.hpp"
#include "../../render/mesh/StaticMeshPipeline.hpp"
#include "../../render/billboard/BillboardPipeline.hpp"
#include "../../render/text/TextPipeline.hpp"
#include "../../render/occlusion/CameraOcclusionManager.hpp"
#include "../../render/gpudriven/GPUDrivenRenderer.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "print/Logger.hpp"
#include <cmath>

namespace controllers::offscreen
{
    CameraController::CameraController(render::RenderPassHandler& renderHandler)
        : renderHandler{renderHandler}
    {
    }

    void CameraController::create(types::CameraId id, bool enableOcclusion)
    {
        renderHandler.createCamera(id, enableOcclusion);
    }

    void CameraController::remove(types::CameraId id)
    {
        renderHandler.removeCamera(id);
    }

    void CameraController::setActive(types::CameraId id)
    {
        auto previousId = renderHandler.getActiveCameraId();
        if (previousId != id)
        {
            loggerInfo("Switching active camera from {} to {}", previousId, id);
        }
        renderHandler.setActiveCamera(id);
    }

    types::CameraId CameraController::getActiveId() const
    {
        return renderHandler.getActiveCameraId();
    }

    void CameraController::prepareCameras()
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::CameraComponent, components::WorldTransformComponent>();

        for (auto entity : view)
        {
            auto& camComp = view.get<components::CameraComponent>(entity);
            if (!camComp.isRegistered)
            {
                create(camComp.cameraId, camComp.enableOcclusionCulling);
                camComp.isRegistered = true;
            }
        }
    }

    void CameraController::updateCamera(types::CameraId cameraId,
                                         const glm::mat4& view, const glm::mat4& projection,
                                         const glm::vec3& cameraPos, float time)
    {
        // Update mesh pipeline UBO only for the active camera (the one being rendered)
        if (cameraId == renderHandler.getActiveCameraId() && renderHandler.isMeshPipelineInitialized())
        {
            renderHandler.getMeshPipeline()->updateCameraUBO(view, projection, cameraPos, time);

            // Store the current view matrix for cluster debug visualization
            currentViewMatrix = view;

            if (!renderHandler.isDebugRendererInitialized())
            {
                renderHandler.initDebugRenderer();
            }
        }

        // Update GPU-driven renderer camera data
        // Extract far plane from projection matrix for perspective projection
        float farPlane = 1000.0f;  // Default fallback
        if (std::abs(projection[2][2]) > 0.0001f)
        {
            float nearEstimate = projection[3][2] / projection[2][2];
            if (nearEstimate > 0.0f && std::abs(projection[2][2] + 1.0f) > 0.0001f)
            {
                farPlane = projection[3][2] / (projection[2][2] + 1.0f);
                if (farPlane < 0.0f) farPlane = 1000.0f;
            }
        }
        renderHandler.setGPUDrivenCameraData(cameraPos, currentNearPlane, farPlane, time);

        renderHandler.setDebugCameraMatrices(view, projection);

        if (renderHandler.isBillboardPipelineInitialized())
        {
            renderHandler.getBillboardPipeline()->updateCameraUBO(view, projection, cameraPos);
        }

        if (renderHandler.isTextPipelineInitialized())
        {
            renderHandler.getTextPipeline()->updateCameraUBO(view, projection, cameraPos);
        }

        // Get or create camera data
        auto* cameraManager = renderHandler.getCameraOcclusionManager();
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
        if (cameraId == types::MAIN_CAMERA_ID)
        {
            occlusionCullingReady = cameraData->occlusionInitialized;
            currentViewProj = projection * view;
            currentFrustum = cameraData->frustum;
        }
    }
}
