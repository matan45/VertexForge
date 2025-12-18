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

    void OffScreenAdapter::meshUpdateCamera(const glm::mat4& view, const glm::mat4& projection,
                                             const glm::vec3& cameraPos, float time) {
        if (offScreen) {
            offScreen->meshUpdateCamera(view, projection, cameraPos, time);
        }
    }

    bool OffScreenAdapter::isMeshLoaded(const std::string& meshPath) const {
        return offScreen && offScreen->isMeshLoaded(meshPath);
    }

    std::vector<std::string> OffScreenAdapter::getLoadedMeshes() const {
        return offScreen ? offScreen->getLoadedMeshes() : std::vector<std::string>{};
    }

    void OffScreenAdapter::prepareFrameMeshes() {
        if (offScreen) {
            offScreen->prepareFrameMeshes();
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

}
