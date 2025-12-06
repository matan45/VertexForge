#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "interfaces/ISceneService.hpp"
#include "interfaces/IRenderService.hpp"

// Legacy includes for backward compatibility during migration
#include "OffScreen.hpp"
#include "scene/SceneGraphSystem.hpp"

namespace windows {
	class ViewPort : public controllers::imguiHandler::ImguiWindow
	{
	private:
		// Legacy references (kept for backward compatibility)
		controllers::OffScreen& offscreen;
		std::shared_ptr<scene::SceneGraphSystem> sceneGraphSystem;

		// Camera control state
		float cameraSpeed = 5.0f;
		float mouseSensitivity = 0.1f;
		bool isFirst = true;
		float lastMouseX = 0.0f;
		float lastMouseY = 0.0f;

		// Flag to use new service-based rendering
		bool useServices = false;

	public:
		// Legacy constructor (backward compatible)
		explicit ViewPort(controllers::OffScreen& offscreen, std::shared_ptr<scene::SceneGraphSystem> sceneGraphSystem);
		~ViewPort() override = default;

		void draw() override;

		// Enable service-based rendering
		void enableServiceMode() { useServices = true; }

	private:
		void handleCameraInput();
		void handleCameraInputWithServices();
		void cameraMovement(components::TransformComponent* transform, float dt, float speed);
		components::TransformComponent* getFirstCameraTransform() const;
	};
}

