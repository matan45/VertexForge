#include "ViewPort.hpp"
#include "imgui.h"
#include "time/Timer.hpp"
#include "scene/EntityRegistry.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

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

		float dt = static_cast<float>(engineTime::Timer::getDeltaTime());

		// Camera movement (WASD + Q/E)
		cameraMovement(transform, dt, cameraSpeed);

		// Mouse look (right mouse button held)
		if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
			ImGui::SetMouseCursor(ImGuiMouseCursor_None);
			ImVec2 mousePos = ImGui::GetMousePos();

			if (isFirst) {
				// First frame - just capture position, don't rotate
				lastMouseX = mousePos.x;
				lastMouseY = mousePos.y;
				isFirst = false;
			}
			else {
				// Calculate delta and apply rotation
				float xOffset = mousePos.x - lastMouseX;
				float yOffset = mousePos.y - lastMouseY;

				// Mouse X movement -> Yaw (rotation.y) - negated for natural feel
				// Mouse Y movement -> Pitch (rotation.x)
				transform->rotation.y -= xOffset * mouseSensitivity;
				transform->rotation.x -= yOffset * mouseSensitivity;

				// Reset yaw if exceeds 360 degrees
				if (transform->rotation.y >= 360.0f || transform->rotation.y <= -360.0f) {
					transform->rotation.y = 0.0f;
				}

				// Clamp pitch to avoid flipping
				transform->rotation.x = glm::clamp(transform->rotation.x, -89.0f, 89.0f);

				transform->isDirty = true;

				lastMouseX = mousePos.x;
				lastMouseY = mousePos.y;
			}
		}
		else {
			// Reset when mouse button is released
			isFirst = true;
		}
	}

	void ViewPort::cameraMovement(components::TransformComponent* transform, float dt, float speed)
	{
		float yawRad = transform->rotation.y / 180.0f * glm::pi<float>();

		if (ImGui::IsKeyDown(ImGuiKey_W)) {
			transform->position.x += std::sin(yawRad) * speed * dt;
			transform->position.z -= std::cos(yawRad) * speed * dt;
			transform->isDirty = true;
		}
		else if (ImGui::IsKeyDown(ImGuiKey_A)) {
			transform->position.x -= std::cos(yawRad) * speed * dt;
			transform->position.z -= std::sin(yawRad) * speed * dt;
			transform->isDirty = true;
		}
		else if (ImGui::IsKeyDown(ImGuiKey_D)) {
			transform->position.x += std::cos(yawRad) * speed * dt;
			transform->position.z += std::sin(yawRad) * speed * dt;
			transform->isDirty = true;
		}
		else if (ImGui::IsKeyDown(ImGuiKey_S)) {
			transform->position.x -= std::sin(yawRad) * speed * dt;
			transform->position.z += std::cos(yawRad) * speed * dt;
			transform->isDirty = true;
		}
		else if (ImGui::IsKeyDown(ImGuiKey_E)) {
			transform->position.y += -1.0f * speed * dt;
			transform->isDirty = true;
		}
		else if (ImGui::IsKeyDown(ImGuiKey_Q)) {
			transform->position.y += 1.0f * speed * dt;
			transform->isDirty = true;
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
