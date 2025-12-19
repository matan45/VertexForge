#pragma once
#include "HiZBuffer.hpp"
#include "OcclusionCullingManager.hpp"
#include "math/Frustum.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <cstdint>
#include <unordered_map>

namespace core
{
    class Device;
    class SwapChain;
}

namespace render::occlusion
{
    // Unique identifier for cameras
    using CameraId = uint32_t;

    // Reserved camera IDs
    constexpr CameraId MAIN_CAMERA_ID = 0;
    constexpr CameraId INVALID_CAMERA_ID = UINT32_MAX;

    // Per-camera rendering data for occlusion culling
    struct CameraRenderData
    {
        CameraId cameraId = INVALID_CAMERA_ID;

        // Occlusion culling components (owned per camera)
        std::unique_ptr<HiZBuffer> hiZBuffer;
        std::unique_ptr<OcclusionCullingManager> occlusionManager;

        // Camera state
        math::Frustum frustum;
        glm::mat4 viewProj{1.0f};
        float nearPlane = 0.1f;

        // Configuration
        bool useOcclusionCulling = true;
        bool hiZInitialized = false;
        bool occlusionInitialized = false;

        CameraRenderData() = default;
        CameraRenderData(CameraId id) : cameraId(id) {}

        // Non-copyable, movable
        CameraRenderData(const CameraRenderData&) = delete;
        CameraRenderData& operator=(const CameraRenderData&) = delete;
        CameraRenderData(CameraRenderData&&) = default;
        CameraRenderData& operator=(CameraRenderData&&) = default;

        bool isValid() const { return cameraId != INVALID_CAMERA_ID; }
        bool isMainCamera() const { return cameraId == MAIN_CAMERA_ID; }
    };

    // Manager for multiple camera render data instances
    class CameraOcclusionManager
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        std::unordered_map<CameraId, std::unique_ptr<CameraRenderData>> cameras;
        CameraId activeCameraId = MAIN_CAMERA_ID;

    public:
        CameraOcclusionManager(core::Device& device, core::SwapChain& swapChain);
        ~CameraOcclusionManager();

        // Camera management
        CameraRenderData* createCamera(CameraId id, bool enableOcclusion = true);
        CameraRenderData* getCamera(CameraId id);
        const CameraRenderData* getCamera(CameraId id) const;
        CameraRenderData* getMainCamera();
        CameraRenderData* getActiveCamera();
        void removeCamera(CameraId id);
        bool hasCamera(CameraId id) const;

        // Active camera (the one currently being rendered)
        void setActiveCamera(CameraId id);
        CameraId getActiveCameraId() const { return activeCameraId; }

        // Initialize Hi-Z for a camera (requires depth buffer info)
        void initCameraHiZ(CameraId id, vk::Image depthImage, vk::ImageView depthView, vk::Format depthFormat);

        // Initialize occlusion culling for a camera (requires Hi-Z to be ready)
        void initCameraOcclusionCulling(CameraId id);

        // Update camera matrices
        void updateCamera(CameraId id, const glm::mat4& viewProj, float nearPlane);
        void updateCameraFrustum(CameraId id, const math::Frustum& frustum);

        // Update occlusion objects for a camera
        void updateOcclusionObjects(CameraId id, const std::vector<GPUObjectData>& objects);

        // Get visibility results for a camera
        std::vector<uint32_t> getVisibilityResults(CameraId id);

        // Generate Hi-Z for a camera (called during render)
        void generateHiZ(CameraId id, vk::CommandBuffer cmd);

        // Run occlusion culling for a camera (called during render)
        void runOcclusionCulling(CameraId id, vk::CommandBuffer cmd);

        // Cleanup
        void cleanup();

        // Accessors for compatibility
        bool isHiZInitialized(CameraId id) const;
        bool isOcclusionInitialized(CameraId id) const;

        // Get all cameras for iteration (debug/stats)
        const std::unordered_map<CameraId, std::unique_ptr<CameraRenderData>>& getAllCameras() const { return cameras; }
    };
}
