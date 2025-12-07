#include "ViewPort.hpp"
#include "events/EventDispatcher.hpp"
#include "events/RenderEvents.hpp"
#include "time/Timer.hpp"
#include "imgui.h"

namespace windows {

	ViewPort::ViewPort() 
		: editorCamera(std::make_unique<editor::EditorCamera>()) 
	{
	}

	void ViewPort::draw()
	{
		auto& dispatcher = events::EventDispatcher::instance();

		if (ImGui::Begin("ViewPort")) {
			// Handle camera input when viewport is focused or hovered
			if (ImGui::IsWindowFocused() || ImGui::IsWindowHovered()) {
				handleCameraInput();
			}

			ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();

			// Update editor camera aspect ratio based on viewport size
			if (viewportPanelSize.x > 0 && viewportPanelSize.y > 0) {
				editorCamera->setAspectRatio(viewportPanelSize.x / viewportPanelSize.y);
			}

			// Update IBL camera matrices with editor camera matrices each frame
			events::render::UpdateIBLCameraCommand cameraCmd;
			cameraCmd.viewMatrix = editorCamera->getViewMatrix();
			cameraCmd.projectionMatrix = editorCamera->getProjectionMatrix();
			dispatcher.execute(cameraCmd);

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
		float dt = static_cast<float>(engineTime::Timer::getDeltaTime());

		// Camera movement (WASD + Q/E)
		bool forward = ImGui::IsKeyDown(ImGuiKey_W);
		bool backward = ImGui::IsKeyDown(ImGuiKey_S);
		bool left = ImGui::IsKeyDown(ImGuiKey_A);
		bool right = ImGui::IsKeyDown(ImGuiKey_D);
		bool up = ImGui::IsKeyDown(ImGuiKey_E);
		bool down = ImGui::IsKeyDown(ImGuiKey_Q);
		bool sprint = ImGui::IsKeyDown(ImGuiKey_LeftShift);

		if (forward || backward || left || right || up || down) {
			editorCamera->processKeyboardInput(dt, forward, backward, left, right, up, down, sprint);
		}

		// Mouse look (right mouse button held)
		if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
			ImGui::SetMouseCursor(ImGuiMouseCursor_None);
			ImVec2 mousePos = ImGui::GetMousePos();

			if (isFirstMouseInput) {
				lastMouseX = mousePos.x;
				lastMouseY = mousePos.y;
				isFirstMouseInput = false;
			}
			else {
				float xOffset = mousePos.x - lastMouseX;
				float yOffset = mousePos.y - lastMouseY;

				editorCamera->processMouseMovement(xOffset, yOffset);

				lastMouseX = mousePos.x;
				lastMouseY = mousePos.y;
			}
		}
		else {
			isFirstMouseInput = true;
		}
	}

}
