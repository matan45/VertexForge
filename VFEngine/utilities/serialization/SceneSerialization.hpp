#pragma once
#include <string_view>

namespace scene {
	class SceneGraphSystem;
}

namespace serialization
{
	class SceneSerialization
	{
	public:
		static scene::SceneGraphSystem loadScene(std::string_view filename);
		static bool saveScene(const scene::SceneGraphSystem& sceneGraph, std::string_view filename);
	};
}

