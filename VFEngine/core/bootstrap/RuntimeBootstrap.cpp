#include "RuntimeBootstrap.hpp"
#include "../controllers/CoreInterface.hpp"
#include "../../window/window/Window.hpp"
#include "../controllers/OffScreen.hpp"
#include "../adapters/OffScreenAdapter.hpp"
#include "../adapters/AudioAdapter.hpp"
#include "../adapters/ScriptingAdapter.hpp"
#include "../adapters/PhysicsAdapter.hpp"
#include "../adapters/NavmeshAdapter.hpp"
#include "../adapters/SocketAdapter.hpp"
#include "../adapters/AnimatorAdapter.hpp"
#include "../adapters/IKAdapter.hpp"
#include "../adapters/VFXRuntimeAdapter.hpp"
#include "../adapters/PostProcessAdapter.hpp"
#include "../adapters/WaterRenderAdapter.hpp"
#include "../adapters/RenderTextureAdapter.hpp"
#include "../adapters/DebugDrawAdapter.hpp"
#include "../adapters/NativeAPIRegistry.hpp"
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
        scriptingAdapter = std::make_unique<ScriptingAdapter>();
        physicsAdapter = std::make_unique<PhysicsAdapter>();
        navmeshAdapter = std::make_unique<NavmeshAdapter>();
        socketAdapter = std::make_unique<SocketAdapter>();
        animatorAdapter = std::make_unique<AnimatorAdapter>();
        ikAdapter = std::make_unique<IKAdapter>();
        vfxRuntimeAdapter = std::make_unique<VFXRuntimeAdapter>();
        postProcessAdapter = std::make_unique<PostProcessAdapter>(offScreen.get());
        waterRenderAdapter = std::make_unique<WaterRenderAdapter>();
        renderTextureAdapter = std::make_unique<RenderTextureAdapter>(offScreen.get());
        debugDrawAdapter = std::make_unique<DebugDrawAdapter>();

        offScreen->init();
        audioAdapter->init();
        scriptingAdapter->init();
        physicsAdapter->init();
        navmeshAdapter->init();

        // Wire VFX runtime provider to offscreen renderer
        // This allows VFX to be rendered as part of the scene
        offScreenAdapter->setVFXRuntimeProvider(vfxRuntimeAdapter.get());

        // Wire water render provider to offscreen renderer
        offScreenAdapter->setWaterRenderProvider(waterRenderAdapter.get());

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

        if (scriptingAdapter)
        {
            scriptingAdapter->cleanUp();
        }

        if (physicsAdapter)
        {
            physicsAdapter->cleanUp();
        }

        if (navmeshAdapter)
        {
            navmeshAdapter->cleanUp();
        }

        renderTextureAdapter.reset();
        debugDrawAdapter.reset();
        postProcessAdapter.reset();
        waterRenderAdapter.reset();
        vfxRuntimeAdapter.reset();
        offScreenAdapter.reset();
        audioAdapter.reset();
        scriptingAdapter.reset();
        physicsAdapter.reset();
        navmeshAdapter.reset();
        socketAdapter.reset();
        animatorAdapter.reset();
        ikAdapter.reset();

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

    services::IScriptingProvider* RuntimeBootstrap::getScriptingProvider()
    {
        return scriptingAdapter.get();
    }

    services::IPhysicsProvider* RuntimeBootstrap::getPhysicsProvider()
    {
        return physicsAdapter.get();
    }

    services::INavmeshProvider* RuntimeBootstrap::getNavmeshProvider()
    {
        return navmeshAdapter.get();
    }

    services::ISocketProvider* RuntimeBootstrap::getSocketProvider()
    {
        return socketAdapter.get();
    }

    services::IAnimatorProvider* RuntimeBootstrap::getAnimatorProvider()
    {
        return animatorAdapter.get();
    }

    services::IIKProvider* RuntimeBootstrap::getIKProvider()
    {
        return ikAdapter.get();
    }

    services::IVFXRuntimeProvider* RuntimeBootstrap::getVFXRuntimeProvider()
    {
        return vfxRuntimeAdapter.get();
    }

    services::IPostProcessProvider* RuntimeBootstrap::getPostProcessProvider()
    {
        return postProcessAdapter.get();
    }

    services::IRenderTextureProvider* RuntimeBootstrap::getRenderTextureProvider()
    {
        return renderTextureAdapter.get();
    }

    services::IDebugDrawProvider* RuntimeBootstrap::getDebugDrawProvider()
    {
        return debugDrawAdapter.get();
    }

    WaterRenderAdapter* RuntimeBootstrap::getWaterRenderAdapterInternal()
    {
        return waterRenderAdapter.get();
    }

    window::Window* RuntimeBootstrap::getWindow()
    {
        return coreInterface ? coreInterface->getWindow() : nullptr;
    }

    void RuntimeBootstrap::setWindowTitle(const std::string& title)
    {
        if (auto* win = getWindow())
        {
            win->setTitle(title);
        }
    }

    void RuntimeBootstrap::setWindowIcon(const std::string& iconPath)
    {
        if (auto* win = getWindow())
        {
            win->setWindowIcon(iconPath);
        }
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
                // Reset per-frame rate limiters for script API
                NativeAPIRegistry::beginFrame();

                if (audioAdapter)
                {
                    audioAdapter->update();
                }
                if (cb)
                {
                    cb();
                }

                // Consume immediate debug draw data and forward to renderer
                if (debugDrawAdapter)
                {
                    auto drawList = debugDrawAdapter->consumeDrawList();
                    if (!drawList.empty())
                    {
                        offScreen->updateImmediateDebugDrawList(std::move(drawList));
                    }
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
