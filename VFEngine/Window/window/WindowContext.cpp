#include "WindowContext.hpp"

namespace window {
	void WindowContext::init(bool loadEditorIcon)
	{
		mainWindow = std::make_unique<Window>();
		mainWindow->initWindow(loadEditorIcon);
	}

	void WindowContext::cleanup()
	{
		mainWindow.reset();
	}

}