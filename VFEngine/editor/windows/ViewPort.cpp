#include "ViewPort.hpp"
#include "imgui.h"
#include "time/Timer.hpp"
#include "scene/EntityRegistry.hpp"
#include "ServiceLocator.hpp"
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
				if (useServices) {
					handleCameraInputWithServices();
				} else {
					handleCameraInput();
				}
			}

			ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();

			if (useServices) {
				// Use service-based rendering
				auto renderService = services::ServiceLocator::instance().tryGet<services::IRenderService>();
				if (renderService) {
					auto texture = renderService->getViewportTexture();
					if (texture.isValid()) {
						ImGui::Image(texture.imguiDescriptorSet, ImVec2{viewportPanelSize.x, viewportPanelSize.y});
					}
				}
			} else {
				// Legacy rendering
				ImGui::Image(offscreen.render(), ImVec2{viewportPanelSize.x, viewportPanelSize.y});
			}
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

	void ViewPort::handleCameraInputWithServices()
	{
		// Get scene service to find camera
		auto sceneService = services::ServiceLocator::instance().tryGet<services::ISceneService>();
		if (!sceneService) {
			return;
		}

		// Get primary camera
		auto cameraHandle = sceneService->getPrimaryCamera();
		if (!cameraHandle.has_value()) {
			return;
		}

		// Get current transform
		auto transformOpt = sceneService->getTransform(*cameraHandle);
		if (!transformOpt.has_value()) {
			return;
		}

		services::TransformData transform = *transformOpt;
		float dt = static_cast<float>(engineTime::Timer::getDeltaTime());
		bool transformChanged = false;

		// Camera movement (WASD + Q/E)
		float yawRad = transform.rotation.y / 180.0f * glm::pi<float>();

		if (ImGui::IsKeyDown(ImGuiKey_W)) {
			transform.position.x += std::sin(yawRad) * cameraSpeed * dt;
			transform.position.z -= std::cos(yawRad) * cameraSpeed * dt;
			transformChanged = true;
		}
		else if (ImGui::IsKeyDown(ImGuiKey_A)) {
			transform.position.x -= std::cos(yawRad) * cameraSpeed * dt;
			transform.position.z -= std::sin(yawRad) * cameraSpeed * dt;
			transformChanged = true;
		}
		else if (ImGui::IsKeyDown(ImGuiKey_D)) {
			transform.position.x += std::cos(yawRad) * cameraSpeed * dt;
			transform.position.z += std::sin(yawRad) * cameraSpeed * dt;
			transformChanged = true;
		}
		else if (ImGui::IsKeyDown(ImGuiKey_S)) {
			transform.position.x -= std::sin(yawRad) * cameraSpeed * dt;
			transform.position.z += std::cos(yawRad) * cameraSpeed * dt;
			transformChanged = true;
		}
		else if (ImGui::IsKeyDown(ImGuiKey_E)) {
			transform.position.y += -1.0f * cameraSpeed * dt;
			transformChanged = true;
		}
		else if (ImGui::IsKeyDown(ImGuiKey_Q)) {
			transform.position.y += 1.0f * cameraSpeed * dt;
			transformChanged = true;
		}

		// Mouse look (right mouse button held)
		if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
			ImGui::SetMouseCursor(ImGuiMouseCursor_None);
			ImVec2 mousePos = ImGui::GetMousePos();

			if (isFirst) {
				lastMouseX = mousePos.x;
				lastMouseY = mousePos.y;
				isFirst = false;
			}
			else {
				float xOffset = mousePos.x - lastMouseX;
				float yOffset = mousePos.y - lastMouseY;

				transform.rotation.y -= xOffset * mouseSensitivity;
				transform.rotation.x -= yOffset * mouseSensitivity;

				if (transform.rotation.y >= 360.0f || transform.rotation.y <= -360.0f) {
					transform.rotation.y = 0.0f;
				}
				transform.rotation.x = glm::clamp(transform.rotation.x, -89.0f, 89.0f);

				transformChanged = true;
				lastMouseX = mousePos.x;
				lastMouseY = mousePos.y;
			}
		}
		else {
			isFirst = true;
		}

		// Update transform through service if changed
		if (transformChanged) {
			sceneService->setTransform(*cameraHandle, transform);
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
