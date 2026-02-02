#pragma once
#include "../core/OffScreen.hpp"
#include "occlusion/CameraOcclusionManager.hpp"
#include "material/MaterialManager.hpp"
#include "math/Frustum.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <unordered_map>

namespace services
{
    class IVFXRuntimeProvider;
    class ITerrainRenderProvider;
}

namespace core
{
    class Device;
    class SwapChain;
    class DeferredDeletionQueue;
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
        struct PhysicsColliderRenderData;
        struct LightGizmoRenderData;
        struct ClusterDebugRenderData;
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

        bool meshPipelineInitialized = false;
        std::vector<mesh::MeshRenderData> currentMeshDrawList;
        std::vector<mesh::MeshRenderData> customShaderMeshDrawList;
        std::vector<mesh::MeshRenderData> combinedMeshDrawList;
        const math::Frustum* currentFrustum = nullptr;

        bool billboardPipelineInitialized = false;
        std::vector<billboard::BillboardRenderData> currentBillboardDrawList;

        bool debugRendererInitialized = false;
        glm::mat4 currentView{1.0f};
        glm::mat4 currentProjection{1.0f};

        bool gpuDrivenRendererInitialized = false;
        glm::vec3 currentCameraPosition{0.0f};
        float currentNearPlane = 0.1f;
        float currentFarPlane = 1000.0f;
        float currentTime = 0.0f;

        services::IVFXRuntimeProvider* vfxRuntimeProvider = nullptr;
        services::ITerrainRenderProvider* terrainRenderProvider = nullptr;

        mutable std::unordered_map<std::string, bool> customShaderRequirementCache;
        material::CallbackId materialChangeCallbackId{};
        mutable bool lightOcclusionInitialized = false;

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
        void reinitMeshPipelineWithDefaults();
        void reinitMeshPipelineWithIBL();

        void setMeshDrawList(std::vector<mesh::MeshRenderData>&& meshes);
        void setCurrentFrustum(const math::Frustum* frustum) { currentFrustum = frustum; }

        void initBillboardPipeline();
        billboard::BillboardPipeline* getBillboardPipeline() const { return billboardPipeline.get(); }
        bool isBillboardPipelineInitialized() const { return billboardPipelineInitialized; }
        void setBillboardDrawList(std::vector<billboard::BillboardRenderData>&& billboards);

        void initDebugRenderer();
        void setCameraFrustumDrawList(std::vector<mesh::CameraFrustumRenderData>&& frustums);
        void setAudioSphereDrawList(std::vector<mesh::AudioSphereRenderData>&& spheres);
        void setPhysicsColliderDrawList(std::vector<mesh::PhysicsColliderRenderData>&& colliders);
        void setLightGizmoDrawList(std::vector<mesh::LightGizmoRenderData>&& gizmos);
        void setShowPhysicsDebug(bool show);
        bool getShowPhysicsDebug() const;
        void setShowClusterDebug(bool show);
        bool getShowClusterDebug() const;
        void setClusterDebugData(mesh::ClusterDebugRenderData&& data);
        void setDebugCameraMatrices(const glm::mat4& view, const glm::mat4& projection);
        bool isDebugRendererInitialized() const { return debugRendererInitialized; }
        DebugRenderer* getDebugRenderer() const { return debugRenderer.get(); }

        occlusion::CameraOcclusionManager* getCameraOcclusionManager() const { return cameraOcclusionManager.get(); }

        gpudriven::GPUDrivenRenderer* getGPUDrivenRenderer() const { return gpuDrivenRenderer.get(); }
        bool isGPUDrivenRendererInitialized() const { return gpuDrivenRendererInitialized; }

        void setDeletionQueue(core::DeferredDeletionQueue* queue);
        void setGPUDrivenCameraData(const glm::vec3& cameraPos, float nearPlane, float farPlane, float time = 0.0f);

        void setVisibleLightsFromBVH(const std::vector<uint32_t>& visibleLights);
        void clearVisibleLights();
        void readBackLightOcclusionResults();

        void setVFXRuntimeProvider(services::IVFXRuntimeProvider* provider);
        services::IVFXRuntimeProvider* getVFXRuntimeProvider() const { return vfxRuntimeProvider; }

        void setTerrainRenderProvider(services::ITerrainRenderProvider* provider);
        services::ITerrainRenderProvider* getTerrainRenderProvider() const { return terrainRenderProvider; }
        void clearTerrainData();

        void setViewMode(uint32_t mode);
        uint32_t getViewMode() const;

        void setFrustumCullingEnabled(bool enabled);
        void setOcclusionCullingEnabled(bool enabled);
        void setLODSelectionEnabled(bool enabled);
        void setMeshletFrustumCullingEnabled(bool enabled);
        void setMeshletBackfaceCullingEnabled(bool enabled);
        void setTerrainFrustumCullingEnabled(bool enabled);
        void setTerrainMeshletCullingEnabled(bool enabled);

        void setTerrainRenderingEnabled(bool enabled);
        void setTerrainLODBias(float bias);
        void setTerrainErrorThreshold(float threshold);
        void setTerrainTextureScale(float scale);
        void setTerrainShadowLOD(uint32_t lod);

        occlusion::CameraRenderData* createCamera(occlusion::CameraId id, bool enableOcclusion = true);
        void removeCamera(occlusion::CameraId id);
        void setActiveCamera(occlusion::CameraId id);
        occlusion::CameraId getActiveCameraId() const;

        void initHiZ(occlusion::CameraId cameraId, vk::Image depthImage, vk::ImageView depthView,
                     vk::Format depthFormat);

        void updateOcclusionObjects(occlusion::CameraId cameraId, const std::vector<occlusion::GPUObjectData>& objects);
        void updateOcclusionCamera(occlusion::CameraId cameraId, const glm::mat4& viewProj, float nearPlane);

        void cleanUp() const;

        void draw(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
        
    private:
        void initGPUDrivenRenderer();
        void updateGPUDrivenHiZ() const;
        bool materialRequiresCustomShader(const std::string& materialPath) const;
        static bool computeMaterialRequiresCustomShader(const std::string& materialPath);
    };
}
