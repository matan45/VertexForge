#pragma once
#include <memory>
#include "WindowImguiHandler.hpp"

#include "interfaces/ISceneService.hpp"
#include "interfaces/IEditorRenderService.hpp"
#include "interfaces/IInputService.hpp"
#include "interfaces/IWindowStateService.hpp"
#include "interfaces/IPreviewService.hpp"
#include "interfaces/IEditorModeService.hpp"
#include "interfaces/IAudioService.hpp"
#include "interfaces/IScriptingService.hpp"
#include "events/EventTypes.hpp"

namespace core {
	class EditorBootstrap;
}

namespace core::audio {
	class AudioSceneUpdater;
}

namespace handlers {

	class EditorHandler
	{
	private:
		// Bootstrap encapsulates Core/Graphics initialization and provides service adapters
		std::unique_ptr<core::EditorBootstrap> bootstrap;

		std::unique_ptr<WindowImguiHandler> windowImguiHandler;
		
		std::shared_ptr<services::ISceneService> sceneService;
		std::shared_ptr<services::IEditorRenderService> renderService;
		std::shared_ptr<services::IInputService> inputService;
		std::shared_ptr<services::IWindowStateService> windowStateService;
		std::shared_ptr<services::IPreviewService> previewService;
		std::shared_ptr<services::IEditorModeService> editorModeService;
		std::shared_ptr<services::IAudioService> audioService;
		std::shared_ptr<services::IScriptingService> scriptingService;
		std::unique_ptr<core::audio::AudioSceneUpdater> audioSceneUpdater;

		events::SubscriptionToken resizeSubscription;

	public:
		explicit EditorHandler();
		~EditorHandler();

		void init();
		void run() const;
		void cleanUp();

	private:
		void initializeServices();
		void setupEventSubscriptions();
		void cleanupEventSubscriptions();
	};
}
