#include "SceneSerialization.hpp"
#include "JsonConverters.hpp"
#include "../scene/SceneGraphSystem.hpp"
#include "../scene/Entity.hpp"
#include "../components/Components.hpp"
#include "../print/EditorLogger.hpp"
#include <nlohmann/json.hpp>
#include <fstream>

namespace serialization {

	using json = nlohmann::json;

	// Forward declaration of recursive serialization helper
	static json serializeEntity(scene::Entity& entity);

	// Serialize transform component
	static json serializeTransform(const components::TransformComponent& transform) {
		json j;
		j["position"] = json::array({ transform.position.x, transform.position.y, transform.position.z });
		j["rotation"] = json::array({ transform.rotation.x, transform.rotation.y, transform.rotation.z });
		j["scale"] = json::array({ transform.scale.x, transform.scale.y, transform.scale.z });
		return j;
	}

	// Serialize camera component
	static json serializeCamera(const components::CameraComponent& camera) {
		json j;
		j["fieldOfView"] = camera.fieldOfView;
		j["nearPlane"] = camera.nearPlane;
		j["farPlane"] = camera.farPlane;
		j["aspectRatio"] = camera.aspectRatio;
		j["isPerspective"] = camera.isPerspective;
		j["orthoSize"] = camera.orthoSize;
		return j;
	}

	// Serialize IBL component
	static json serializeIBL(const components::IBLComponent& ibl) {
		json j;
		// Remove any embedded null terminators from the string
		std::string cleanFileName = ibl.fileName;
		if (auto pos = cleanFileName.find('\0'); pos != std::string::npos) {
			cleanFileName.resize(pos);
		}
		j["fileName"] = cleanFileName;
		return j;
	}

	// Recursively serialize an entity and its children
	static json serializeEntity(scene::Entity& entity) {
		json entityJson;

		// Use UUID for persistent identification
		entityJson["uuid"] = entity.getUUID().getValue();
		entityJson["name"] = entity.getName();

		// Transform (always present per Entity constructor)
		if (entity.hasComponent<components::TransformComponent>()) {
			entityJson["transform"] = serializeTransform(entity.getComponent<components::TransformComponent>());
		}

		// Optional components
		json componentsJson = json::object();

		if (entity.hasComponent<components::CameraComponent>()) {
			componentsJson["camera"] = serializeCamera(entity.getComponent<components::CameraComponent>());
		}

		if (entity.hasComponent<components::IBLComponent>()) {
			componentsJson["ibl"] = serializeIBL(entity.getComponent<components::IBLComponent>());
		}

		entityJson["components"] = componentsJson;

		// Serialize children recursively
		json childrenJson = json::array();
		for (auto& child : entity.getChildren()) {
			childrenJson.push_back(serializeEntity(child));
		}
		entityJson["children"] = childrenJson;

		return entityJson;
	}

	// ============================================
	// DESERIALIZATION HELPERS
	// ============================================

	// Deserialize transform component from JSON
	static void deserializeTransform(const json& j, components::TransformComponent& transform) {
		if (j.contains("position")) {
			const auto& pos = j["position"];
			transform.position = glm::vec3(pos[0].get<float>(), pos[1].get<float>(), pos[2].get<float>());
		}
		if (j.contains("rotation")) {
			const auto& rot = j["rotation"];
			transform.rotation = glm::vec3(rot[0].get<float>(), rot[1].get<float>(), rot[2].get<float>());
		}
		if (j.contains("scale")) {
			const auto& scl = j["scale"];
			transform.scale = glm::vec3(scl[0].get<float>(), scl[1].get<float>(), scl[2].get<float>());
		}
		transform.isDirty = true;
	}

	// Deserialize camera component from JSON
	static void deserializeCamera(const json& j, components::CameraComponent& camera) {
		if (j.contains("fieldOfView")) camera.fieldOfView = j["fieldOfView"].get<float>();
		if (j.contains("nearPlane")) camera.nearPlane = j["nearPlane"].get<float>();
		if (j.contains("farPlane")) camera.farPlane = j["farPlane"].get<float>();
		if (j.contains("aspectRatio")) camera.aspectRatio = j["aspectRatio"].get<float>();
		if (j.contains("isPerspective")) camera.isPerspective = j["isPerspective"].get<bool>();
		if (j.contains("orthoSize")) camera.orthoSize = j["orthoSize"].get<float>();
		camera.updateProjectionMatrix();
	}

	// Deserialize IBL component from JSON - returns filename
	static std::string deserializeIBL(const json& j) {
		if (j.contains("fileName")) {
			return j["fileName"].get<std::string>();
		}
		return "";
	}

	// Forward declaration for recursive deserialization
	static void deserializeEntity(const json& entityJson, scene::Entity& entity, scene::SceneGraphSystem& sceneGraph, bool isRoot);

