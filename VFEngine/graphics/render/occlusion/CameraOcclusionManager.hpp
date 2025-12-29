#pragma once
#include "HiZBuffer.hpp"
#include "OcclusionCullingManager.hpp"
#include "math/Frustum.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <cstdint>
#include <unordered_map>
#include "../../../utilities/types/CameraTypes.hpp"

namespace core
{
    class Device;
    class SwapChain;
}

namespace render::occlusion
{
    using types::CameraId;
    using types::MAIN_CAMERA_ID;
    using types::INVALID_CAMERA_ID;

    // Per-camera rendering data for occlusion culling
    struct CameraRenderData
    {
        CameraId cameraId = INVALID_CAMERA_ID;

        // Occlusion culling resources
        std::unique_ptr<HiZBuffer> hiZBuffer;
        std::unique_ptr<OcclusionCullingManager> occlusionManager;

        math::Frustum frustum;
        glm::mat4 viewProj{1.0f};
        float nearPlane = 0.1f;

        bool useOcclusionCulling = true;
        bool hiZInitialized = false;
        bool occlusionInitialized = false;

        CameraRenderData() = default;

        CameraRenderData(CameraId id) : cameraId(id)
        {
        }

        // Non-copyable, movable
        CameraRenderData(const CameraRenderData&) = delete;
        CameraRenderData& operator=(const CameraRenderData&) = delete;
        CameraRenderData(CameraRenderData&&) = default;
        CameraRenderData& operator=(CameraRenderData&&) = default;

        bool isValid() const { return cameraId != INVALID_CAMERA_ID; }
        bool isMainCamera() const { return cameraId == MAIN_CAMERA_ID; }
    };

    class CameraOcclusionManager
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        std::unordered_map<CameraId, std::unique_ptr<CameraRenderData>> cameras;
        CameraId activeCameraId = MAIN_CAMERA_ID;

    public:
        explicit CameraOcclusionManager(core::Device& device, core::SwapChain& swapChain);
        ~CameraOcclusionManager();

        CameraRenderData* createCamera(CameraId id, bool enableOcclusion = true);
        CameraRenderData* getCamera(CameraId id);
        void removeCamera(CameraId id);
        bool hasCamera(CameraId id) const;

        void setActiveCamera(CameraId id);
        CameraId getActiveCameraId() const { return activeCameraId; }

        void initCameraHiZ(CameraId id, vk::Image depthImage, vk::ImageView depthView, vk::Format depthFormat);
        void recreateCameraHiZ(CameraId id, vk::Image depthImage, vk::ImageView depthView, vk::Format depthFormat);
        void initCameraOcclusionCulling(CameraId id);

        void updateCamera(CameraId id, const glm::mat4& viewProj, float nearPlane);
        void updateCameraFrustum(CameraId id, const math::Frustum& frustum);

        void updateOcclusionObjects(CameraId id, const std::vector<GPUObjectData>& objects);

        std::vector<uint32_t> getVisibilityResults(CameraId id);

        void generateHiZ(CameraId id, vk::CommandBuffer cmd);

        void runOcclusionCulling(CameraId id, vk::CommandBuffer cmd);

        void cleanup();

        bool isHiZInitialized(CameraId id);
        bool isOcclusionInitialized(CameraId id);

        const std::unordered_map<CameraId, std::unique_ptr<CameraRenderData>>& getAllCameras() const { return cameras; }
    };
}
