#pragma once
#include "imguiHandler/ImguiWindowHandler.hpp"
#include "OffScreen.hpp"
#include "CoreInterface.hpp"
#include "../windows/SceneGraph.hpp"
#include "../windows/ViewPort.hpp"
#include "../windows/MainImguiWindow.hpp"
#include "../windows/ContentBrowser.hpp"
#include "scene/LevelHandler.hpp"

namespace handlers {
	class WindowImguiHandler
	{
	private:
		controllers::OffScreen& offscreen;
		controllers::CoreInterface& coreInterface;
		std::shared_ptr<scene::SceneGraphSystem> sceneGraphSystem;

		// Store window pointers to enable service mode later
		std::shared_ptr<windows::SceneGraph> sceneGraphWindow;
		std::shared_ptr<windows::ViewPort> viewPortWindow;
		std::shared_ptr<windows::MainImguiWindow> mainImguiWindow;
		std::shared_ptr<windows::ContentBrowser> contentBrowserWindow;

	public:
		explicit WindowImguiHandler(controllers::OffScreen& offscreen, controllers::CoreInterface& coreInterface);
		~WindowImguiHandler() = default;

		void init();
		void cleanUp() const;

		// Enable service mode on all windows
		void enableServiceMode();
	};
}


