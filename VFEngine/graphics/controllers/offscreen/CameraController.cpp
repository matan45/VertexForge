#include "CameraController.hpp"
#include "../../render/RenderPassHandler.hpp"
#include "../../render/mesh/StaticMeshPipeline.hpp"
#include "../../render/billboard/BillboardPipeline.hpp"
#include "../../render/text/TextPipeline.hpp"
#include "../../render/occlusion/CameraOcclusionManager.hpp"
#include "../../render/gpudriven/GPUDrivenRenderer.hpp"
#include "../../render/postprocess/JitterSequence.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "print/Log.hpp"
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
            vfLogInfo("Switching active camera from {} to {}", previousId, id);
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
        // Save unjittered projection and apply TAA jitter if enabled
        unjitteredProjection = projection;
        glm::mat4 effectiveProjection = projection;
        currentJitterOffset = glm::vec2(0.0f);

        if (taaEnabled && cameraId == renderHandler.getActiveCameraId())
        {
            glm::vec2 jitter = render::postprocess::JitterSequence::halton23(taaFrameIndex % 16);
            currentJitterOffset = (jitter - 0.5f) * 2.0f; // center around 0

            effectiveProjection = render::postprocess::JitterSequence::applyJitter(
                projection, currentJitterOffset, viewportWidth, viewportHeight);
            taaFrameIndex++;
        }

        // Update mesh pipeline UBO only for the active camera (the one being rendered)
        if (cameraId == renderHandler.getActiveCameraId() && renderHandler.isMeshPipelineInitialized())
        {
            renderHandler.getMeshPipeline()->updateCameraUBO(view, effectiveProjection, cameraPos, time);

            // Store the current view matrix for cluster debug visualization
            currentViewMatrix = view;
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

        renderHandler.setDebugCameraMatrices(view, effectiveProjection);
        renderHandler.setUnjitteredProjection(unjitteredProjection);
        renderHandler.setTAAJitterData(currentJitterOffset, taaFrameIndex);

        if (renderHandler.isBillboardPipelineInitialized())
        {
            renderHandler.getBillboardPipeline()->updateCameraUBO(view, effectiveProjection, cameraPos);
        }

        if (renderHandler.isTextPipelineInitialized())
        {
            renderHandler.getTextPipeline()->updateCameraUBO(view, effectiveProjection, cameraPos);
        }

        // Get or create camera data
        auto* cameraManager = renderHandler.getCameraOcclusionManager();
        auto* cameraData = cameraManager->getCamera(cameraId);
        if (!cameraData)
        {
            return;
        }

        // Update camera's frustum
        cameraData->frustum.extractFromMatrix(effectiveProjection * view);
        cameraData->viewProj = effectiveProjection * view;
        cameraData->nearPlane = currentNearPlane;

        // Update camera data
        cameraManager->updateCamera(cameraId, effectiveProjection * view, currentNearPlane);

        // Keep backward compatibility for main camera ready flag
        if (cameraId == types::MAIN_CAMERA_ID)
        {
            occlusionCullingReady = cameraData->hiZInitialized;
            currentViewProj = effectiveProjection * view;
            currentFrustum = cameraData->frustum;
        }
    }
}
