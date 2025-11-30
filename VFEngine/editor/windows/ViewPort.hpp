#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "OffScreen.hpp"
#include "scene/SceneGraphSystem.hpp"

namespace windows {
	class ViewPort : public controllers::imguiHandler::ImguiWindow
	{
	private:
		controllers::OffScreen& offscreen;
		std::shared_ptr<scene::SceneGraphSystem> sceneGraphSystem;
		float cameraSpeed = 5.0f;
		float mouseSensitivity = 0.1f;
		bool isFirst = true;
		float lastMouseX = 0.0f;
		float lastMouseY = 0.0f;

	public:
		explicit ViewPort(controllers::OffScreen& offscreen, std::shared_ptr<scene::SceneGraphSystem> sceneGraphSystem);
		~ViewPort() override = default;

		void draw() override;

	private:
		void handleCameraInput();
		void cameraMovement(components::TransformComponent* transform, float dt, float speed);
		components::TransformComponent* getFirstCameraTransform() const;
	};
}

