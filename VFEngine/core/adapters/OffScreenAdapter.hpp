#pragma once
#include "../../services/providers/IOffScreenProvider.hpp"

namespace controllers
{
    class OffScreen;
}

namespace core
{
    class OffScreenAdapter : public services::IOffScreenProvider
    {
    private:
        controllers::OffScreen* offScreen;

    public:
        explicit OffScreenAdapter(controllers::OffScreen* offScreen);
        ~OffScreenAdapter() override = default;
        
        void init() override;
        void cleanUp() override;
        void* render() override;

        void iblSet(std::string_view iblPath) override;
        void iblSetCameraMatrices(const glm::mat4& view, const glm::mat4& projection) override;
        void iblRemove() override;

        std::string meshLoad(std::string_view meshPath) override;
        void meshUnload(const std::string& meshId) override;
        void meshUpdateCamera(services::CameraId cameraId, const glm::mat4& view, const glm::mat4& projection,
                              const glm::vec3& cameraPos, float time = 0.0f) override;
        bool isMeshLoaded(const std::string& meshPath) const override;
        std::vector<std::string> getLoadedMeshes() const override;
        void prepareCameras() override;
        std::optional<services::MeshBounds> getMeshBoundingBox(const std::string& meshPath) const override;
        void prepareFrameMeshes() override;

        void rebuildBVH() override;
        void markBVHDirty() override;

        void createCamera(services::CameraId id, bool enableOcclusion = false) override;
        void removeCamera(services::CameraId id) override;
        void setActiveCamera(services::CameraId id) override;
        services::CameraId getActiveCameraId() const override;
        void prepareFrameCameraFrustums() override;
        void prepareFrameAudioSpheres() override;
        void prepareFrameLightGizmos() override;

        void prepareFrameBillboards() override;
        void setShowBillboardIcons(bool show) override;
        bool getShowBillboardIcons() const override;
        bool loadBillboardAtlas(const std::string& atlasPath) override;

        services::CullingDebugStats getCullingStats() const override;

        void applyShadowSettings(const types::RenderSettings& settings) override;
        services::ShadowStats getShadowStats() const override;

        void setPlayMode(bool playMode) override;
        bool isPlayMode() const override;

        void setShowDebugRendering(bool show) override;
        bool getShowDebugRendering() const override;

        void setShowGrid(bool show) override;
        bool getShowGrid() const override;
        void prepareGrid() override;

        void setShowPhysicsDebug(bool show) override;
        bool getShowPhysicsDebug() const override;
        void prepareFramePhysicsColliders() override;

        void setViewMode(uint32_t mode) override;
        uint32_t getViewMode() const override;

        void setShowClusterDebug(bool show) override;
        bool getShowClusterDebug() const override;
        void prepareFrameClusterDebug() override;

        void setShowShadowDebug(bool show) override;
        bool getShowShadowDebug() const override;
        void prepareFrameShadowDebug() override;

        void setFrustumCullingEnabled(bool enabled) override;
        void setOcclusionCullingEnabled(bool enabled) override;
        void setLODSelectionEnabled(bool enabled) override;
        void setMeshletFrustumCullingEnabled(bool enabled) override;
        void setMeshletBackfaceCullingEnabled(bool enabled) override;

        void setVFXRuntimeProvider(services::IVFXRuntimeProvider* provider) override;
        void setTerrainRenderProvider(services::ITerrainRenderProvider* provider) override;

    private:
        bool playModeActive = false;
    };
}
