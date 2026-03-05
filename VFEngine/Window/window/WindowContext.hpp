#pragma once

#include "Window.hpp"
#include <memory>

namespace window {
	class WindowContext
	{
	private:
		inline static std::unique_ptr<Window> mainWindow;

	public:
		static void init(bool loadEditorIcon = true);
		static void cleanup();

		static Window* getWindow() {
			return mainWindow.get();
		}
	};
}


