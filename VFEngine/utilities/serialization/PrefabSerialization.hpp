#pragma once
#include <string_view>
#include <optional>
#include <nlohmann/json.hpp>
#include "../scene/Entity.hpp"

namespace scene {
	class SceneGraphSystem;
}

namespace serialization
{
	using json = nlohmann::json;

	class PrefabSerialization
	{
	public:
		static bool savePrefab(const scene::Entity& entity, std::string_view filename);
		
		static std::optional<scene::Entity> loadPrefab(
			std::string_view filename,
			scene::Entity& parent,
			scene::SceneGraphSystem& sceneGraph
		);
		
		static bool validatePrefab(std::string_view filename);

	private:
		static json serializeEntityTree(const scene::Entity& entity);
		
		static scene::Entity deserializeEntityTree(
			const json& entityJson,
			scene::Entity& parent,
			scene::SceneGraphSystem& sceneGraph
		);
		
		static void deserializeComponents(const json& componentsJson, scene::Entity& entity);
	};
}
