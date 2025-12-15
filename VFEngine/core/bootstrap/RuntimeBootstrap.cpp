#include "RuntimeBootstrap.hpp"
#include "../controllers/CoreInterface.hpp"
#include "../controllers/OffScreen.hpp"
#include "../adapters/OffScreenAdapter.hpp"
#include "scene/LevelHandler.hpp"

namespace core {

    RuntimeBootstrap::RuntimeBootstrap()
        : coreInterface(std::make_unique<::controllers::CoreInterface>())
        , offScreen(std::make_unique<::controllers::OffScreen>()) {}

    RuntimeBootstrap::~RuntimeBootstrap() = default;

    void RuntimeBootstrap::init() {
        // Initialize core systems
        coreInterface->init();

        // Create adapters that implement provider interfaces
        offScreenAdapter = std::make_unique<OffScreenAdapter>(offScreen.get());

        // Initialize offscreen rendering
        offScreen->init();
    }

    void RuntimeBootstrap::run() const {
        coreInterface->run();
    }

    void RuntimeBootstrap::cleanUp() {
        // Clean up in reverse order of initialization
        if (offScreen) {
            offScreen->cleanUp();
        }

        // Reset adapters
        offScreenAdapter.reset();

        // Clean up core
        if (coreInterface) {
            coreInterface->cleanUp();
        }
    }

    services::IOffScreenProvider* RuntimeBootstrap::getOffScreenProvider() {
        return offScreenAdapter.get();
    }

    window::Window* RuntimeBootstrap::getWindow() {
        return coreInterface ? coreInterface->getWindow() : nullptr;
    }

    std::shared_ptr<scene::SceneGraphSystem> RuntimeBootstrap::getSceneGraphSystem() {
        auto level = scene::LevelHandler::getInstance();
        return level ? level->getSceneGraphSystem() : nullptr;
    }

}
