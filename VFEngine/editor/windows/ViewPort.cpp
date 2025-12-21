#include "ViewPort.hpp"
#include "events/EventDispatcher.hpp"
#include "events/RenderEvents.hpp"
#include "events/SceneEvents.hpp"
#include "events/EditorModeEvents.hpp"
#include "time/Timer.hpp"
#include "imgui.h"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "data/DTOs.hpp"
#include "data/EntityConversion.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <limits>

namespace windows {

	ViewPort::ViewPort() 
		: editorCamera(std::make_unique<editor::EditorCamera>()) 
	{
	}

	void ViewPort::draw()
	{
		auto& dispatcher = events::EventDispatcher::instance();

		if (ImGui::Begin("ViewPort")) {
			// Handle camera input only when viewport is focused AND mouse is hovering
			bool isFocused = ImGui::IsWindowFocused();
			bool isHovered = ImGui::IsWindowHovered();
			if (isFocused && isHovered) {
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
			
			ImVec2 viewportPos = ImGui::GetCursorScreenPos();

			events::render::GetViewportTextureQuery query;
			auto texture = dispatcher.query(query);
			if (texture.isValid()) {
				ImGui::Image(texture.imguiDescriptorSet, ImVec2{viewportPanelSize.x, viewportPanelSize.y});
			}

			// Check if we're in Play mode - skip picking/selection in Play mode
			bool isPlayMode = dispatcher.query(events::editor::IsPlayModeQuery{});

			// Update picking data (only in Edit mode)
			glm::vec2 vp(viewportPos.x, viewportPos.y);
			glm::vec2 vs(viewportPanelSize.x, viewportPanelSize.y);
			if (!isPlayMode) {
				updateBillboardScreenPositions(vp, vs);
				updateMeshPickData();
			}

			// Handle picking (only in Edit mode)
			if (!isPlayMode && ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
				&& !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
				ImVec2 mousePos = ImGui::GetMousePos();
				glm::vec2 mp(mousePos.x, mousePos.y);

				// Try billboard picking first (higher priority - smaller targets)
				auto picked = pickBillboardAt(mp);

				// If no billboard hit, try mesh picking
				if (!picked.has_value()) {
					picked = pickMeshAt(mp, vp, vs);
				}

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
			
			glm::vec3 worldPos = glm::vec3(worldTransform.worldMatrix[3]);
			
			glm::vec4 clipPos = projMatrix * viewMatrix * glm::vec4(worldPos, 1.0f);

			// Behind camera check
			if (clipPos.w <= 0.0f) {
				continue;
			}

			// Convert to NDC
			glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
			
			if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f) {
				continue;
			}

			// Convert to screen space
			// Note: No Y flip needed because EditorCamera's projection matrix already
			// flips Y for Vulkan (projectionMatrix[1][1] *= -1), so ndc.y = -1 is top, +1 is bottom
			glm::vec2 screenPos;
			screenPos.x = (ndc.x * 0.5f + 0.5f) * viewportSize.x + viewportPos.x;
			screenPos.y = (ndc.y * 0.5f + 0.5f) * viewportSize.y + viewportPos.y;

			BillboardScreenHit hit;
			hit.entity = services::internal::toHandle(entity);
			hit.screenCenter = screenPos;
			hit.screenSize = billboard.size;  // Size is in screen pixels for ScreenSpace mode

			cachedBillboardHits.push_back(hit);
		}
	}

	std::optional<services::EntityHandle> ViewPort::pickBillboardAt(glm::vec2 screenPos)
	{
		auto& registry = scene::EntityRegistry::getRegistry();

		// Iterate in reverse order (last rendered = closest to camera for screen-space billboards)
		for (auto it = cachedBillboardHits.rbegin(); it != cachedBillboardHits.rend(); ++it) {
			const auto& hit = *it;

			glm::vec2 halfSize = hit.screenSize * 0.5f;
			glm::vec2 minBounds = hit.screenCenter - halfSize;
			glm::vec2 maxBounds = hit.screenCenter + halfSize;

			if (screenPos.x >= minBounds.x && screenPos.x <= maxBounds.x &&
				screenPos.y >= minBounds.y && screenPos.y <= maxBounds.y) {
				// Validate entity still exists and has BillboardComponent (prevents race condition
				// if entity was deleted or component removed between cache update and pick)
				auto enttEntity = services::internal::fromHandle(hit.entity);
				if (registry.valid(enttEntity) &&
					registry.all_of<components::BillboardComponent>(enttEntity)) {
					return hit.entity;
				}
				// Entity was deleted or no longer has billboard, skip and continue searching
			}
		}
		return std::nullopt;
	}

	void ViewPort::updateMeshPickData()
	{
		cachedMeshHits.clear();

		auto& dispatcher = events::EventDispatcher::instance();
		auto& registry = scene::EntityRegistry::getRegistry();
		auto view = registry.view<components::MeshComponent, components::WorldTransformComponent>();

		for (auto entity : view) {
			const auto& meshComp = view.get<components::MeshComponent>(entity);
			const auto& worldTransform = view.get<components::WorldTransformComponent>(entity);

			if (meshComp.meshPath.empty()) {
				continue;
			}

			// Get mesh bounding box from renderer
			events::render::GetMeshBoundingBoxQuery query;
			query.meshPath = meshComp.meshPath;
			auto bounds = dispatcher.query(query);

			if (!bounds.has_value()) {
				continue;
			}

			// Transform local AABB to world space
			math::AABB localAABB(bounds->min, bounds->max);
			math::AABB worldAABB = localAABB.getTransformed(worldTransform.worldMatrix);

			MeshPickData pickData;
			pickData.entity = services::internal::toHandle(entity);
			pickData.worldAABB = worldAABB;
			pickData.meshPath = meshComp.meshPath;

			cachedMeshHits.push_back(pickData);
		}
	}

	math::Ray ViewPort::screenToWorldRay(glm::vec2 screenPos, glm::vec2 viewportPos, glm::vec2 viewportSize)
	{
		screenPos.x = glm::clamp(screenPos.x, viewportPos.x, viewportPos.x + viewportSize.x);
		screenPos.y = glm::clamp(screenPos.y, viewportPos.y, viewportPos.y + viewportSize.y);

		// Convert screen position to normalized viewport coordinates [0, 1]
		float normalizedX = (screenPos.x - viewportPos.x) / viewportSize.x;
		float normalizedY = (screenPos.y - viewportPos.y) / viewportSize.y;

		// Convert to NDC [-1, 1]
		// Note: Y is already in correct orientation due to Vulkan's flipped projection
		float ndcX = normalizedX * 2.0f - 1.0f;
		float ndcY = normalizedY * 2.0f - 1.0f;

		// Get inverse matrices
		glm::mat4 invProj = glm::inverse(editorCamera->getProjectionMatrix());
		glm::mat4 invView = glm::inverse(editorCamera->getViewMatrix());

		// Unproject near and far points
		glm::vec4 nearPoint = invProj * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
		glm::vec4 farPoint = invProj * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);

		// Perspective divide
		nearPoint /= nearPoint.w;
		farPoint /= farPoint.w;

		// Transform to world space
		glm::vec3 worldNear = glm::vec3(invView * nearPoint);
		glm::vec3 worldFar = glm::vec3(invView * farPoint);
		
		glm::vec3 direction = glm::normalize(worldFar - worldNear);
		return math::Ray(worldNear, direction);
	}

	std::optional<services::EntityHandle> ViewPort::pickMeshAt(glm::vec2 screenPos, glm::vec2 viewportPos, glm::vec2 viewportSize)
	{
		if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f) {
			return std::nullopt;
		}

		auto& registry = scene::EntityRegistry::getRegistry();
		
		math::Ray ray = screenToWorldRay(screenPos, viewportPos, viewportSize);

		// Find closest hit
		float closestDistance = std::numeric_limits<float>::max();
		std::optional<services::EntityHandle> closestEntity;

		for (const auto& meshData : cachedMeshHits) {
			auto hitDistance = meshData.worldAABB.intersectRay(ray);
			if (hitDistance.has_value() && *hitDistance < closestDistance) {
				// Validate entity still exists
				auto enttEntity = services::internal::fromHandle(meshData.entity);
				if (registry.valid(enttEntity) &&
					registry.all_of<components::MeshComponent>(enttEntity)) {
					closestDistance = *hitDistance;
					closestEntity = meshData.entity;
				}
			}
		}

		return closestEntity;
	}

}
