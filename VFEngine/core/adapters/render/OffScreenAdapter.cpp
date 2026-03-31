#include "OffScreenAdapter.hpp"
#include "../../controllers/OffScreen.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include "../../services/events/lifecycle/AssetLifecycleEvents.hpp"
#include "resource/AssetTypes.hpp"
#include "print/Log.hpp"

namespace core {

    OffScreenAdapter::OffScreenAdapter(controllers::OffScreen* offScreen)
        : offScreen(offScreen)
    {
        auto& dispatcher = events::EventDispatcher::instance();
        assetReleaseToken = dispatcher.subscribe<events::lifecycle::AssetReleaseReadyNotification>(
            [this](const events::lifecycle::AssetReleaseReadyNotification& notification)
            {
                if (!this->offScreen) return;

                switch (notification.type)
                {
                case resource::AssetType::Mesh:
                    this->offScreen->meshRelease(notification.path);
                    vfLogInfo("OffScreenAdapter: Released mesh GPU resources for '{}'", notification.path);
                    break;
                case resource::AssetType::Texture:
                case resource::AssetType::HDR:
                    this->offScreen->textureRelease(notification.path);
                    vfLogInfo("OffScreenAdapter: Released texture GPU resources for '{}'", notification.path);
                    break;
                case resource::AssetType::Material:
                    this->offScreen->materialRelease(notification.path);
                    vfLogInfo("OffScreenAdapter: Released material GPU resources for '{}'", notification.path);
                    break;
                default:
                    break;
                }
            });
    }

    OffScreenAdapter::~OffScreenAdapter()
    {
        if (assetReleaseToken.isValid())
        {
            events::EventDispatcher::instance().unsubscribe(assetReleaseToken);
        }
    }

    void* OffScreenAdapter::render() {
        return offScreen ? offScreen->render() : nullptr;
    }

    void OffScreenAdapter::iblSet(std::string_view iblPath) {
        if (offScreen) {
            offScreen->iblSet(iblPath);
        }
    }

    void OffScreenAdapter::iblSetCameraMatrices(const glm::mat4& view, const glm::mat4& projection) {
        if (offScreen) {
            offScreen->iblSetCameraMatrices(view, projection);
        }
    }

    void OffScreenAdapter::iblRemove() {
        if (offScreen) {
            offScreen->iblRemove();
        }
    }

    std::string OffScreenAdapter::meshLoad(std::string_view meshPath) {
        return offScreen ? offScreen->meshLoad(meshPath) : "";
    }

    void OffScreenAdapter::meshUnload(const std::string& meshId) {
        if (offScreen) {
            offScreen->meshUnload(meshId);
        }
    }

    void OffScreenAdapter::meshUpdateCamera(services::CameraId cameraId, const glm::mat4& view,
                                             const glm::mat4& projection, const glm::vec3& cameraPos, float time) {
        if (offScreen) {
            offScreen->meshUpdateCamera(cameraId, view, projection, cameraPos, time);
        }
    }

    bool OffScreenAdapter::isMeshLoaded(const std::string& meshPath) const {
        return offScreen && offScreen->isMeshLoaded(meshPath);
    }

    std::vector<std::string> OffScreenAdapter::getLoadedMeshes() const {
        return offScreen ? offScreen->getLoadedMeshes() : std::vector<std::string>{};
    }

    std::optional<services::MeshBounds> OffScreenAdapter::getMeshBoundingBox(const std::string& meshPath) const {
        return offScreen ? offScreen->getMeshBoundingBox(meshPath) : std::nullopt;
    }

    void OffScreenAdapter::prepareCameras() {
        if (offScreen) {
            offScreen->prepareCameras();
        }
    }

    void OffScreenAdapter::prepareFrameMeshes() {
        if (offScreen) {
            offScreen->prepareFrameMeshes();
        }
    }

    void OffScreenAdapter::prepareFrameCameraFrustums() {
        if (offScreen) {
            offScreen->prepareFrameCameraFrustums();
        }
    }

