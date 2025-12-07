#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "../camera/EditorCamera.hpp"
#include <memory>

namespace windows {
	class ViewPort : public controllers::imguiHandler::ImguiWindow
	{
	private:
		// Editor camera - standalone camera for scene navigation
		std::unique_ptr<editor::EditorCamera> editorCamera;
		
		// Mouse tracking for camera look
		bool isFirstMouseInput = true;
		float lastMouseX = 0.0f;
		float lastMouseY = 0.0f;

	public:
		ViewPort();
		~ViewPort() override = default;

		void draw() override;

		// Get the editor camera for renderer access
		editor::EditorCamera* getEditorCamera() const { return editorCamera.get(); }

	private:
		void handleCameraInput();
	};
}
