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
#include "../audio/AudioSceneUpdater.hpp"
#include "../adapters/WaterRenderAdapter.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ApplicationEvents.hpp"
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
            // Update scripts every frame in runtime
            if (scriptingService) {
                float deltaTime = static_cast<float>(engineTime::Timer::getDeltaTime());
                scriptingService->updateScripts(deltaTime);
            }
            // Update audio listener from primary camera
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

        waterService.reset();
        projectService.reset();
        audioSceneUpdater.reset();
        audioService.reset();
        scriptingService.reset();
        renderService.reset();
        sceneService.reset();
        windowStateService.reset();
        inputService.reset();

        // Clean up via bootstrap
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

        return true;
    }

    void RuntimeHandler::initializeServices() {
        // Create service implementations using providers from bootstrap
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

        // Create water service and wire to render adapter
        auto waterServiceImpl = std::make_shared<services::WaterService>(bootstrap->getSceneGraphSystem());
        waterService = waterServiceImpl;
        waterServiceImpl->setPhysicsProvider(bootstrap->getPhysicsProvider());
        if (auto* waterAdapter = bootstrap->getWaterRenderAdapterInternal())
        {
            waterAdapter->setWaterService(waterServiceImpl.get());
        }

        // Register event handlers for command/query pattern
        sceneService->registerEventHandlers();
        projectService->registerEventHandlers();
        renderService->registerEventHandlers();
        inputService->registerEventHandlers();
        windowStateService->registerEventHandlers();
        static_cast<services::AudioServiceImpl*>(audioService.get())->registerEventHandlers();
        static_cast<services::ScriptingServiceImpl*>(scriptingService.get())->registerEventHandlers();
        waterService->registerEventHandlers();
    }

    void RuntimeHandler::setupEventSubscriptions()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // Subscribe to window resize events
        resizeSubscription = dispatcher.subscribe<events::application::WindowResizedNotification>(
            [this](const events::application::WindowResizedNotification&) {
                bootstrap->triggerResize();
            });

        // Subscribe to window minimize events (pause game when minimized)
        minimizeSubscription = dispatcher.subscribe<events::application::WindowMinimizedNotification>(
            [](const events::application::WindowMinimizedNotification&) {
                // Could pause game loop or reduce update frequency here
            });

        // Subscribe to window restore events
        restoreSubscription = dispatcher.subscribe<events::application::WindowRestoredNotification>(
            [](const events::application::WindowRestoredNotification&) {
                // Could resume game loop here
            });
    }

    void RuntimeHandler::cleanupEventSubscriptions()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (resizeSubscription.isValid()) {
            dispatcher.unsubscribe(resizeSubscription);
            resizeSubscription = {};
        }
        if (minimizeSubscription.isValid()) {
            dispatcher.unsubscribe(minimizeSubscription);
            minimizeSubscription = {};
        }
        if (restoreSubscription.isValid()) {
            dispatcher.unsubscribe(restoreSubscription);
            restoreSubscription = {};
        }
    }

}
