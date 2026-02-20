#include "RuntimeHandler.hpp"
#include "RuntimeBootstrap.hpp"
#include "impl/SceneServiceImpl.hpp"
#include "impl/RuntimeRenderServiceImpl.hpp"
#include "impl/InputServiceImpl.hpp"
#include "impl/WindowStateServiceImpl.hpp"
#include "impl/AudioServiceImpl.hpp"
#include "impl/ScriptingServiceImpl.hpp"
#include "impl/ProjectServiceImpl.hpp"
#include "impl/scene/WaterService.hpp"
#include "impl/PhysicsServiceImpl.hpp"
#include "impl/PhysicsAnimationServiceImpl.hpp"
#include "impl/NavmeshServiceImpl.hpp"
#include "impl/PhysicsPlayModeHandler.hpp"
#include "../audio/AudioSceneUpdater.hpp"
#include "../adapters/WaterRenderAdapter.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ApplicationEvents.hpp"
#include "events/EditorModeEvents.hpp"
#include "events/ProjectEvents.hpp"
#include "events/SceneEvents.hpp"
#include "print/EditorLogger.hpp"
#include <filesystem>
#include "time/Timer.hpp"

namespace handlers {

    RuntimeHandler::RuntimeHandler()
        : bootstrap(std::make_unique<core::RuntimeBootstrap>()) {}

    RuntimeHandler::~RuntimeHandler() = default;

    void RuntimeHandler::init() {
        bootstrap->init();
        
        initializeServices();
        
        bootstrap->setFrameCallback([this]() {
            if (inputService) {
                inputService->update();
            }
            if (windowStateService) {
                windowStateService->update();
            }

            float deltaTime = static_cast<float>(engineTime::Timer::getDeltaTime());

            // Update order: Physics → Scripts → Audio
            if (physicsPlayModeHandler) {
                physicsPlayModeHandler->update(deltaTime);
            }

            if (scriptingService) {
                scriptingService->updateScripts(deltaTime);
            }

            if (audioSceneUpdater) {
                audioSceneUpdater->updateListenerFromPrimaryCamera();
            }
        });
        
        setupEventSubscriptions();
    }

    void RuntimeHandler::run() const {
        bootstrap->run();
    }

    void RuntimeHandler::cleanUp() {
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
        physicsAnimationService.reset();
        physicsService.reset();
        navmeshService.reset();
        waterService.reset();
        projectService.reset();
        audioSceneUpdater.reset();
        audioService.reset();
        scriptingService.reset();
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

        std::filesystem::path scenePath =
            std::filesystem::path(projectOpt->workingDirectory) / projectOpt->startupScene;

        if (!std::filesystem::exists(scenePath)) {
            vfLogError("Startup scene not found: {}", scenePath.string());
            dispatcher.execute(events::scene::NewSceneCommand{});
            return false;
        }

        events::scene::LoadSceneCommand sceneCmd;
        sceneCmd.filePath = scenePath.string();
        if (!dispatcher.execute(sceneCmd)) {
            vfLogError("Failed to load startup scene: {}", scenePath.string());
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
            bootstrap->getAnimatorProvider()
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