	// Recursively deserialize children entities
	static void deserializeChildren(const json& childrenJson, scene::Entity& parent, scene::SceneGraphSystem& sceneGraph) {
		for (const auto& childJson : childrenJson) {
			// Get name for the child
			std::string childName = childJson.value("name", "Unnamed");

			// Create new child entity
			scene::Entity child(childName);

			// Restore UUID if present
			if (childJson.contains("uuid")) {
				uint64_t uuidValue = childJson["uuid"].get<uint64_t>();
				child.addOrReplaceComponent<components::UUIDComponent>(uuidValue);
			}

			// Add to parent
			sceneGraph.addChild(parent, child);

			// Deserialize the child's data (transform, components, children)
			deserializeEntity(childJson, child, sceneGraph, false);
		}
	}

	// Main entity deserialization (handles both root and children)
	static void deserializeEntity(const json& entityJson, scene::Entity& entity, scene::SceneGraphSystem& sceneGraph, bool isRoot) {
		// Set name
		if (entityJson.contains("name")) {
			entity.setName(entityJson["name"].get<std::string>());
		}

		// Restore UUID for root
		if (isRoot && entityJson.contains("uuid")) {
			uint64_t uuidValue = entityJson["uuid"].get<uint64_t>();
			entity.addOrReplaceComponent<components::UUIDComponent>(uuidValue);
		}

		// Deserialize transform
		if (entityJson.contains("transform")) {
			auto& transform = entity.getComponent<components::TransformComponent>();
			deserializeTransform(entityJson["transform"], transform);
		}

		// Deserialize optional components
		if (entityJson.contains("components")) {
			const auto& componentsJson = entityJson["components"];

			// Camera component
			if (componentsJson.contains("camera")) {
				auto& camera = entity.addOrReplaceComponent<components::CameraComponent>();
				deserializeCamera(componentsJson["camera"], camera);
			}

			// IBL component
			if (componentsJson.contains("ibl")) {
				std::string iblFileName = deserializeIBL(componentsJson["ibl"]);
				if (!iblFileName.empty()) {
					entity.addOrReplaceComponent<components::IBLComponent>().fileName = iblFileName;
				}
			}
		}

		// Deserialize children recursively
		if (entityJson.contains("children") && entityJson["children"].is_array()) {
			deserializeChildren(entityJson["children"], entity, sceneGraph);
		}
	}

	scene::SceneGraphSystem SceneSerialization::loadScene(std::string_view filename)
	{
		scene::SceneGraphSystem sceneGraph;
		loadSceneInto(filename, sceneGraph);
		return sceneGraph;
	}

	bool SceneSerialization::loadSceneInto(std::string_view filename, scene::SceneGraphSystem& sceneGraph)
	{
		try {
			// Open and parse file
			std::string filePath{ filename };
			std::ifstream file{ filePath };
			if (!file.is_open()) {
				vfLogError("Failed to open file for reading: {}", filename);
				return false;
			}

			json sceneJson = json::parse(file);
			file.close();

			// Validate and log version
			if (sceneJson.contains("version")) {
				std::string version = sceneJson["version"].get<std::string>();
				vfLogInfo("Loading scene version: {}", version);
			}

			// Clear existing scene first
			sceneGraph.clearScene();

			// Deserialize root entity
			if (sceneJson.contains("root")) {
				scene::Entity& root = sceneGraph.GetRoot();
				deserializeEntity(sceneJson["root"], root, sceneGraph, true);
			}

			vfLogInfo("Scene loaded successfully from: {}", filename);
			return true;
		}
		catch (const json::parse_error& e) {
			vfLogError("JSON parse error while loading scene: {}", e.what());
			return false;
		}
		catch (const std::exception& e) {
			vfLogError("Failed to load scene: {}", e.what());
			return false;
		}
	}

	bool SceneSerialization::saveScene(scene::SceneGraphSystem& sceneGraph, std::string_view filename)
	{
		try {
			json sceneJson;
			sceneJson["version"] = "1.0";

			scene::Entity& root = sceneGraph.GetRoot();

			sceneJson["root"] = serializeEntity(root);

			// Write to file with UTF-8 encoding, no BOM, pretty-printed
			std::string filePath{filename};
			std::ofstream file{filePath};
			if (!file.is_open()) {
				vfLogError("Failed to open file for writing: {}", filename);
				return false;
			}

			file << sceneJson.dump(2); // Pretty print with 2-space indent
			file.close();

			vfLogInfo("Scene saved successfully to: {}", filename);
			return true;
		}
		catch (const std::exception& e) {
			vfLogError("Failed to save scene: {}", e.what());
			return false;
		}
	}

}