    void OffScreenAdapter::prepareFrameAudioSpheres() {
        if (offScreen) {
            offScreen->prepareFrameAudioSpheres();
        }
    }

    void OffScreenAdapter::prepareFrameLightGizmos() {
        if (offScreen) {
            offScreen->prepareFrameLightGizmos();
        }
    }

    void OffScreenAdapter::prepareFrameBillboards() {
        if (offScreen) {
            offScreen->prepareFrameBillboards();
        }
    }

    void OffScreenAdapter::prepareFrameText() {
        if (offScreen) {
            offScreen->prepareFrameText();
        }
    }

    void OffScreenAdapter::prepareSceneData() {
        if (offScreen) {
            offScreen->prepareSceneData();
        }
    }

    void OffScreenAdapter::setShowBillboardIcons(bool show) {
        if (offScreen) {
            offScreen->setShowBillboardIcons(show);
        }
    }

    bool OffScreenAdapter::getShowBillboardIcons() const {
        return offScreen && offScreen->getShowBillboardIcons();
    }

    bool OffScreenAdapter::loadBillboardAtlas(const std::string& atlasPath) {
        return offScreen && offScreen->loadBillboardAtlas(atlasPath);
    }

    void OffScreenAdapter::removeCamera(services::CameraId id) {
        if (offScreen) {
            offScreen->removeCamera(id);
        }
    }

    services::CullingDebugStats OffScreenAdapter::getCullingStats() const {
        return offScreen ? offScreen->getCullingStats() : services::CullingDebugStats{};
    }

    void OffScreenAdapter::applyShadowSettings(const types::RenderSettings& settings) {
        if (offScreen) {
            offScreen->applyShadowSettings(settings);
        }
    }

    services::ShadowStats OffScreenAdapter::getShadowStats() const {
        return offScreen ? offScreen->getShadowStats() : services::ShadowStats{};
    }

    types::RTShadowStats OffScreenAdapter::getRTShadowStats() const {
        return offScreen ? offScreen->getRTShadowStats() : types::RTShadowStats{};
    }

    services::GPUPipelineStatus OffScreenAdapter::getGPUPipelineStatus() const {
        return offScreen ? offScreen->getGPUPipelineStatus() : services::GPUPipelineStatus{};
    }

    void OffScreenAdapter::setPlayMode(bool playMode) {
        if (offScreen) {
            offScreen->setPlayMode(playMode);
        }
    }

    void OffScreenAdapter::setShowDebugRendering(bool show) {
        if (offScreen) {
            offScreen->setShowDebugRendering(show);
        }
    }

    bool OffScreenAdapter::getShowDebugRendering() const {
        return offScreen ? offScreen->getShowDebugRendering() : true;
    }

    void OffScreenAdapter::setShowGrid(bool show) {
        if (offScreen) {
            offScreen->setShowGrid(show);
        }
    }

    bool OffScreenAdapter::getShowGrid() const {
        return offScreen ? offScreen->getShowGrid() : true;
    }

    void OffScreenAdapter::prepareGrid() {
        if (offScreen) {
            offScreen->prepareGrid();
        }
    }

    void OffScreenAdapter::setShowPhysicsDebug(bool show) {
        if (offScreen) {
            offScreen->setShowPhysicsDebug(show);
        }
    }

    bool OffScreenAdapter::getShowPhysicsDebug() const {
        return offScreen ? offScreen->getShowPhysicsDebug() : false;
    }

    void OffScreenAdapter::prepareFramePhysicsColliders() {
        if (offScreen) {
            offScreen->prepareFramePhysicsColliders();
        }
    }

    void OffScreenAdapter::setViewMode(uint32_t mode) {
        if (offScreen) {
            offScreen->setViewMode(mode);
        }
    }

    uint32_t OffScreenAdapter::getViewMode() const {
        return offScreen ? offScreen->getViewMode() : 0;
    }

