#pragma once
#include <glm/glm.hpp>
#include "math/Frustum.hpp"
#include "types/CameraTypes.hpp"

namespace render
{
    class RenderPassHandler;
}

namespace controllers::offscreen
{
    class CameraController
    {
    private:
        render::RenderPassHandler& renderHandler;

        glm::mat4 currentViewProj{1.0f};
        glm::mat4 currentViewMatrix{1.0f};
        float currentNearPlane = 0.1f;
        bool occlusionCullingEnabled = true;
        bool occlusionCullingReady = false;
        math::Frustum currentFrustum;

        bool taaEnabled = false;
        uint32_t taaFrameIndex = 0;
        glm::mat4 unjitteredProjection{1.0f};
        glm::vec2 currentJitterOffset{0.0f};
        // VK-1662: the jitter is measured in RENDER pixels (the upscaler's input); the display extent only picks
        // the Halton phase count (JitterSequence::phaseCount).
        uint32_t renderWidth = 1920;
        uint32_t renderHeight = 1080;
        uint32_t displayWidth = 1920;
        uint32_t displayHeight = 1080;
    public:
        explicit CameraController(render::RenderPassHandler& renderHandler);

        void create(types::CameraId id, bool enableOcclusion = false);
        void remove(types::CameraId id);
        void setActive(types::CameraId id);
        types::CameraId getActiveId() const;

        void prepareCameras();
        void updateCamera(types::CameraId cameraId, const glm::mat4& view,
                         const glm::mat4& projection, const glm::vec3& cameraPos, float time = 0.0f);

        void setOcclusionCullingEnabled(bool enabled) { occlusionCullingEnabled = enabled; }
        bool isOcclusionCullingEnabled() const { return occlusionCullingEnabled; }
        bool isOcclusionCullingReady() const { return occlusionCullingReady; }

        const math::Frustum& getCurrentFrustum() const { return currentFrustum; }
        const glm::mat4& getCurrentViewProj() const { return currentViewProj; }
        const glm::mat4& getCurrentViewMatrix() const { return currentViewMatrix; }
        float getCurrentNearPlane() const { return currentNearPlane; }

        void setTAAEnabled(bool enabled) { taaEnabled = enabled; if (!enabled) taaFrameIndex = 0; }
        void setJitterExtents(uint32_t renderW, uint32_t renderH, uint32_t displayW, uint32_t displayH)
        {
            renderWidth = renderW;
            renderHeight = renderH;
            displayWidth = displayW;
            displayHeight = displayH;
        }
        const glm::mat4& getUnjitteredProjection() const { return unjitteredProjection; }
        uint32_t getTAAFrameIndex() const { return taaFrameIndex; }
        const glm::vec2& getCurrentJitterOffset() const { return currentJitterOffset; }
    };
}
