#pragma once
#include <memory>
#include "CoreInterface.hpp"
#include "WindowImguiHandler.hpp"
#include "OffScreen.hpp"

// Service includes
#include "interfaces/ISceneService.hpp"
#include "interfaces/IRenderService.hpp"
#include "interfaces/IInputService.hpp"
#include "interfaces/IResourceService.hpp"

namespace handlers {

	class EditorHandler
	{
	private:
		std::unique_ptr<controllers::CoreInterface> coreInterface;
		std::unique_ptr<controllers::OffScreen> offScreenInterface;

		std::unique_ptr<WindowImguiHandler> windowImguiHandler;

		// Service implementations (stored to keep them alive)
		std::shared_ptr<services::ISceneService> sceneService;
		std::shared_ptr<services::IRenderService> renderService;
		std::shared_ptr<services::IInputService> inputService;
		std::shared_ptr<services::IResourceService> resourceService;

	public:
		explicit EditorHandler();
		~EditorHandler();

		void init();
		void run() const;
		void cleanUp();
	
	private:
		void initializeServices();
	};
}
