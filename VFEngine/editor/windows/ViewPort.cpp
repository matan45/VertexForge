#include "ViewPort.hpp"
#include "events/EventDispatcher.hpp"
#include "events/RenderEvents.hpp"
#include "events/SceneEvents.hpp"
#include "time/Timer.hpp"
#include "imgui.h"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "data/DTOs.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace windows {

	ViewPort::ViewPort() 
		: editorCamera(std::make_unique<editor::EditorCamera>()) 
	{
	}

	void ViewPort::draw()
	{
		auto& dispatcher = events::EventDispatcher::instance();

		if (ImGui::Begin("ViewPort")) {
			// Right-click context menu for viewport settings
			if (ImGui::BeginPopupContextWindow("ViewportContextMenu")) {
				events::render::GetShowBillboardIconsQuery query;
				bool showBillboards = dispatcher.query(query);

				if (ImGui::Checkbox("Show Billboard Icons", &showBillboards)) {
					events::render::SetShowBillboardIconsCommand cmd;
					cmd.show = showBillboards;
					dispatcher.execute(cmd);
				}
				ImGui::EndPopup();
			}

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
			
			events::render::UpdateMeshCameraCommand meshCameraCmd;
			meshCameraCmd.viewMatrix = editorCamera->getViewMatrix();
			meshCameraCmd.projectionMatrix = editorCamera->getProjectionMatrix();
			meshCameraCmd.cameraPosition = editorCamera->position;
			meshCameraCmd.time = static_cast<float>(engineTime::Timer::getElapsedTime());
			dispatcher.execute(meshCameraCmd);

			// Get viewport position for picking calculations
			ImVec2 viewportPos = ImGui::GetCursorScreenPos();

			// Get viewport texture through event system
			events::render::GetViewportTextureQuery query;
			auto texture = dispatcher.query(query);
			if (texture.isValid()) {
				ImGui::Image(texture.imguiDescriptorSet, ImVec2{viewportPanelSize.x, viewportPanelSize.y});
			}

			// Update billboard screen positions for picking
			updateBillboardScreenPositions(glm::vec2(viewportPos.x, viewportPos.y),
			                               glm::vec2(viewportPanelSize.x, viewportPanelSize.y));

			// Handle left-click picking (only when not in camera look mode)
			if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
				&& !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
				ImVec2 mousePos = ImGui::GetMousePos();
				auto picked = pickBillboardAt(glm::vec2(mousePos.x, mousePos.y));
				if (picked.has_value()) {
					events::scene::SelectEntityCommand cmd;
					cmd.entity = *picked;
					dispatcher.execute(cmd);
				}
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

	void ViewPort::updateBillboardScreenPositions(glm::vec2 viewportPos, glm::vec2 viewportSize)
	{
		cachedBillboardHits.clear();

		if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f) {
			return;
		}

		glm::mat4 viewMatrix = editorCamera->getViewMatrix();
		glm::mat4 projMatrix = editorCamera->getProjectionMatrix();

		auto& registry = scene::EntityRegistry::getRegistry();
		auto view = registry.view<components::BillboardComponent, components::WorldTransformComponent>();

		for (auto entity : view) {
			const auto& billboard = view.get<components::BillboardComponent>(entity);
			const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

			// Skip non-selectable or non-editor billboards
			if (!billboard.selectable || !billboard.editorOnly) {
				continue;
			}

			// Get world position from transform matrix
			glm::vec3 worldPos = glm::vec3(worldTransform.worldMatrix[3]);

			// Project to clip space
			glm::vec4 clipPos = projMatrix * viewMatrix * glm::vec4(worldPos, 1.0f);

			// Behind camera check
			if (clipPos.w <= 0.0f) {
				continue;
			}

			// Convert to NDC
			glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;

			// Skip if outside frustum
			if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f) {
				continue;
			}

			// Convert to screen space
			glm::vec2 screenPos;
			screenPos.x = (ndc.x * 0.5f + 0.5f) * viewportSize.x + viewportPos.x;
			screenPos.y = (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportSize.y + viewportPos.y;

			BillboardScreenHit hit;
			// Convert entt::entity to EntityHandle
			hit.entity = services::EntityHandle{ static_cast<uint64_t>(static_cast<uint32_t>(entity)) };
			hit.screenCenter = screenPos;
			hit.screenSize = billboard.size;  // Size is in screen pixels for ScreenSpace mode

			cachedBillboardHits.push_back(hit);
		}
	}

	std::optional<services::EntityHandle> ViewPort::pickBillboardAt(glm::vec2 screenPos)
	{
		// Iterate in reverse order (last rendered = closest to camera for screen-space billboards)
		for (auto it = cachedBillboardHits.rbegin(); it != cachedBillboardHits.rend(); ++it) {
			const auto& hit = *it;

			glm::vec2 halfSize = hit.screenSize * 0.5f;
			glm::vec2 minBounds = hit.screenCenter - halfSize;
			glm::vec2 maxBounds = hit.screenCenter + halfSize;

			if (screenPos.x >= minBounds.x && screenPos.x <= maxBounds.x &&
				screenPos.y >= minBounds.y && screenPos.y <= maxBounds.y) {
				return hit.entity;
			}
		}
		return std::nullopt;
	}

}
