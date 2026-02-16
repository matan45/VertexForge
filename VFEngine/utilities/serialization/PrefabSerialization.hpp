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
		static json serializeEntityTreeComponents(const scene::Entity& entity);
		static void serializeRenderComponents(const scene::Entity& entity, json& out);
		static void serializePhysicsAndEffectComponents(const scene::Entity& entity, json& out);

		static scene::Entity deserializeEntityTree(
			const json& entityJson,
			scene::Entity& parent,
			scene::SceneGraphSystem& sceneGraph
		);

		static void deserializeComponents(const json& componentsJson, scene::Entity& entity);
		static void deserializeRenderingComponents(const json& componentsJson, scene::Entity& entity);
		static void deserializeSceneComponents(const json& componentsJson, scene::Entity& entity);
		static void deserializeMediaComponents(const json& componentsJson, scene::Entity& entity);
		static void deserializeLightComponents(const json& componentsJson, scene::Entity& entity);

		static std::optional<json> parsePrefabJson(std::string_view filename);
	};
}
