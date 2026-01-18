#include "EditorBootstrap.hpp"
#include "../controllers/CoreInterface.hpp"
#include "../controllers/OffScreen.hpp"
#include "../adapters/OffScreenAdapter.hpp"
#include "../adapters/EditorTextureAdapter.hpp"
#include "../adapters/MaterialPreviewAdapter.hpp"
#include "../adapters/MeshPreviewAdapter.hpp"
#include "../adapters/AnimationPreviewAdapter.hpp"
#include "../adapters/AnimatorAdapter.hpp"
#include "../adapters/AudioAdapter.hpp"
#include "../adapters/ScriptingAdapter.hpp"
#include "../adapters/PhysicsAdapter.hpp"
#include "../adapters/NativeAPIRegistry.hpp"
#include "scene/LevelHandler.hpp"
#include "print/Logger.hpp"
#include "../../utilities/types/PhysicsTypes.hpp"
#include "../../graphics/animation/RuntimeAnimatorSystem.hpp"

namespace core
{
    EditorBootstrap::EditorBootstrap()
        : coreInterface(std::make_unique<::controllers::CoreInterface>())
          , offScreen(std::make_unique<::controllers::OffScreen>())
    {
    }

    EditorBootstrap::~EditorBootstrap() = default;

    void EditorBootstrap::init()
    {
        coreInterface->init();

        offScreenAdapter = std::make_unique<OffScreenAdapter>(offScreen.get());
        textureAdapter = std::make_unique<EditorTextureAdapter>();
        materialPreviewAdapter = std::make_unique<MaterialPreviewAdapter>();
        meshPreviewAdapter = std::make_unique<MeshPreviewAdapter>();
        animationPreviewAdapter = std::make_unique<AnimationPreviewAdapter>();
        animatorAdapter = std::make_unique<AnimatorAdapter>();
        audioAdapter = std::make_unique<AudioAdapter>();
        scriptingAdapter = std::make_unique<ScriptingAdapter>();
        physicsAdapter = std::make_unique<PhysicsAdapter>();

        offScreen->init();
        audioAdapter->init();
        scriptingAdapter->init();
        physicsAdapter->init();

        // Initialize runtime animator system to subscribe to mesh data change events
        animation::RuntimeAnimatorSystem::instance().initialize();

        // Apply default physics settings on startup
        // Scene-specific settings will be loaded when a scene is loaded
        physicsAdapter->applySettings(types::PhysicsSettings::createDefault());

        coreInterface->setResizeCallback([this]()
        {
            offScreen->recreate();
        });
    }

    void EditorBootstrap::run() const
    {
        coreInterface->run();
    }

    void EditorBootstrap::cleanUp()
    {
        // Shutdown runtime animator system first to unsubscribe from events
        animation::RuntimeAnimatorSystem::instance().shutdown();

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

        animatorAdapter.reset();
        animationPreviewAdapter.reset();
        meshPreviewAdapter.reset();
        materialPreviewAdapter.reset();
        textureAdapter.reset();
        offScreenAdapter.reset();
        audioAdapter.reset();
        scriptingAdapter.reset();
        physicsAdapter.reset();

        if (coreInterface)
        {
            coreInterface->cleanUp();
        }
    }

    services::IOffScreenProvider* EditorBootstrap::getOffScreenProvider()
    {
        return offScreenAdapter.get();
    }

    services::IEditorTextureProvider* EditorBootstrap::getEditorTextureProvider()
    {
        return textureAdapter.get();
    }

    services::IMaterialPreviewProvider* EditorBootstrap::getMaterialPreviewProvider()
    {
        return materialPreviewAdapter.get();
    }

    services::IMeshPreviewProvider* EditorBootstrap::getMeshPreviewProvider()
    {
        return meshPreviewAdapter.get();
    }

    services::IAnimationPreviewProvider* EditorBootstrap::getAnimationPreviewProvider()
    {
        return animationPreviewAdapter.get();
    }

    services::IAnimatorProvider* EditorBootstrap::getAnimatorProvider()
    {
        return animatorAdapter.get();
    }

    services::IAudioProvider* EditorBootstrap::getAudioProvider()
    {
        return audioAdapter.get();
    }

    services::IScriptingProvider* EditorBootstrap::getScriptingProvider()
    {
        return scriptingAdapter.get();
    }

    services::IPhysicsProvider* EditorBootstrap::getPhysicsProvider()
    {
        return physicsAdapter.get();
    }

    window::Window* EditorBootstrap::getWindow()
    {
        return coreInterface ? coreInterface->getWindow() : nullptr;
    }

    std::shared_ptr<scene::SceneGraphSystem> EditorBootstrap::getSceneGraphSystem()
    {
        auto level = scene::LevelHandler::getInstance();
        return level ? level->getSceneGraphSystem() : nullptr;
    }

    void EditorBootstrap::setFrameCallback(std::function<void()> callback)
    {
        if (coreInterface)
        {
            // Wrap the callback to also update audio and async loading each frame
            coreInterface->setFrameCallback([this, cb = std::move(callback)]()
            {
                // Reset per-frame rate limiters for script API
                NativeAPIRegistry::beginFrame();

                if (audioAdapter)
                {
                    audioAdapter->update();
                }
               
                if (meshPreviewAdapter)
                {
                    meshPreviewAdapter->processAsyncLoading();
                }
                
                if (textureAdapter)
                {
                    textureAdapter->processAsyncLoading();
                }
                if (cb)
                {
                    cb();
                }
            });
        }
    }

    void EditorBootstrap::triggerResize()
    {
        if (coreInterface)
        {
            coreInterface->triggerResize();
        }
    }

}
