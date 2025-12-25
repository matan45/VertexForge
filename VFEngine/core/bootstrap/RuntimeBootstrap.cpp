#include "RuntimeBootstrap.hpp"
#include "../controllers/CoreInterface.hpp"
#include "../controllers/OffScreen.hpp"
#include "../adapters/OffScreenAdapter.hpp"
#include "../adapters/AudioAdapter.hpp"
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
        audioAdapter = std::make_unique<AudioAdapter>();

        offScreen->init();
        audioAdapter->init();

        // Set up resize callback to recreate offscreen resources (Hi-Z, etc.)
        coreInterface->setResizeCallback([this]()
        {
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

        if (audioAdapter)
        {
            audioAdapter->cleanUp();
        }

        offScreenAdapter.reset();
        audioAdapter.reset();

        if (coreInterface)
        {
            coreInterface->cleanUp();
        }
    }

    services::IOffScreenProvider* RuntimeBootstrap::getOffScreenProvider()
    {
        return offScreenAdapter.get();
    }

    services::IAudioProvider* RuntimeBootstrap::getAudioProvider()
    {
        return audioAdapter.get();
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
            // Wrap the callback to also update audio each frame
            coreInterface->setFrameCallback([this, cb = std::move(callback)]()
            {
                if (audioAdapter)
                {
                    audioAdapter->update();
                }
                if (cb)
                {
                    cb();
                }
            });
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
