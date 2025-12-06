#include "ViewPort.hpp"
#include "events/EventDispatcher.hpp"
#include "events/RenderEvents.hpp"
#include "events/SceneEvents.hpp"
#include "data/DTOs.hpp"
#include "time/Timer.hpp"
#include "imgui.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

namespace windows {

	void ViewPort::draw()
	{
		auto& dispatcher = events::EventDispatcher::instance();

		if (ImGui::Begin("ViewPort")) {
			// Handle camera input when viewport is focused or hovered
			if (ImGui::IsWindowFocused() || ImGui::IsWindowHovered()) {
				handleCameraInput();
			}

			ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();

			// Get viewport texture through event system
			events::render::GetViewportTextureQuery query;
			auto texture = dispatcher.query(query);
			if (texture.isValid()) {
				ImGui::Image(texture.imguiDescriptorSet, ImVec2{viewportPanelSize.x, viewportPanelSize.y});
			}
		}
		ImGui::End();
	}

	void ViewPort::handleCameraInput()
	{
		auto& dispatcher = events::EventDispatcher::instance();

		// Get primary camera through event system
		events::scene::GetPrimaryCameraQuery cameraQuery;
		auto cameraHandle = dispatcher.query(cameraQuery);
		if (!cameraHandle.has_value()) {
			return;
		}

		// Get current transform through event system
		events::scene::GetTransformQuery transformQuery;
		transformQuery.entity = *cameraHandle;
		auto transformOpt = dispatcher.query(transformQuery);
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
		if (ImGui::IsKeyDown(ImGuiKey_S)) {
			transform.position.x -= std::sin(yawRad) * cameraSpeed * dt;
			transform.position.z += std::cos(yawRad) * cameraSpeed * dt;
			transformChanged = true;
		}
		if (ImGui::IsKeyDown(ImGuiKey_A)) {
			transform.position.x -= std::cos(yawRad) * cameraSpeed * dt;
			transform.position.z -= std::sin(yawRad) * cameraSpeed * dt;
			transformChanged = true;
		}
		if (ImGui::IsKeyDown(ImGuiKey_D)) {
			transform.position.x += std::cos(yawRad) * cameraSpeed * dt;
			transform.position.z += std::sin(yawRad) * cameraSpeed * dt;
			transformChanged = true;
		}
		if (ImGui::IsKeyDown(ImGuiKey_E)) {
			transform.position.y += cameraSpeed * dt;
			transformChanged = true;
		}
		if (ImGui::IsKeyDown(ImGuiKey_Q)) {
			transform.position.y -= cameraSpeed * dt;
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

		// Update transform through event system if changed
		if (transformChanged) {
			events::scene::SetTransformCommand cmd;
			cmd.entity = *cameraHandle;
			cmd.transform = transform;
			dispatcher.execute(cmd);
		}
	}

}
