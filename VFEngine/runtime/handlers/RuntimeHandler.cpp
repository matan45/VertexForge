#include "RuntimeHandler.hpp"
#include "RuntimeBootstrap.hpp"
#include "impl/SceneServiceImpl.hpp"
#include "impl/RenderServiceImpl.hpp"
#include "impl/InputServiceImpl.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ApplicationEvents.hpp"

namespace handlers {

    RuntimeHandler::RuntimeHandler()
        : bootstrap(std::make_unique<core::RuntimeBootstrap>()) {}

    RuntimeHandler::~RuntimeHandler() = default;

    void RuntimeHandler::init() {
        // Initialize core systems via bootstrap
        bootstrap->init();

        // Initialize services with providers from bootstrap
        initializeServices();

        // Set up frame callback to update services each frame
        bootstrap->setFrameCallback([this]() {
            if (inputService) {
                inputService->update();
            }
        });

        // Subscribe to window events from Services
        setupEventSubscriptions();
    }

    void RuntimeHandler::run() const {
        bootstrap->run();
    }

    void RuntimeHandler::cleanUp() {
        // Unsubscribe from events before cleanup
        cleanupEventSubscriptions();

        // Reset services before graphics cleanup to release Vulkan resources
        renderService.reset();
        sceneService.reset();
        inputService.reset();

        // Clean up via bootstrap
        bootstrap->cleanUp();
    }

    bool RuntimeHandler::loadScene(const std::string& scenePath) {
        // TODO: Implement scene loading
        // This will load a serialized scene file and populate the ECS
        return false;
    }

    void RuntimeHandler::initializeServices() {
        // Get shared instances from the bootstrap
        auto sceneGraphSystem = bootstrap->getSceneGraphSystem();

        // Create service implementations using providers from bootstrap
        // nullTextureProvider is a member - ensures proper lifetime management
        auto sceneServiceImpl = std::make_shared<services::SceneServiceImpl>(sceneGraphSystem);
        auto renderServiceImpl = std::make_shared<services::RenderServiceImpl>(
            bootstrap->getOffScreenProvider(),
            &nullTextureProvider
        );
        auto inputServiceImpl = std::make_shared<services::InputServiceImpl>(bootstrap->getWindow());

        sceneService = sceneServiceImpl;
        renderService = renderServiceImpl;
        inputService = inputServiceImpl;

        // Register event handlers for command/query pattern
        sceneServiceImpl->registerEventHandlers();
        renderServiceImpl->registerEventHandlers();
        inputServiceImpl->registerEventHandlers();
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
