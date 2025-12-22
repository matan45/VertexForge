#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "../camera/EditorCamera.hpp"
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"
#include "math/Frustum.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <utility>

namespace windows {

	// Viewport toolbar icon indices in the atlas (5 columns x 1 row)
	enum class ViewportIcon : uint32_t
	{
		Grid = 0,
		Move = 1,
		Rotate = 2,
		Scale = 3,
		Translate = 4
	};

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

		// Viewport toolbar icon atlas
		services::EditorTextureHandle iconAtlas;
		bool iconsLoaded = false;
		static constexpr uint32_t ATLAS_COLUMNS = 4;
		static constexpr uint32_t ATLAS_ROWS = 4;
		static constexpr float ICON_SIZE = 32.0f;

	public:
		explicit ViewPort();
		~ViewPort() override;

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

		// Icon atlas helpers
		void loadIconAtlas();
		std::pair<glm::vec2, glm::vec2> getIconUV(ViewportIcon icon) const;
		bool iconButton(ViewportIcon icon, bool isActive, const char* tooltip);
	};
}
