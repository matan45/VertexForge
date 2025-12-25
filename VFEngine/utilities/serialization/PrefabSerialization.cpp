#include "PrefabSerialization.hpp"
#include "SceneSerialization.hpp"
#include "JsonConverters.hpp"
#include "../scene/SceneGraphSystem.hpp"
#include "../components/Components.hpp"
#include "../print/EditorLogger.hpp"
#include <fstream>

namespace serialization {

	json PrefabSerialization::serializeEntityTree(scene::Entity& entity) {
		json entityJson;

		// Store name (UUID is NOT stored - will be generated fresh on load)
		entityJson["name"] = entity.getName();

		// Transform (always present per Entity constructor)
		if (entity.hasComponent<components::TransformComponent>()) {
			entityJson["transform"] = SceneSerialization::serializeTransform(
				entity.getComponent<components::TransformComponent>());
		}

		// Optional components
		json componentsJson = json::object();

		if (entity.hasComponent<components::CameraComponent>()) {
			componentsJson["camera"] = SceneSerialization::serializeCamera(
				entity.getComponent<components::CameraComponent>());
		}

		if (entity.hasComponent<components::IBLComponent>()) {
			componentsJson["ibl"] = SceneSerialization::serializeIBL(
				entity.getComponent<components::IBLComponent>());
		}

		if (entity.hasComponent<components::MeshComponent>()) {
			componentsJson["mesh"] = SceneSerialization::serializeMesh(
				entity.getComponent<components::MeshComponent>());
		}

		if (entity.hasComponent<components::MaterialComponent>()) {
			componentsJson["material"] = SceneSerialization::serializeMaterial(
				entity.getComponent<components::MaterialComponent>());
		}

		if (entity.hasComponent<components::BillboardComponent>()) {
			componentsJson["billboard"] = SceneSerialization::serializeBillboard(
				entity.getComponent<components::BillboardComponent>());
		}

		if (entity.hasComponent<components::AudioSource2DComponent>()) {
			componentsJson["audioSource2D"] = SceneSerialization::serializeAudioSource2D(
				entity.getComponent<components::AudioSource2DComponent>());
		}

		if (entity.hasComponent<components::AudioSource3DComponent>()) {
			componentsJson["audioSource3D"] = SceneSerialization::serializeAudioSource3D(
				entity.getComponent<components::AudioSource3DComponent>());
		}

		entityJson["components"] = componentsJson;

		// Serialize children recursively
		json childrenJson = json::array();
		for (auto& child : entity.getChildren()) {
			childrenJson.push_back(serializeEntityTree(child));
		}
		entityJson["children"] = childrenJson;

		return entityJson;
	}

	void PrefabSerialization::deserializeComponents(const json& componentsJson, scene::Entity& entity) {
		// Camera component (auto-adds billboard if not explicitly defined)
		if (componentsJson.contains("camera")) {
			auto& camera = entity.addOrReplaceComponent<components::CameraComponent>();
			SceneSerialization::deserializeCamera(componentsJson["camera"], camera);
			// Auto-add camera billboard if no billboard component is defined
			if (!componentsJson.contains("billboard")) {
				auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
				billboard.iconType = components::BillboardIconType::Camera;
			}
		}

		// IBL component
		if (componentsJson.contains("ibl")) {
			std::string iblFileName = SceneSerialization::deserializeIBL(componentsJson["ibl"]);
			if (!iblFileName.empty()) {
				entity.addOrReplaceComponent<components::IBLComponent>().fileName = iblFileName;
			}
		}

		if (componentsJson.contains("mesh")) {
			auto& meshComp = entity.addOrReplaceComponent<components::MeshComponent>();
			SceneSerialization::deserializeMesh(componentsJson["mesh"], meshComp);
		}

		if (componentsJson.contains("material")) {
			auto& matComp = entity.addOrReplaceComponent<components::MaterialComponent>();
			SceneSerialization::deserializeMaterial(componentsJson["material"], matComp);
		}

		if (componentsJson.contains("billboard")) {
			auto& billboardComp = entity.addOrReplaceComponent<components::BillboardComponent>();
			SceneSerialization::deserializeBillboard(componentsJson["billboard"], billboardComp);
		}

		if (componentsJson.contains("audioSource2D")) {
			auto& audioComp = entity.addOrReplaceComponent<components::AudioSource2DComponent>();
			SceneSerialization::deserializeAudioSource2D(componentsJson["audioSource2D"], audioComp);
			// Auto-add audio source billboard if no billboard component is defined
			if (!componentsJson.contains("billboard")) {
				auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
				billboard.iconType = components::BillboardIconType::AudioSource;
			}
		}

		if (componentsJson.contains("audioSource3D")) {
			auto& audioComp = entity.addOrReplaceComponent<components::AudioSource3DComponent>();
			SceneSerialization::deserializeAudioSource3D(componentsJson["audioSource3D"], audioComp);
			// Auto-add audio source billboard if no billboard component is defined
			if (!componentsJson.contains("billboard")) {
				auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
				billboard.iconType = components::BillboardIconType::AudioSource;
			}
		}
	}

