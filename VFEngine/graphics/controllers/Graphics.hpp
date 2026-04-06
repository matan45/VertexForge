#pragma once


namespace window {
	class Window;
}

namespace controllers {
	class Graphics
	{
		
	public:

		static void createContext(window::Window* glfwWindow, bool enablePipelineCache = false);
		static void destroyContext();

	private:
		explicit Graphics() = default;
		~Graphics() = default;
	};
};


