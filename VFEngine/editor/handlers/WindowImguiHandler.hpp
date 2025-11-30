#pragma once
#include "imguiHandler/ImguiWindowHandler.hpp"
#include "OffScreen.hpp"
#include "CoreInterface.hpp"
#include "../windows/SceneGraph.hpp"
#include "scene/LevelHandler.hpp"

namespace handlers {
	class WindowImguiHandler
	{
	private:
		controllers::OffScreen& offscreen;
		controllers::CoreInterface& coreInterface;
		std::shared_ptr<scene::SceneGraphSystem> sceneGraphSystem;
	public:
		explicit WindowImguiHandler(controllers::OffScreen& offscreen, controllers::CoreInterface& coreInterface);
		~WindowImguiHandler() = default;

		void init() const;
		void cleanUp() const;

	};
}


