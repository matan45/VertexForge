#include "RuntimeBootstrap.hpp"
#include "../controllers/CoreInterface.hpp"
#include "../controllers/OffScreen.hpp"
#include "../adapters/OffScreenAdapter.hpp"
#include "scene/LevelHandler.hpp"

namespace core
{
    RuntimeBootstrap::RuntimeBootstrap()
        : coreInterface(std::make_unique<::controllers::CoreInterface>())
          , offScreen(std::make_unique<::controllers::OffScreen>())
    {
    }

    RuntimeBootstrap::~RuntimeBootstrap() = default;

    void RuntimeBootstrap::init()
    {
        coreInterface->init();

        offScreenAdapter = std::make_unique<OffScreenAdapter>(offScreen.get());

        offScreen->init();

        // Set up resize callback to recreate offscreen resources (Hi-Z, etc.)
        coreInterface->setResizeCallback([this]() {
            offScreen->recreate();
        });
    }

    void RuntimeBootstrap::run() const
    {
        coreInterface->run();
    }

    void RuntimeBootstrap::cleanUp()
    {
        if (offScreen)
        {
            offScreen->cleanUp();
        }

        offScreenAdapter.reset();

        if (coreInterface)
        {
            coreInterface->cleanUp();
        }
    }

    services::IOffScreenProvider* RuntimeBootstrap::getOffScreenProvider()
    {
        return offScreenAdapter.get();
    }

    window::Window* RuntimeBootstrap::getWindow()
    {
        return coreInterface ? coreInterface->getWindow() : nullptr;
    }

    std::shared_ptr<scene::SceneGraphSystem> RuntimeBootstrap::getSceneGraphSystem()
    {
        auto level = scene::LevelHandler::getInstance();
        return level ? level->getSceneGraphSystem() : nullptr;
    }

    void RuntimeBootstrap::setFrameCallback(std::function<void()> callback)
    {
        if (coreInterface)
        {
            coreInterface->setFrameCallback(std::move(callback));
        }
    }

    void RuntimeBootstrap::triggerResize()
    {
        if (coreInterface)
        {
            coreInterface->triggerResize();
        }
    }
}
