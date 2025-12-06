#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "interfaces/ISceneService.hpp"
#include "data/EntityHandle.hpp"

// Legacy includes for backward compatibility
#include "scene/SceneGraphSystem.hpp"
#include <memory>

namespace windows {
	class SceneGraph : public controllers::imguiHandler::ImguiWindow
	{
	private:
		// Service-based selection
		services::EntityHandle selectedHandle;

		// Legacy selection (for backward compatibility)
		entt::entity selected = entt::null;

		// Legacy reference
		std::shared_ptr<scene::SceneGraphSystem> sceneGraphSystem;

		// Flag to use services
		bool useServices = false;

	public:
		explicit SceneGraph(std::shared_ptr<scene::SceneGraphSystem> sceneGraphSystem);
		~SceneGraph() override = default;

		void draw() override;

		// Enable service-based mode
		void enableServiceMode() { useServices = true; }

	private:
		// Legacy methods
		void drawEntityNode(scene::Entity entity);
		void drawDetails(entt::entity entity) const;
		void drawDynamicComponent(scene::Entity entity) const;
		void drawDragDropTarget(scene::Entity entity) const;

		// Service-based methods
		void drawWithServices();
		void drawEntityNodeWithServices(services::EntityHandle handle, services::ISceneService& sceneService);
		void drawDetailsWithServices(services::EntityHandle handle, services::ISceneService& sceneService);
	};
}

