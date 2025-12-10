#pragma once
#include <string_view>
#include <nlohmann/json.hpp>
#include "../scene/Entity.hpp"
namespace scene {
	class SceneGraphSystem;
}

namespace serialization
{
	using json = nlohmann::json;
	class SceneSerialization
	{
	public:
		static scene::SceneGraphSystem loadScene(std::string_view filename);
		static bool loadSceneInto(std::string_view filename, scene::SceneGraphSystem& sceneGraph);
		static bool saveScene(scene::SceneGraphSystem& sceneGraph, std::string_view filename);

	private:

		static json serializeEntity(scene::Entity& entity);
		static void deserializeEntity(const json& entityJson, scene::Entity& entity, scene::SceneGraphSystem& sceneGraph, bool isRoot);
		static void deserializeChildren(const json& childrenJson, scene::Entity& parent, scene::SceneGraphSystem& sceneGraph);

		static json serializeTransform(const components::TransformComponent& transform);
		static void deserializeTransform(const json& j, components::TransformComponent& transform);

		static json serializeCamera(const components::CameraComponent& camera);
		static void deserializeCamera(const json& j, components::CameraComponent& camera);

		static json serializeIBL(const components::IBLComponent& ibl);
		static std::string deserializeIBL(const json& j);



	};
}