    void OffScreenAdapter::setShowClusterDebug(bool show) {
        if (offScreen) {
            offScreen->setShowClusterDebug(show);
        }
    }

    bool OffScreenAdapter::getShowClusterDebug() const {
        return offScreen ? offScreen->getShowClusterDebug() : false;
    }

    void OffScreenAdapter::prepareFrameClusterDebug() {
        if (offScreen) {
            offScreen->prepareFrameClusterDebug();
        }
    }

    void OffScreenAdapter::setShowShadowDebug(bool show) {
        if (offScreen) {
            offScreen->setShowShadowDebug(show);
        }
    }

    bool OffScreenAdapter::getShowShadowDebug() const {
        return offScreen ? offScreen->getShowShadowDebug() : false;
    }

    void OffScreenAdapter::prepareFrameShadowDebug() {
        if (offScreen) {
            offScreen->prepareFrameShadowDebug();
        }
    }

    void OffScreenAdapter::setShowWireframe(bool show) {
        if (offScreen) {
            offScreen->setShowWireframe(show);
        }
    }

    bool OffScreenAdapter::getShowWireframe() const {
        return offScreen ? offScreen->getShowWireframe() : false;
    }

    void OffScreenAdapter::setShowNavmeshDebug(bool show) {
        if (offScreen) {
            offScreen->setShowNavmeshDebug(show);
        }
    }

    bool OffScreenAdapter::getShowNavmeshDebug() const {
        return offScreen ? offScreen->getShowNavmeshDebug() : false;
    }

    void OffScreenAdapter::updateNavmeshDebugMesh(const std::vector<glm::vec3>& vertices,
                                                   const std::vector<uint32_t>& indices) {
        if (offScreen) {
            offScreen->updateNavmeshDebugMesh(vertices, indices);
        }
    }

    void OffScreenAdapter::clearNavmeshDebugMesh() {
        if (offScreen) {
            offScreen->clearNavmeshDebugMesh();
        }
    }

    void OffScreenAdapter::prepareFrameUICanvasOutlines() {
        if (offScreen) {
            offScreen->prepareFrameUICanvasOutlines();
        }
    }

    void OffScreenAdapter::prepareFrameUIImages() {
        if (offScreen) {
            offScreen->prepareFrameUIImages();
        }
    }

    void OffScreenAdapter::setFrustumCullingEnabled(bool enabled) {
        if (offScreen) {
            offScreen->setFrustumCullingEnabled(enabled);
        }
    }

    void OffScreenAdapter::setOcclusionCullingEnabled(bool enabled) {
        if (offScreen) {
            offScreen->setOcclusionCullingEnabled(enabled);
        }
    }

    void OffScreenAdapter::setLODSelectionEnabled(bool enabled) {
        if (offScreen) {
            offScreen->setLODSelectionEnabled(enabled);
        }
    }

    void OffScreenAdapter::setLODCrossfadeEnabled(bool enabled) {
        if (offScreen) {
            offScreen->setLODCrossfadeEnabled(enabled);
        }
    }

    void OffScreenAdapter::setMeshletFrustumCullingEnabled(bool enabled) {
        if (offScreen) {
            offScreen->setMeshletFrustumCullingEnabled(enabled);
        }
    }

    void OffScreenAdapter::setMeshletBackfaceCullingEnabled(bool enabled) {
        if (offScreen) {
            offScreen->setMeshletBackfaceCullingEnabled(enabled);
        }
    }

    void OffScreenAdapter::setMeshletOcclusionCullingEnabled(bool enabled) {
        if (offScreen) {
            offScreen->setMeshletOcclusionCullingEnabled(enabled);
        }
    }

    void OffScreenAdapter::setDistanceCullingEnabled(bool enabled) {
        if (offScreen) {
            offScreen->setDistanceCullingEnabled(enabled);
        }
    }

    void OffScreenAdapter::setCategoryDistance(uint32_t category, float distance) {
        if (offScreen) {
            offScreen->setCategoryDistance(category, distance);
        }
    }

