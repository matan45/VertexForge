#include "ViewPort.hpp"
#include "imgui.h"
#include "time/Timer.hpp"
#include "scene/EntityRegistry.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace windows {

	ViewPort::ViewPort(controllers::OffScreen& offscreen, std::shared_ptr<scene::SceneGraphSystem> sceneGraphSystem)
		: offscreen{ offscreen }, sceneGraphSystem{ sceneGraphSystem }
	{
	}

	void ViewPort::draw()
	{
		if (ImGui::Begin("ViewPort")) {
			// Handle camera input when viewport is focused or hovered
			if (ImGui::IsWindowFocused() || ImGui::IsWindowHovered()) {
				handleCameraInput();
			}

			ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
			ImGui::Image(offscreen.render(), ImVec2{viewportPanelSize.x, viewportPanelSize.y});
		}
		ImGui::End();
	}

	void ViewPort::handleCameraInput()
	{
		auto* transform = getFirstCameraTransform();
		if (!transform) {
			return;
		}

		float deltaTime = static_cast<float>(engineTime::Timer::getDeltaTime());
		float velocity = cameraSpeed * deltaTime;

		// Calculate forward and right vectors based on camera rotation
		float yawRad = glm::radians(transform->rotation.y);
		float pitchRad = glm::radians(transform->rotation.x);

		// Forward vector (where the camera is looking)
		glm::vec3 forward;
		forward.x = cos(pitchRad) * sin(yawRad);
		forward.y = -sin(pitchRad);
		forward.z = cos(pitchRad) * cos(yawRad);
		forward = glm::normalize(forward);

		// Right vector (perpendicular to forward on XZ plane)
		glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));

		// Up vector (world up for Q/E movement)
		glm::vec3 up(0.0f, 1.0f, 0.0f);

		// WASD movement
		if (ImGui::IsKeyDown(ImGuiKey_W)) {
			transform->position += forward * velocity;
			transform->isDirty = true;
		}
		if (ImGui::IsKeyDown(ImGuiKey_S)) {
			transform->position -= forward * velocity;
			transform->isDirty = true;
		}
		if (ImGui::IsKeyDown(ImGuiKey_A)) {
			transform->position -= right * velocity;
			transform->isDirty = true;
		}
		if (ImGui::IsKeyDown(ImGuiKey_D)) {
			transform->position += right * velocity;
			transform->isDirty = true;
		}

		// Q/E for up/down movement
		if (ImGui::IsKeyDown(ImGuiKey_E)) {
			transform->position += up * velocity;
			transform->isDirty = true;
		}
		if (ImGui::IsKeyDown(ImGuiKey_Q)) {
			transform->position -= up * velocity;
			transform->isDirty = true;
		}

		// Mouse look (right mouse button held)
		if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
			ImVec2 mousePos = ImGui::GetMousePos();

			if (!rightMousePressed) {
				// First frame of right click - store initial position
				rightMousePressed = true;
				lastMouseX = mousePos.x;
				lastMouseY = mousePos.y;
			}
			else {
				// Calculate mouse delta
				float deltaX = mousePos.x - lastMouseX;
				float deltaY = mousePos.y - lastMouseY;

				// Update rotation (yaw and pitch)
				transform->rotation.y += deltaX * mouseSensitivity;
				transform->rotation.x += deltaY * mouseSensitivity;

				// Clamp pitch to avoid gimbal lock
				transform->rotation.x = glm::clamp(transform->rotation.x, -89.0f, 89.0f);

				transform->isDirty = true;

				lastMouseX = mousePos.x;
				lastMouseY = mousePos.y;
			}
		}
		else {
			rightMousePressed = false;
		}
	}

	components::TransformComponent* ViewPort::getFirstCameraTransform() const
	{
		auto& registry = scene::EntityRegistry::getRegistry();
		auto view = registry.view<components::CameraComponent, components::TransformComponent>();
		for (auto entity : view) {
			return &view.get<components::TransformComponent>(entity);
		}
		return nullptr;
	}

}