	scene::Entity PrefabSerialization::deserializeEntityTree(
		const json& entityJson,
		scene::Entity& parent,
		scene::SceneGraphSystem& sceneGraph)
	{
		// Create new entity with NEW UUID (not restoring from file)
		std::string entityName = entityJson.value("name", "Prefab");
		scene::Entity entity(entityName);

		if (!entity.isValid()) {
			vfLogError("Failed to create entity '{}' during prefab load", entityName);
			return entity;
		}

		// Add to parent
		sceneGraph.addChild(parent, entity);

		// Deserialize transform
		if (entityJson.contains("transform")) {
			auto& transform = entity.getComponent<components::TransformComponent>();
			SceneSerialization::deserializeTransform(entityJson["transform"], transform);
		}

		// Deserialize optional components
		if (entityJson.contains("components")) {
			deserializeComponents(entityJson["components"], entity);
		}

		// Deserialize children recursively
		if (entityJson.contains("children") && entityJson["children"].is_array()) {
			for (const auto& childJson : entityJson["children"]) {
				if (!childJson.is_object()) {
					vfLogWarning("Skipping invalid child entry in prefab (not an object)");
					continue;
				}
				deserializeEntityTree(childJson, entity, sceneGraph);
			}
		}

		return entity;
	}

	bool PrefabSerialization::savePrefab(scene::Entity& entity, std::string_view filename) {
		try {
			json prefabJson;
			prefabJson["version"] = "1.0";
			prefabJson["prefab"]["name"] = entity.getName();
			prefabJson["prefab"]["entity"] = serializeEntityTree(entity);

			// Write to file with UTF-8 encoding, pretty-printed
			std::string filePath{ filename };
			std::ofstream file{ filePath };
			if (!file.is_open()) {
				vfLogError("Failed to open file for writing: {}", filename);
				return false;
			}

			file << prefabJson.dump(2); // Pretty print with 2-space indent
			file.close();

			vfLogInfo("Prefab saved successfully to: {}", filename);
			return true;
		}
		catch (const std::exception& e) {
			vfLogError("Failed to save prefab: {}", e.what());
			return false;
		}
	}

	std::optional<scene::Entity> PrefabSerialization::loadPrefab(
		std::string_view filename,
		scene::Entity& parent,
		scene::SceneGraphSystem& sceneGraph)
	{
		try {
			std::string filePath{ filename };
			std::ifstream file{ filePath };
			if (!file.is_open()) {
				vfLogError("Failed to open prefab file: {}", filename);
				return std::nullopt;
			}

			json prefabJson = json::parse(file);
			file.close();

			// Validate structure
			if (!prefabJson.is_object()) {
				vfLogError("Invalid prefab file: root is not a JSON object");
				return std::nullopt;
			}

			if (!prefabJson.contains("prefab") || !prefabJson["prefab"].is_object()) {
				vfLogError("Invalid prefab file: missing 'prefab' object");
				return std::nullopt;
			}

			if (!prefabJson["prefab"].contains("entity") || !prefabJson["prefab"]["entity"].is_object()) {
				vfLogError("Invalid prefab file: missing 'entity' object");
				return std::nullopt;
			}

			// Log version if present
			if (prefabJson.contains("version") && prefabJson["version"].is_string()) {
				std::string version = prefabJson["version"].get<std::string>();
				vfLogInfo("Loading prefab version: {}", version);
			}

			// Deserialize the entity tree with NEW UUIDs
			scene::Entity rootEntity = deserializeEntityTree(
				prefabJson["prefab"]["entity"],
				parent,
				sceneGraph
			);

			if (!rootEntity.isValid()) {
				vfLogError("Failed to instantiate prefab from: {}", filename);
				return std::nullopt;
			}

			vfLogInfo("Prefab loaded successfully from: {}", filename);
			return rootEntity;
		}
		catch (const json::parse_error& e) {
			vfLogError("JSON parse error while loading prefab: {}", e.what());
			return std::nullopt;
		}
		catch (const std::exception& e) {
			vfLogError("Failed to load prefab: {}", e.what());
			return std::nullopt;
		}
	}

	bool PrefabSerialization::validatePrefab(std::string_view filename) {
		try {
			std::string filePath{ filename };
			std::ifstream file{ filePath };
			if (!file.is_open()) {
				return false;
			}

			json prefabJson = json::parse(file);
			file.close();

			// Check required structure
			if (!prefabJson.is_object()) return false;
			if (!prefabJson.contains("prefab")) return false;
			if (!prefabJson["prefab"].contains("entity")) return false;

			return true;
		}
		catch (...) {
			return false;
		}
	}

}