    void OffScreenAdapter::setShadowDistanceMultiplier(float multiplier) {
        if (offScreen) {
            offScreen->setShadowDistanceMultiplier(multiplier);
        }
    }

    void OffScreenAdapter::setGlobalLodBias(float bias) {
        if (offScreen) {
            offScreen->setGlobalLodBias(bias);
        }
    }

    void OffScreenAdapter::setTerrainFrustumCullingEnabled(bool enabled) {
        if (offScreen) {
            offScreen->setTerrainFrustumCullingEnabled(enabled);
        }
    }

    void OffScreenAdapter::setTerrainMeshletCullingEnabled(bool enabled) {
        if (offScreen) {
            offScreen->setTerrainMeshletCullingEnabled(enabled);
        }
    }

    void OffScreenAdapter::setWBOITEnabled(bool enabled) {
        if (offScreen) {
            offScreen->setWBOITEnabled(enabled);
        }
    }

    void OffScreenAdapter::setTerrainRenderingEnabled(bool enabled) {
        if (offScreen) {
            offScreen->setTerrainRenderingEnabled(enabled);
        }
    }

    void OffScreenAdapter::setTerrainLODBias(float bias) {
        if (offScreen) {
            offScreen->setTerrainLODBias(bias);
        }
    }

    void OffScreenAdapter::setTerrainErrorThreshold(float threshold) {
        if (offScreen) {
            offScreen->setTerrainErrorThreshold(threshold);
        }
    }

    void OffScreenAdapter::setTerrainTextureScale(float scale) {
        if (offScreen) {
            offScreen->setTerrainTextureScale(scale);
        }
    }

    void OffScreenAdapter::setTerrainSVTEnabled(bool enabled) {
        if (offScreen) {
            offScreen->setTerrainSVTEnabled(enabled);
        }
    }

    void OffScreenAdapter::setUIViewportOffset(const glm::vec2& offset, const glm::vec2& panelSize) {
        if (offScreen) {
            offScreen->setUIViewportOffset(offset, panelSize);
        }
    }

    void OffScreenAdapter::applyAtmosphereSettings(const render::atmosphere::AtmosphereSettings& settings) {
        if (offScreen) {
            offScreen->applyAtmosphereSettings(settings);
        }
    }

    render::atmosphere::AtmosphereSettings OffScreenAdapter::getAtmosphereSettings() const {
        return offScreen ? offScreen->getAtmosphereSettings() : render::atmosphere::AtmosphereSettings{};
    }

    void OffScreenAdapter::applyCloudSettings(const render::cloud::CloudSettings& settings) {
        if (offScreen) {
            offScreen->applyCloudSettings(settings);
        }
    }

    render::cloud::CloudSettings OffScreenAdapter::getCloudSettings() const {
        return offScreen ? offScreen->getCloudSettings() : render::cloud::CloudSettings{};
    }

    void OffScreenAdapter::setVFXRuntimeProvider(services::IVFXRuntimeProvider* provider) {
        if (offScreen) {
            offScreen->setVFXRuntimeProvider(provider);
        }
    }

    void OffScreenAdapter::setTerrainRenderProvider(services::ITerrainRenderProvider* provider) {
        if (offScreen) {
            offScreen->setTerrainRenderProvider(provider);
        }
    }

    void OffScreenAdapter::setOceanRenderProvider(services::IOceanRenderProvider* provider) {
        if (offScreen) {
            offScreen->setOceanRenderProvider(provider);
        }
    }

    void OffScreenAdapter::setGrassRenderProvider(services::IGrassRenderProvider* provider) {
        if (offScreen) {
            offScreen->setGrassRenderProvider(provider);
        }
    }

    void OffScreenAdapter::setVegetationRenderProvider(services::IVegetationRenderProvider* provider) {
        if (offScreen) {
            offScreen->setVegetationRenderProvider(provider);
        }
    }

}
