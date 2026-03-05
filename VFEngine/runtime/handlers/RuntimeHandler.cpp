#include "print/Log.hpp"
#include "RuntimeHandler.hpp"
#include "runtime/RuntimeBootstrap.hpp"
#include "impl/project/SceneServiceImpl.hpp"
#include "impl/render/RuntimeRenderServiceImpl.hpp"
#include "impl/input/InputServiceImpl.hpp"
#include "impl/editor/WindowStateServiceImpl.hpp"
#include "impl/audio/AudioServiceImpl.hpp"
#include "impl/scripting/ScriptingServiceImpl.hpp"
#include "impl/project/ProjectServiceImpl.hpp"
#include "impl/scene/WaterService.hpp"
#include "impl/physics/PhysicsServiceImpl.hpp"
#include "impl/physics/PhysicsAnimationServiceImpl.hpp"
#include "impl/navmesh/NavmeshServiceImpl.hpp"
#include "impl/physics/PhysicsPlayModeHandler.hpp"
#include "impl/physics/ControllerServiceImpl.hpp"
#include "../audio/AudioSceneUpdater.hpp"
#include "../adapters/terrain/WaterRenderAdapter.hpp"
#include "impl/render/RenderTextureServiceImpl.hpp"
#include "impl/render/RenderTexturePlayModeHandler.hpp"
#include "impl/render/DebugDrawServiceImpl.hpp"
#include "events/EventDispatcher.hpp"
#include "events/project/ApplicationEvents.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/project/ProjectEvents.hpp"
#include "events/project/SceneEvents.hpp"
#include "resource/PathResolver.hpp"
#include <filesystem>
#include "time/Timer.hpp"
#include "core/PluginManager.hpp"

namespace handlers {

    RuntimeHandler::RuntimeHandler()
        : bootstrap(std::make_unique<core::RuntimeBootstrap>()) {}

    RuntimeHandler::~RuntimeHandler() = default;

    void RuntimeHandler::init() {
        resource::PathResolver::initialize();

        bootstrap->init();

        initializeServices();

        pluginManager = std::make_unique<plugin::PluginManager>(std::unordered_set<std::string>{
            std::string(plugin::capability::audio),
            std::string(plugin::capability::physics),
            std::string(plugin::capability::scripting)
        });
        auto exePath = std::filesystem::current_path();
        auto pluginsDir = exePath / "plugins";
        if (!std::filesystem::exists(pluginsDir)) {
            pluginsDir = exePath / "../../plugins";
        }
        pluginManager->loadAll(pluginsDir);
        pluginManager->initializeAll();

        bootstrap->setFrameCallback([this]() {
            if (inputService) {
                inputService->update();
            }
            if (windowStateService) {
                windowStateService->update();
            }

            float deltaTime = static_cast<float>(engineTime::Timer::getDeltaTime());

            // Update order: Physics → Scripts → Controller Movement → Audio
            if (physicsPlayModeHandler) {
                physicsPlayModeHandler->update(deltaTime);
            }

            if (scriptingService) {
                scriptingService->updateScripts(deltaTime);
            }

            if (controllerService) {
                controllerService->applyControllerMovement(deltaTime);
            }

            if (renderTexturePlayModeHandler) {
                renderTexturePlayModeHandler->update(deltaTime);
            }

            if (audioSceneUpdater) {
                audioSceneUpdater->updateListenerFromPrimaryCamera();
            }

            if (pluginManager) {
                pluginManager->updateAll(deltaTime);
            }
        });
        
        setupEventSubscriptions();
    }

    void RuntimeHandler::run() const {
        bootstrap->run();
    }

