#pragma once
#include "../../services/providers/render/IOffScreenProvider.hpp"
#include "../../services/events/EventTypes.hpp"

namespace controllers
{
    class OffScreen;
}

namespace services
{
    class IVFXRuntimeProvider;
    class ITerrainRenderProvider;
    class IWaterRenderProvider;
    class IGrassRenderProvider;
    class IVegetationRenderProvider;
}

namespace core
{
    class OffScreenAdapter : public services::IOffScreenProvider
    {
    private:
        controllers::OffScreen* offScreen;
        events::SubscriptionToken assetReleaseToken;

    public:
        explicit OffScreenAdapter(controllers::OffScreen* offScreen);
        ~OffScreenAdapter() override;

        void setVFXRuntimeProvider(services::IVFXRuntimeProvider* provider);
        void setTerrainRenderProvider(services::ITerrainRenderProvider* provider);
        void setWaterRenderProvider(services::IWaterRenderProvider* provider);
        void setGrassRenderProvider(services::IGrassRenderProvider* provider);
        void setVegetationRenderProvider(services::IVegetationRenderProvider* provider);

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

        void removeCamera(services::CameraId id) override;
        void prepareFrameCameraFrustums() override;
        void prepareFrameAudioSpheres() override;
        void prepareFrameLightGizmos() override;

        void prepareFrameBillboards() override;
        void prepareFrameText() override;
        void prepareSceneData() override;
        void setShowBillboardIcons(bool show) override;
        bool getShowBillboardIcons() const override;
        bool loadBillboardAtlas(const std::string& atlasPath) override;

        services::CullingDebugStats getCullingStats() const override;

        void applyShadowSettings(const types::RenderSettings& settings) override;
        services::ShadowStats getShadowStats() const override;

        void setPlayMode(bool playMode) override;

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

        void setShowNavmeshDebug(bool show) override;
        bool getShowNavmeshDebug() const override;
        void updateNavmeshDebugMesh(const std::vector<glm::vec3>& vertices, const std::vector<uint32_t>& indices) override;
        void clearNavmeshDebugMesh() override;

        void prepareFrameUICanvasOutlines() override;
        void prepareFrameUIImages() override;

        void setFrustumCullingEnabled(bool enabled) override;
        void setOcclusionCullingEnabled(bool enabled) override;
        void setLODSelectionEnabled(bool enabled) override;
        void setMeshletFrustumCullingEnabled(bool enabled) override;
        void setMeshletBackfaceCullingEnabled(bool enabled) override;
        void setDistanceCullingEnabled(bool enabled) override;
        void setCategoryDistance(uint32_t category, float distance) override;
        void setShadowDistanceMultiplier(float multiplier) override;
        void setGlobalLodBias(float bias) override;
        void setTerrainFrustumCullingEnabled(bool enabled) override;
        void setTerrainMeshletCullingEnabled(bool enabled) override;

        void setWBOITEnabled(bool enabled) override;

        void setTerrainRenderingEnabled(bool enabled) override;
        void setTerrainLODBias(float bias) override;
        void setTerrainErrorThreshold(float threshold) override;
        void setTerrainTextureScale(float scale) override;
        void setTerrainShadowLOD(uint32_t lod) override;

        void setUIViewportOffset(const glm::vec2& offset, const glm::vec2& panelSize) override;

        void applyAtmosphereSettings(const render::atmosphere::AtmosphereSettings& settings) override;
        render::atmosphere::AtmosphereSettings getAtmosphereSettings() const override;
    };
}
