#pragma once
#include <memory>
#include "WindowImguiHandler.hpp"

// Service includes
#include "interfaces/ISceneService.hpp"
#include "interfaces/IRenderService.hpp"
#include "interfaces/IInputService.hpp"
#include "interfaces/IPreviewService.hpp"
#include "events/EventTypes.hpp"

// Forward declaration for EditorBootstrap
namespace core {
	class EditorBootstrap;
}

namespace handlers {

	class EditorHandler
	{
	private:
		// Bootstrap encapsulates Core/Graphics initialization and provides service adapters
		std::unique_ptr<core::EditorBootstrap> bootstrap;

		std::unique_ptr<WindowImguiHandler> windowImguiHandler;

		// Service implementations (stored to keep them alive)
		std::shared_ptr<services::ISceneService> sceneService;
		std::shared_ptr<services::IRenderService> renderService;
		std::shared_ptr<services::IInputService> inputService;
		std::shared_ptr<services::IPreviewService> previewService;

		// Event subscription tokens
		events::SubscriptionToken resizeSubscription;
		events::SubscriptionToken minimizeSubscription;
		events::SubscriptionToken restoreSubscription;
		events::SubscriptionToken focusSubscription;

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
