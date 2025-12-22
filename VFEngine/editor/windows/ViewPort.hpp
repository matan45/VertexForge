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
	
	struct BillboardScreenHit {
		services::EntityHandle entity;
		glm::vec2 screenCenter;
		glm::vec2 screenSize;
	};
	
	struct MeshPickData {
		services::EntityHandle entity;
		math::AABB worldAABB;
		std::string meshPath;
	};

	class ViewPort : public controllers::imguiHandler::ImguiWindow
	{
	private:
		std::unique_ptr<editor::EditorCamera> editorCamera;

		// Mouse tracking for camera look
		bool isFirstMouseInput = true;
		float lastMouseX = 0.0f;
		float lastMouseY = 0.0f;
		
		std::vector<BillboardScreenHit> cachedBillboardHits;
		
		std::vector<MeshPickData> cachedMeshHits;

	public:
		explicit ViewPort();
		~ViewPort() override = default;

		void draw() override;
		
		editor::EditorCamera* getEditorCamera() const { return editorCamera.get(); }

	private:
		void handleCameraInput();
		void drawViewportOverlay();
		void updateBillboardScreenPositions(glm::vec2 viewportPos, glm::vec2 viewportSize);
		std::optional<services::EntityHandle> pickBillboardAt(glm::vec2 screenPos);

		void updateMeshPickData();
		std::optional<services::EntityHandle> pickMeshAt(glm::vec2 screenPos, glm::vec2 viewportPos, glm::vec2 viewportSize);
		math::Ray screenToWorldRay(glm::vec2 screenPos, glm::vec2 viewportPos, glm::vec2 viewportSize);
	};
}
