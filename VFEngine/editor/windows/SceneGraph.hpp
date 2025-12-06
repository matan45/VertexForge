#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "interfaces/ISceneService.hpp"
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
		void drawEntityNode(services::EntityHandle handle, services::ISceneService& sceneService);
		void drawDetails(services::EntityHandle handle, services::ISceneService& sceneService);
	};
}
