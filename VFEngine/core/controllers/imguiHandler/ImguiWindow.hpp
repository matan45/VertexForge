#pragma once
// getting the pointer from editor and do the draw in new frame and end frame then render the pipline
namespace controllers::imguiHandler {

		class ImguiWindow
		{
		public:
			virtual void draw() = 0;
			virtual bool shouldClose() const { return false; }
			virtual ~ImguiWindow() = default;
		};

};
