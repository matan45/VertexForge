#pragma once
#include "../core/OffScreen.hpp"
#include "occlusion/CameraRenderData.hpp"
#include "math/Frustum.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace core
{
    class Device;
    class SwapChain;
}

namespace render::gpudriven
{
    class GPUDrivenRenderer;
}

namespace render
{
    class ClearColor;
    class IBL;
    class DebugRenderer;

    namespace occlusion
    {
        struct GPUObjectData;
    }

    namespace mesh
    {
        class StaticMeshPipeline;
        struct MeshRenderData;
        struct CameraFrustumRenderData;
        struct AudioSphereRenderData;
    }

    namespace billboard
    {
        class BillboardPipeline;
        struct BillboardRenderData;
    }

    class RenderPassHandler
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;

        std::unique_ptr<ClearColor> clearColor;
        std::unique_ptr<IBL> iblRenderer;
        std::unique_ptr<mesh::StaticMeshPipeline> meshPipeline;
        std::unique_ptr<billboard::BillboardPipeline> billboardPipeline;
        std::unique_ptr<DebugRenderer> debugRenderer;
        std::unique_ptr<occlusion::CameraOcclusionManager> cameraOcclusionManager;
        std::unique_ptr<gpudriven::GPUDrivenRenderer> gpuDrivenRenderer;

        core::OffscreenResources& offscreenResources;

        // Mesh rendering state
        bool meshPipelineInitialized = false;
        mutable std::vector<mesh::MeshRenderData> currentMeshDrawList;
        const math::Frustum* currentFrustum = nullptr;

        // Billboard rendering state
        bool billboardPipelineInitialized = false;
        mutable std::vector<billboard::BillboardRenderData> currentBillboardDrawList;

        // Debug rendering state
        bool debugRendererInitialized = false;
        mutable glm::mat4 currentView{1.0f};
        mutable glm::mat4 currentProjection{1.0f};

        // GPU-driven rendering state
        bool gpuDrivenRendererInitialized = false;
        mutable glm::vec3 currentCameraPosition{0.0f};
        mutable float currentNearPlane = 0.1f;
        mutable float currentFarPlane = 1000.0f;
        mutable float currentTime = 0.0f;

        // Private helper to initialize GPU-driven renderer (called from initMeshPipeline)
        void initGPUDrivenRenderer();

    public:
        explicit RenderPassHandler(core::Device& device, core::SwapChain& swapChain,
                                   core::OffscreenResources& offscreenResources);
        ~RenderPassHandler();

        void init();

        void recreate();

        IBL* getIBL() const { return iblRenderer.get(); }
        
        mesh::StaticMeshPipeline* getMeshPipeline() const { return meshPipeline.get(); }
        bool isMeshPipelineInitialized() const { return meshPipelineInitialized; }
        
        void initMeshPipeline(bool enableGPUDriven = true);

        // (called when IBL is removed)
        void reinitMeshPipelineWithDefaults();

        //(called when IBL is set/changed)
        void reinitMeshPipelineWithIBL();
        
        void setMeshDrawList(std::vector<mesh::MeshRenderData>&& meshes);
        void setCurrentFrustum(const math::Frustum* frustum) { currentFrustum = frustum; }

        // Billboard pipeline methods
        void initBillboardPipeline();
        billboard::BillboardPipeline* getBillboardPipeline() const { return billboardPipeline.get(); }
        bool isBillboardPipelineInitialized() const { return billboardPipelineInitialized; }
        void setBillboardDrawList(std::vector<billboard::BillboardRenderData>&& billboards);

        // Debug renderer methods
        void initDebugRenderer();
        void setCameraFrustumDrawList(std::vector<mesh::CameraFrustumRenderData>&& frustums);
        void setAudioSphereDrawList(std::vector<mesh::AudioSphereRenderData>&& spheres);
        void setDebugCameraMatrices(const glm::mat4& view, const glm::mat4& projection);
        bool isDebugRendererInitialized() const { return debugRendererInitialized; }
        DebugRenderer* getDebugRenderer() const { return debugRenderer.get(); }

        // Camera occlusion manager access
        occlusion::CameraOcclusionManager* getCameraOcclusionManager() const { return cameraOcclusionManager.get(); }

        // GPU-driven rendering methods
        gpudriven::GPUDrivenRenderer* getGPUDrivenRenderer() const { return gpuDrivenRenderer.get(); }
        bool isGPUDrivenRendererInitialized() const { return gpuDrivenRendererInitialized; }
        void setGPUDrivenCameraData(const glm::vec3& cameraPos, float nearPlane, float farPlane, float time = 0.0f);

        // Camera management (delegates to CameraOcclusionManager)
        occlusion::CameraRenderData* createCamera(occlusion::CameraId id, bool enableOcclusion = true);
        occlusion::CameraRenderData* getCamera(occlusion::CameraId id);
        void removeCamera(occlusion::CameraId id);
        void setActiveCamera(occlusion::CameraId id);
        occlusion::CameraId getActiveCameraId() const;

        // Hi-Z occlusion culling methods
        void initHiZ(occlusion::CameraId cameraId, vk::Image depthImage, vk::ImageView depthView, vk::Format depthFormat);
        bool isHiZInitialized(occlusion::CameraId cameraId) const;

        // GPU occlusion culling methods
        void initOcclusionCulling(occlusion::CameraId cameraId);
        void updateOcclusionObjects(occlusion::CameraId cameraId, const std::vector<occlusion::GPUObjectData>& objects);
        void updateOcclusionCamera(occlusion::CameraId cameraId, const glm::mat4& viewProj, float nearPlane);
        std::vector<uint32_t> getOcclusionVisibility(occlusion::CameraId cameraId);
        bool isOcclusionCullingInitialized(occlusion::CameraId cameraId) const;

        void cleanUp() const;

        void draw(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
    };
}
