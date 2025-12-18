#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "../camera/EditorCamera.hpp"
#include "data/EntityHandle.hpp"
#include "math/Frustum.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>
#include <optional>

namespace windows {
	// Cached screen-space data for billboard picking
	struct BillboardScreenHit {
		services::EntityHandle entity;
		glm::vec2 screenCenter;
		glm::vec2 screenSize;
	};

	// Cached data for mesh picking (ray-AABB intersection)
	struct MeshPickData {
		services::EntityHandle entity;
		math::AABB worldAABB;
		std::string meshPath;
	};

	class ViewPort : public controllers::imguiHandler::ImguiWindow
	{
	private:
		// Editor camera - standalone camera for scene navigation
		std::unique_ptr<editor::EditorCamera> editorCamera;

		// Mouse tracking for camera look
		bool isFirstMouseInput = true;
		float lastMouseX = 0.0f;
		float lastMouseY = 0.0f;

		// Billboard picking cache
		std::vector<BillboardScreenHit> cachedBillboardHits;

		// Mesh picking cache
		std::vector<MeshPickData> cachedMeshHits;

	public:
		ViewPort();
		~ViewPort() override = default;

		void draw() override;

		// Get the editor camera for renderer access
		editor::EditorCamera* getEditorCamera() const { return editorCamera.get(); }

	private:
		void handleCameraInput();
		void updateBillboardScreenPositions(glm::vec2 viewportPos, glm::vec2 viewportSize);
		std::optional<services::EntityHandle> pickBillboardAt(glm::vec2 screenPos);

		void updateMeshPickData();
		std::optional<services::EntityHandle> pickMeshAt(glm::vec2 screenPos, glm::vec2 viewportPos, glm::vec2 viewportSize);
		math::Ray screenToWorldRay(glm::vec2 screenPos, glm::vec2 viewportPos, glm::vec2 viewportSize);
	};
}
