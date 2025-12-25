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
		// Save entity (and children) to prefab file
		// Returns true on success
		static bool savePrefab(const scene::Entity& entity, std::string_view filename);

		// Load prefab and instantiate as new entity under parent
		// Returns the root entity of the instantiated prefab, or nullopt on failure
		static std::optional<scene::Entity> loadPrefab(
			std::string_view filename,
			scene::Entity& parent,
			scene::SceneGraphSystem& sceneGraph
		);

		// Validate prefab file without instantiating
		static bool validatePrefab(std::string_view filename);

	private:
		// Serialize entity tree to JSON (recursive)
		static json serializeEntityTree(const scene::Entity& entity);

		// Deserialize with NEW UUIDs (not restoring originals)
		static scene::Entity deserializeEntityTree(
			const json& entityJson,
			scene::Entity& parent,
			scene::SceneGraphSystem& sceneGraph
		);

		// Deserialize components from JSON to entity (uses SceneSerialization helpers)
		static void deserializeComponents(const json& componentsJson, scene::Entity& entity);
	};
}
