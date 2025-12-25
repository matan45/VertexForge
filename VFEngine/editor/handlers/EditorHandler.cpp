#include "EditorHandler.hpp"
#include "EditorBootstrap.hpp"
#include "impl/SceneServiceImpl.hpp"
#include "impl/EditorRenderServiceImpl.hpp"
#include "impl/InputServiceImpl.hpp"
#include "impl/WindowStateServiceImpl.hpp"
#include "impl/PreviewServiceImpl.hpp"
#include "impl/EditorModeServiceImpl.hpp"
#include "impl/AudioServiceImpl.hpp"
#include "../audio/AudioSceneUpdater.hpp"
#include "events/EventDispatcher.hpp"
#include "events/ApplicationEvents.hpp"
#include "events/RenderEvents.hpp"
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
		bootstrap->init();
		
		controllers::Import::initialize();

		// Initialize services with providers from bootstrap
		initializeServices();

		// Set up frame callback to update services each frame
		bootstrap->setFrameCallback([this]() {
			if (inputService) {
				inputService->update();
			}
			if (windowStateService) {
				windowStateService->update();
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
		cleanupEventSubscriptions();

		windowImguiHandler->cleanUp();
		
		previewService.reset();
		renderService.reset();
		sceneService.reset();
		
		audioSceneUpdater.reset();
		audioService.reset();
		editorModeService.reset();
		windowStateService.reset();
		inputService.reset();

		bootstrap->cleanUp();
	}

	void EditorHandler::initializeServices()
	{
		// Create service implementations using providers from bootstrap
		sceneService = std::make_shared<services::SceneServiceImpl>(bootstrap->getSceneGraphSystem());
		renderService = std::make_shared<services::EditorRenderServiceImpl>(
			bootstrap->getOffScreenProvider(),
			bootstrap->getEditorTextureProvider()
		);
		inputService = std::make_shared<services::InputServiceImpl>(bootstrap->getWindow());
		windowStateService = std::make_shared<services::WindowStateServiceImpl>(bootstrap->getWindow());
		previewService = std::make_shared<services::PreviewServiceImpl>(
			bootstrap->getMaterialPreviewProvider(),
			bootstrap->getMeshPreviewProvider()
		);
		editorModeService = std::make_shared<services::EditorModeServiceImpl>();
		audioService = std::make_shared<services::AudioServiceImpl>(bootstrap->getAudioProvider());
		audioSceneUpdater = std::make_unique<core::audio::AudioSceneUpdater>();

		// Register event handlers for command/query pattern
		sceneService->registerEventHandlers();
		renderService->registerEventHandlers();
		inputService->registerEventHandlers();
		windowStateService->registerEventHandlers();
		previewService->registerEventHandlers();
		editorModeService->registerEventHandlers();
		static_cast<services::AudioServiceImpl*>(audioService.get())->registerEventHandlers();

		events::render::LoadBillboardAtlasCommand atlasCmd;
		atlasCmd.atlasPath = "../../resources/editor/billboardAtlas.vfImage";
		events::EventDispatcher::instance().execute(atlasCmd);
	}

	void EditorHandler::setupEventSubscriptions()
	{
		auto& dispatcher = events::EventDispatcher::instance();
		
		resizeSubscription = dispatcher.subscribe<events::application::WindowResizedNotification>(
			[this](const events::application::WindowResizedNotification&) {
				bootstrap->triggerResize();
			});
		
		minimizeSubscription = dispatcher.subscribe<events::application::WindowMinimizedNotification>(
			[](const events::application::WindowMinimizedNotification&) {
				// Could pause rendering or other expensive operations here
			});

		restoreSubscription = dispatcher.subscribe<events::application::WindowRestoredNotification>(
			[](const events::application::WindowRestoredNotification&) {
				// Could resume rendering or other operations here
			});
		
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
