#include "EditorHandler.hpp"
#include "EditorBootstrap.hpp"
#include "impl/SceneServiceImpl.hpp"
#include "impl/RenderServiceImpl.hpp"
#include "impl/InputServiceImpl.hpp"
#include "impl/PreviewServiceImpl.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ApplicationEvents.hpp"
#include "Import.hpp"
#include "print/EditorLogger.hpp"

namespace handlers {
	EditorHandler::EditorHandler()
		: bootstrap{ std::make_unique<core::EditorBootstrap>() }
		, windowImguiHandler{ std::make_unique<WindowImguiHandler>() }
	{
	}

	EditorHandler::~EditorHandler() = default;


	void EditorHandler::init()
	{
		// Initialize core systems via bootstrap
		bootstrap->init();

		// Initialize Import controller (Editor calls Import directly)
		controllers::Import::initialize();

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

		windowImguiHandler->init();
	}

	void EditorHandler::run() const
	{
		bootstrap->run();
	}

	void EditorHandler::cleanUp()
	{
		// Unsubscribe from events before cleanup
		cleanupEventSubscriptions();

		windowImguiHandler->cleanUp();

		// Reset services before graphics cleanup to release Vulkan resources
		previewService.reset();
		renderService.reset();
		sceneService.reset();
		inputService.reset();

		// Clean up via bootstrap
		bootstrap->cleanUp();
	}

	void EditorHandler::initializeServices()
	{
		// Get shared instances from the bootstrap
		auto sceneGraphSystem = bootstrap->getSceneGraphSystem();

		// Create service implementations using providers from bootstrap
		auto sceneServiceImpl = std::make_shared<services::SceneServiceImpl>(sceneGraphSystem);
		auto renderServiceImpl = std::make_shared<services::RenderServiceImpl>(
			bootstrap->getOffScreenProvider(),
			bootstrap->getEditorTextureProvider()
		);
		auto inputServiceImpl = std::make_shared<services::InputServiceImpl>(bootstrap->getWindow());
		auto previewServiceImpl = std::make_shared<services::PreviewServiceImpl>(
			bootstrap->getPreviewProvider()
		);

		sceneService = sceneServiceImpl;
		renderService = renderServiceImpl;
		inputService = inputServiceImpl;
		previewService = previewServiceImpl;

		// Register event handlers for command/query pattern
		sceneServiceImpl->registerEventHandlers();
		renderServiceImpl->registerEventHandlers();
		inputServiceImpl->registerEventHandlers();
		previewServiceImpl->registerEventHandlers();
	}

	void EditorHandler::setupEventSubscriptions()
	{
		auto& dispatcher = events::EventDispatcher::instance();

		// Subscribe to window resize events
		resizeSubscription = dispatcher.subscribe<events::application::WindowResizedNotification>(
			[this](const events::application::WindowResizedNotification&) {
				bootstrap->triggerResize();
			});

		// Subscribe to window minimize events (pause rendering when minimized)
		minimizeSubscription = dispatcher.subscribe<events::application::WindowMinimizedNotification>(
			[](const events::application::WindowMinimizedNotification&) {
				// Could pause rendering or other expensive operations here
			});

		// Subscribe to window restore events
		restoreSubscription = dispatcher.subscribe<events::application::WindowRestoredNotification>(
			[](const events::application::WindowRestoredNotification&) {
				// Could resume rendering or other operations here
			});

		// Subscribe to window focus events
		focusSubscription = dispatcher.subscribe<events::application::WindowFocusedNotification>(
			[](const events::application::WindowFocusedNotification&) {
				// Could handle focus changes (e.g., pause input when unfocused)
			});
	}

	void EditorHandler::cleanupEventSubscriptions()
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
		if (focusSubscription.isValid()) {
			dispatcher.unsubscribe(focusSubscription);
			focusSubscription = {};
		}
	}
}
