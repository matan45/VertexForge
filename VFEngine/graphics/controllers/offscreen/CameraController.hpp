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

    private:
        render::RenderPassHandler& renderHandler;

        glm::mat4 currentViewProj{1.0f};
        glm::mat4 currentViewMatrix{1.0f};
        float currentNearPlane = 0.1f;
        bool occlusionCullingEnabled = true;
        bool occlusionCullingReady = false;
        math::Frustum currentFrustum;
    };
}
