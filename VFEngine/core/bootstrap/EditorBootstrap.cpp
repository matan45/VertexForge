#include "EditorBootstrap.hpp"
#include "../controllers/CoreInterface.hpp"
#include "../controllers/OffScreen.hpp"
#include "../adapters/OffScreenAdapter.hpp"
#include "../adapters/EditorTextureAdapter.hpp"
#include "../adapters/PreviewAdapter.hpp"
#include "scene/LevelHandler.hpp"

namespace core {

    EditorBootstrap::EditorBootstrap()
        : coreInterface(std::make_unique<::controllers::CoreInterface>())
        , offScreen(std::make_unique<::controllers::OffScreen>()) {}

    EditorBootstrap::~EditorBootstrap() = default;

    void EditorBootstrap::init() {
        // Initialize core systems
        coreInterface->init();

        // Create adapters that implement provider interfaces
        offScreenAdapter = std::make_unique<OffScreenAdapter>(offScreen.get());
        textureAdapter = std::make_unique<EditorTextureAdapter>();
        previewAdapter = std::make_unique<PreviewAdapter>();

        // Initialize offscreen rendering
        offScreen->init();
    }

    void EditorBootstrap::run() const {
        coreInterface->run();
    }

    void EditorBootstrap::cleanUp() {
        // Clean up in reverse order of initialization
        if (offScreen) {
            offScreen->cleanUp();
        }

        // Reset adapters
        previewAdapter.reset();
        textureAdapter.reset();
        offScreenAdapter.reset();

        // Clean up core
        if (coreInterface) {
            coreInterface->cleanUp();
        }
    }

    services::IOffScreenProvider* EditorBootstrap::getOffScreenProvider() {
        return offScreenAdapter.get();
    }

    services::IEditorTextureProvider* EditorBootstrap::getEditorTextureProvider() {
        return textureAdapter.get();
    }

    services::IPreviewProvider* EditorBootstrap::getPreviewProvider() {
        return previewAdapter.get();
    }

    window::Window* EditorBootstrap::getWindow() {
        return coreInterface ? coreInterface->getWindow() : nullptr;
    }

    std::shared_ptr<scene::SceneGraphSystem> EditorBootstrap::getSceneGraphSystem() {
        auto level = scene::LevelHandler::getInstance();
        return level ? level->getSceneGraphSystem() : nullptr;
    }

}
