#pragma once
#include "imguiHandler/ImguiWindow.hpp"

namespace windows {
	class ViewPort : public controllers::imguiHandler::ImguiWindow
	{
	private:
		// Camera control state
		float cameraSpeed = 5.0f;
		float mouseSensitivity = 0.1f;
		bool isFirst = true;
		float lastMouseX = 0.0f;
		float lastMouseY = 0.0f;

	public:
		ViewPort() = default;
		~ViewPort() override = default;

		void draw() override;

	private:
		void handleCameraInput();
	};
}
