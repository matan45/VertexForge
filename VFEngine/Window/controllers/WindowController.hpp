#pragma once

namespace window {
	class Window;
}

namespace controllers {

	class WindowController {
	public:
		static void init(bool loadEditorIcon = true);
		static window::Window* getWindow();
		static void cleanUp();
	};
}