    void RuntimeHandler::cleanUp() {
        pluginManager.reset();

        cleanupEventSubscriptions();

        if (physicsPlayModeHandler)
        {
            auto& dispatcher = events::EventDispatcher::instance();
            events::editor::EditorModeChangedNotification notification;
            notification.previousMode = services::EditorMode::Play;
            notification.currentMode = services::EditorMode::Edit;
            dispatcher.publish(notification);
        }

        physicsPlayModeHandler.reset();
        renderTexturePlayModeHandler.reset();
        controllerService.reset();
        ikComponentService.reset();
        physicsAnimationService.reset();
        physicsService.reset();
        navmeshService.reset();
        waterService.reset();
        projectService.reset();
        audioSceneUpdater.reset();
        audioService.reset();
        scriptingService.reset();
        renderTextureService.reset();
        debugDrawService.reset();
        renderService.reset();
        sceneService.reset();
        windowStateService.reset();
        inputService.reset();

        bootstrap->cleanUp();
    }

    bool RuntimeHandler::loadProject(const std::string& projectPath) {
        auto& dispatcher = events::EventDispatcher::instance();

        events::project::LoadProjectCommand loadCmd;
        loadCmd.filePath = projectPath;
        if (!dispatcher.execute(loadCmd)) {
            vfLogError("Failed to load project file: {}", projectPath);
            dispatcher.execute(events::scene::NewSceneCommand{});
            return false;
        }

        auto projectOpt = dispatcher.query(events::project::GetCurrentProjectQuery{});
        if (!projectOpt) {
            vfLogError("Failed to get project configuration");
            dispatcher.execute(events::scene::NewSceneCommand{});
            return false;
        }

        // Set window title from project config
        bootstrap->setWindowTitle(projectOpt->projectName);

        // Try to load a window icon (.vfImage) from the assets folder (optional)
        if (!projectOpt->exeIconPath.empty())
        {
            // Derive a .vfImage path from the icon path for GLFW window icon
            std::filesystem::path iconBase =
                std::filesystem::path(projectOpt->workingDirectory) / projectOpt->exeIconPath;
            // Try the path as-is first (user may have set a .vfImage directly)
            if (std::filesystem::exists(iconBase))
            {
                bootstrap->setWindowIcon(iconBase.string());
            }
            else
            {
                // Try with .vfImage extension (in case the config stores an .ico path)
                std::filesystem::path vfImageIcon = iconBase;
                vfImageIcon.replace_extension(".vfImage");
                if (std::filesystem::exists(vfImageIcon))
                {
                    bootstrap->setWindowIcon(vfImageIcon.string());
                }
            }
        }

        std::filesystem::path scenePath =
            std::filesystem::path(projectOpt->workingDirectory) / projectOpt->startupScene;

        if (!std::filesystem::exists(scenePath)) {
            vfLogError("Startup scene not found: {}", scenePath.string());
            dispatcher.execute(events::scene::NewSceneCommand{});
            return false;
        }

        events::scene::LoadSceneCommand sceneCmd;
        sceneCmd.filePath = scenePath.string();
        // loadScene is deferred: returns false only for invalid arguments (null sceneGraph, empty path).
        // Actual load success/failure is reported via SceneLoadingCompletedNotification.
        if (!dispatcher.execute(sceneCmd)) {
            vfLogError("Failed to queue startup scene load: {}", scenePath.string());
            dispatcher.execute(events::scene::NewSceneCommand{});
            return false;
        }

        // PhysicsPlayModeHandler listens for EditorModeChangedNotification
        if (physicsPlayModeHandler)
        {
            events::editor::EditorModeChangedNotification notification;
            notification.previousMode = services::EditorMode::Edit;
            notification.currentMode = services::EditorMode::Play;
            dispatcher.publish(notification);
        }

        return true;
    }

