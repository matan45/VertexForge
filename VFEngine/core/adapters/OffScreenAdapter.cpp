#include "OffScreenAdapter.hpp"
#include "../controllers/OffScreen.hpp"

namespace core {

    OffScreenAdapter::OffScreenAdapter(controllers::OffScreen* offScreen)
        : offScreen(offScreen) {}

    void OffScreenAdapter::init() {
        if (offScreen) {
            offScreen->init();
        }
    }

    void OffScreenAdapter::cleanUp() {
        if (offScreen) {
            offScreen->cleanUp();
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

    void OffScreenAdapter::rebuildBVH() {
        if (offScreen) {
            offScreen->rebuildBVH();
        }
    }

    void OffScreenAdapter::markBVHDirty() {
        if (offScreen) {
            offScreen->markBVHDirty();
        }
    }

    void OffScreenAdapter::createCamera(services::CameraId id, bool enableOcclusion) {
        if (offScreen) {
            offScreen->createCamera(id, enableOcclusion);
        }
    }

    void OffScreenAdapter::removeCamera(services::CameraId id) {
        if (offScreen) {
            offScreen->removeCamera(id);
        }
    }

    void OffScreenAdapter::setActiveCamera(services::CameraId id) {
        if (offScreen) {
            offScreen->setActiveCamera(id);
        }
    }

    services::CameraId OffScreenAdapter::getActiveCameraId() const {
        return offScreen ? offScreen->getActiveCameraId() : services::MAIN_CAMERA_ID;
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

    void OffScreenAdapter::setPlayMode(bool playMode) {
        playModeActive = playMode;
        if (offScreen) {
            offScreen->setPlayMode(playMode);
        }
    }

    bool OffScreenAdapter::isPlayMode() const {
        return playModeActive;
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

}
