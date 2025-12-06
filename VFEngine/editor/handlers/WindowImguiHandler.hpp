#pragma once
#include "imguiHandler/ImguiWindowHandler.hpp"
#include "../windows/SceneGraph.hpp"
#include "../windows/ViewPort.hpp"
#include "../windows/MainImguiWindow.hpp"
#include "../windows/ContentBrowser.hpp"

namespace handlers {
	class WindowImguiHandler
	{
	private:
		// Store window pointers
		std::shared_ptr<windows::SceneGraph> sceneGraphWindow;
		std::shared_ptr<windows::ViewPort> viewPortWindow;
		std::shared_ptr<windows::MainImguiWindow> mainImguiWindow;
		std::shared_ptr<windows::ContentBrowser> contentBrowserWindow;

	public:
		WindowImguiHandler() = default;
		~WindowImguiHandler() = default;

		void init();
		void cleanUp() const;
	};
}