    void RuntimeHandler::initializeServices() {
        sceneService = std::make_shared<services::SceneServiceImpl>(
            bootstrap->getSceneGraphSystem(),
            bootstrap->getAnimatorProvider(),
            bootstrap->getSocketProvider()
        );
        renderService = std::make_shared<services::RuntimeRenderServiceImpl>(
            bootstrap->getOffScreenProvider(),
            bootstrap->getPostProcessProvider()
        );
        inputService = std::make_shared<services::InputServiceImpl>(bootstrap->getWindow());
        windowStateService = std::make_shared<services::WindowStateServiceImpl>(bootstrap->getWindow());
        audioService = std::make_shared<services::AudioServiceImpl>(bootstrap->getAudioProvider());
        audioSceneUpdater = std::make_unique<core::audio::AudioSceneUpdater>();

        scriptingService = std::make_shared<services::ScriptingServiceImpl>(
            bootstrap->getScriptingProvider(),
            bootstrap->getSceneGraphSystem()
        );

        projectService = std::make_shared<services::ProjectServiceImpl>();

        auto waterServiceImpl = std::make_shared<services::WaterService>(bootstrap->getSceneGraphSystem());
        waterService = waterServiceImpl;
        waterServiceImpl->setPhysicsProvider(bootstrap->getPhysicsProvider());
        if (auto* waterAdapter = bootstrap->getWaterRenderAdapterInternal())
        {
            waterAdapter->setWaterService(waterServiceImpl.get());
        }

        if (auto* physicsProvider = bootstrap->getPhysicsProvider())
        {
            physicsService = std::make_shared<services::PhysicsServiceImpl>(physicsProvider);
            physicsAnimationService = std::make_shared<services::PhysicsAnimationServiceImpl>(physicsProvider);
            physicsPlayModeHandler = std::make_unique<services::PhysicsPlayModeHandler>(physicsProvider);
            physicsPlayModeHandler->setWaterService(waterServiceImpl.get());
            physicsPlayModeHandler->subscribeToEvents();
        }

        if (auto* navmeshProvider = bootstrap->getNavmeshProvider())
        {
            navmeshService = std::make_shared<services::NavmeshServiceImpl>(navmeshProvider);
        }

        controllerService = std::make_shared<services::ControllerServiceImpl>(bootstrap->getPhysicsProvider());

        if (auto* ikProvider = bootstrap->getIKProvider())
        {
            ikComponentService = std::make_shared<services::IKComponentService>(ikProvider);
        }

        renderTextureService = std::make_shared<services::RenderTextureServiceImpl>(
            bootstrap->getRenderTextureProvider()
        );

        debugDrawService = std::make_shared<services::DebugDrawServiceImpl>(
            bootstrap->getDebugDrawProvider()
        );

        if (auto* rttProvider = bootstrap->getRenderTextureProvider())
        {
            renderTexturePlayModeHandler = std::make_unique<services::RenderTexturePlayModeHandler>(rttProvider);
            renderTexturePlayModeHandler->subscribeToEvents();
        }

        sceneService->registerEventHandlers();
        projectService->registerEventHandlers();
        renderService->registerEventHandlers();
        inputService->registerEventHandlers();
        windowStateService->registerEventHandlers();
        static_cast<services::AudioServiceImpl*>(audioService.get())->registerEventHandlers();
        static_cast<services::ScriptingServiceImpl*>(scriptingService.get())->registerEventHandlers();
        waterService->registerEventHandlers();
        if (physicsService)
        {
            physicsService->registerEventHandlers();
        }
        if (physicsAnimationService)
        {
            physicsAnimationService->registerEventHandlers();
        }
        if (navmeshService)
        {
            navmeshService->registerEventHandlers();
        }
        renderTextureService->registerEventHandlers();
        debugDrawService->registerEventHandlers();
        controllerService->registerEventHandlers();
        if (ikComponentService)
        {
            ikComponentService->registerEventHandlers();
        }
    }

    void RuntimeHandler::setupEventSubscriptions()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        resizeSubscription = dispatcher.subscribe<events::application::WindowResizedNotification>(
            [this](const events::application::WindowResizedNotification&) {
                bootstrap->triggerResize();
            });
    }

    void RuntimeHandler::cleanupEventSubscriptions()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (resizeSubscription.isValid()) {
            dispatcher.unsubscribe(resizeSubscription);
            resizeSubscription = {};
        }
    }

}
