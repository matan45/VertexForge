#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "data/EntityHandle.hpp"

namespace windows {
	class SceneGraph : public controllers::imguiHandler::ImguiWindow
	{
	private:
		services::EntityHandle selectedHandle;

	public:
		SceneGraph() = default;
		~SceneGraph() override = default;

		void draw() override;

	private:
		void drawEntityNode(services::EntityHandle handle);
		void drawDetails(services::EntityHandle handle);
		void dragDropEntity(services::EntityHandle handle);
	};
}
