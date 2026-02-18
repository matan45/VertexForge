#pragma once
#include "../core/OffScreen.hpp"
#include "occlusion/CameraOcclusionManager.hpp"
#include "material/MaterialManager.hpp"
#include "math/Frustum.hpp"
#include "terrain/TerrainHitResult.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace services
{
    class IVFXRuntimeProvider;
    class ITerrainRenderProvider;
    class IWaterRenderProvider;
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
    class TerrainRaycastPipeline;
}

namespace render::postprocess
{
    class PostProcessPipeline;
}

namespace render::volumetric
{
    class VolumetricFogComposite;
    class VolumetricPipeline;
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
        struct UICanvasOutlineRenderData;
        struct UICanvasImageRenderData;
    }

    namespace billboard
    {
        class BillboardPipeline;
        struct BillboardRenderData;
    }

    namespace text
    {
        class TextPipeline;
        struct TextRenderData;
    }

    namespace ui
    {
        class UIRenderPipeline;
        struct UIImageRenderData;
        class UITextPipeline;
        struct UITextRenderData;
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
        std::unique_ptr<text::TextPipeline> textPipeline;
        std::unique_ptr<ui::UIRenderPipeline> uiPipeline;
        std::unique_ptr<ui::UITextPipeline> uiTextPipeline;
        std::unique_ptr<DebugRenderer> debugRenderer;
        std::unique_ptr<occlusion::CameraOcclusionManager> cameraOcclusionManager;
        std::unique_ptr<gpudriven::GPUDrivenRenderer> gpuDrivenRenderer;
        std::unique_ptr<gpudriven::TerrainRaycastPipeline> terrainRaycastPipeline;
        std::unique_ptr<postprocess::PostProcessPipeline> postProcessPipeline;
        std::unique_ptr<volumetric::VolumetricFogComposite> volumetricFogComposite;

        core::OffscreenResources& offscreenResources;

        bool meshPipelineInitialized = false;
        std::vector<mesh::MeshRenderData> currentMeshDrawList;
        std::vector<mesh::MeshRenderData> customShaderMeshDrawList;
        std::vector<mesh::MeshRenderData> combinedMeshDrawList;
        const math::Frustum* currentFrustum = nullptr;

        bool billboardPipelineInitialized = false;
        std::vector<billboard::BillboardRenderData> currentBillboardDrawList;

        bool textPipelineInitialized = false;
        std::vector<text::TextRenderData> currentTextDrawList;

        bool uiPipelineInitialized = false;
        std::vector<ui::UIImageRenderData> currentUIImageDrawList;

        bool uiTextPipelineInitialized = false;
        std::vector<ui::UITextRenderData> currentUITextDrawList;

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
        services::IWaterRenderProvider* waterRenderProvider = nullptr;

        mutable std::unordered_map<std::string, bool> customShaderRequirementCache;
        material::CallbackId materialChangeCallbackId{};
        mutable bool lightOcclusionInitialized = false;

        float brushOverlayRadius_ = 0.0f;
        float brushOverlayFalloff_ = 0.0f;
        float brushOverlayShape_ = 0.0f;

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

        void initTextPipeline();
        text::TextPipeline* getTextPipeline() const { return textPipeline.get(); }
        bool isTextPipelineInitialized() const { return textPipelineInitialized; }
        void setTextDrawList(std::vector<text::TextRenderData>&& textEntities);
        void appendTextDrawList(std::vector<text::TextRenderData>&& textEntities);

        void initUIRenderPipeline();
        ui::UIRenderPipeline* getUIRenderPipeline() const { return uiPipeline.get(); }
        bool isUIRenderPipelineInitialized() const { return uiPipelineInitialized; }
        void setUIImageDrawList(std::vector<ui::UIImageRenderData>&& images);

        void initUITextPipeline();
        ui::UITextPipeline* getUITextPipeline() const { return uiTextPipeline.get(); }
        bool isUITextPipelineInitialized() const { return uiTextPipelineInitialized; }
        void setUITextDrawList(std::vector<ui::UITextRenderData>&& labels);

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
        void setUICanvasOutlineDrawList(std::vector<mesh::UICanvasOutlineRenderData>&& outlines);
        void setUICanvasImageDrawList(std::vector<mesh::UICanvasImageRenderData>&& images);
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
        void readBackTerrainRaycastResults();

        void setRaycastCursorUV(const glm::vec2& uv);
        void clearRaycastCursor();
        terrain::TerrainHitResult getTerrainHitResult() const;

        void setBrushOverlayParams(float radius, float falloff, float shape);
        void updateBrushOverlayFromHitResult();

        void setVFXRuntimeProvider(services::IVFXRuntimeProvider* provider);

        void setTerrainRenderProvider(services::ITerrainRenderProvider* provider);
        void clearTerrainData();

        void setWaterRenderProvider(services::IWaterRenderProvider* provider);
        void clearWaterData();

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

        postprocess::PostProcessPipeline* getPostProcessPipeline() const { return postProcessPipeline.get(); }

        void initVolumetricFogComposite(volumetric::VolumetricPipeline* volPipeline);
        void resetVolumetricFogComposite();
        volumetric::VolumetricFogComposite* getVolumetricFogComposite() const { return volumetricFogComposite.get(); }

        void cleanUp() const;

        void draw(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
        
    private:
        void initGPUDrivenRenderer();
        void updateGPUDrivenHiZ() const;
        bool materialRequiresCustomShader(const std::string& materialPath) const;
        static bool computeMaterialRequiresCustomShader(const std::string& materialPath);

        std::unordered_set<std::string> collectCustomShaderMaterials(
            const std::vector<mesh::MeshRenderData>& meshes) const;
        void updateDebugBoundingBoxState();
        void rebuildCombinedMeshDrawList();

        void recreateOverlayPipelines();
        void cleanUpPipelines() const;

        void updateGPUDrivenSceneData() const;
        void drawSceneMeshes(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
        void drawGPUDrivenMeshPass(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex,
                                   DebugRenderer* debugRendererPtr, bool hasCustomShaderMeshes, bool hasVFX) const;
        void drawOverlays(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
        void executeOcclusionPasses(const vk::CommandBuffer& commandBuffer) const;
        void dispatchTerrainRaycast(const vk::CommandBuffer& commandBuffer) const;
        void executePostProcess(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
        void updateSunScreenPosition() const;
        void drawUIOverlays(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex) const;
    };
}
