#include "SceneSerialization.hpp"
#include "JsonConverters.hpp"
#include "../scene/SceneGraphSystem.hpp"
#include "../components/Components.hpp"
#include "../print/EditorLogger.hpp"
#include <fstream>

namespace serialization {
	
	// Serialize transform component
	json SceneSerialization::serializeTransform(const components::TransformComponent& transform) {
		json j;
		j["position"] = json::array({ transform.position.x, transform.position.y, transform.position.z });
		j["rotation"] = json::array({ transform.rotation.x, transform.rotation.y, transform.rotation.z });
		j["scale"] = json::array({ transform.scale.x, transform.scale.y, transform.scale.z });
		return j;
	}
	
	json SceneSerialization::serializeCamera(const components::CameraComponent& camera) {
		json j;
		j["fieldOfView"] = camera.fieldOfView;
		j["nearPlane"] = camera.nearPlane;
		j["farPlane"] = camera.farPlane;
		j["aspectRatio"] = camera.aspectRatio;
		j["isPerspective"] = camera.isPerspective;
		j["orthoSize"] = camera.orthoSize;
		return j;
	}
	
	json SceneSerialization::serializeIBL(const components::IBLComponent& ibl) {
		json j;
		// Remove any embedded null terminators from the string
		std::string cleanFileName = ibl.fileName;
		if (auto pos = cleanFileName.find('\0'); pos != std::string::npos) {
			cleanFileName.resize(pos);
		}
		j["fileName"] = cleanFileName;
		return j;
	}

	json SceneSerialization::serializeMesh(const components::MeshComponent& mesh) {
		json j;
		// Remove any embedded null terminators from the string
		std::string cleanPath = mesh.meshPath;
		if (auto pos = cleanPath.find('\0'); pos != std::string::npos) {
			cleanPath.resize(pos);
		}
		j["meshPath"] = cleanPath;
		return j;
	}

	// Recursively serialize an entity and its children
	json SceneSerialization::serializeEntity(scene::Entity& entity) {
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

		if (entity.hasComponent<components::MeshComponent>()) {
			componentsJson["mesh"] = serializeMesh(entity.getComponent<components::MeshComponent>());
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
	
	void SceneSerialization::deserializeTransform(const json& j, components::TransformComponent& transform) {
		if (auto it = j.find("position"); it != j.end() && it->is_array() && it->size() >= 3) {
			transform.position = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
		}
		if (auto it = j.find("rotation"); it != j.end() && it->is_array() && it->size() >= 3) {
			transform.rotation = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
		}
		if (auto it = j.find("scale"); it != j.end() && it->is_array() && it->size() >= 3) {
			transform.scale = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
		}
		transform.isDirty = true;
	}
	
	void SceneSerialization::deserializeCamera(const json& j, components::CameraComponent& camera) {
		if (auto it = j.find("fieldOfView"); it != j.end() && it->is_number())
			camera.fieldOfView = it->get<float>();
		if (auto it = j.find("nearPlane"); it != j.end() && it->is_number())
			camera.nearPlane = it->get<float>();
		if (auto it = j.find("farPlane"); it != j.end() && it->is_number())
			camera.farPlane = it->get<float>();
		if (auto it = j.find("aspectRatio"); it != j.end() && it->is_number())
			camera.aspectRatio = it->get<float>();
		if (auto it = j.find("isPerspective"); it != j.end() && it->is_boolean())
			camera.isPerspective = it->get<bool>();
		if (auto it = j.find("orthoSize"); it != j.end() && it->is_number())
			camera.orthoSize = it->get<float>();
		camera.updateProjectionMatrix();
	}
	
	std::string SceneSerialization::deserializeIBL(const json& j) {
		if (auto it = j.find("fileName"); it != j.end() && it->is_string()) {
			return it->get<std::string>();
		}
		return "";
	}

	std::string SceneSerialization::deserializeMesh(const json& j) {
		if (auto it = j.find("meshPath"); it != j.end() && it->is_string()) {
			return it->get<std::string>();
		}
		return "";
	}

	void SceneSerialization::deserializeChildren(const json& childrenJson, scene::Entity& parent, scene::SceneGraphSystem& sceneGraph) {
		for (const auto& childJson : childrenJson) {
			if (!childJson.is_object()) {
				vfLogWarning("Skipping invalid child entry in scene file (not an object)");
				continue;
			}
			
			std::string childName = childJson.value("name", "Unnamed");
			
			scene::Entity child(childName);
			
			if (!child.isValid()) {
				vfLogError("Failed to create child entity '{}' during scene load", childName);
				continue;
			}
			
			if (childJson.contains("uuid") && childJson["uuid"].is_number_unsigned()) {
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
	void SceneSerialization::deserializeEntity(const json& entityJson, scene::Entity& entity, scene::SceneGraphSystem& sceneGraph, bool isRoot) {
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

			// Mesh component
			if (componentsJson.contains("mesh")) {
				std::string meshPath = deserializeMesh(componentsJson["mesh"]);
				if (!meshPath.empty()) {
					entity.addOrReplaceComponent<components::MeshComponent>().meshPath = meshPath;
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
		json sceneJson;

		// Phase 1: Validate file and parse JSON before modifying scene
		try {
			std::string filePath{ filename };
			std::ifstream file{ filePath };
			if (!file.is_open()) {
				vfLogError("Failed to open file for reading: {}", filename);
				return false;
			}

			sceneJson = json::parse(file);
			file.close();

			// Validate required structure before clearing scene
			if (!sceneJson.is_object()) {
				vfLogError("Invalid scene file: root is not a JSON object");
				return false;
			}

			if (!sceneJson.contains("root") || !sceneJson["root"].is_object()) {
				vfLogError("Invalid scene file: missing or invalid 'root' object");
				return false;
			}

			// Log version if present
			if (sceneJson.contains("version") && sceneJson["version"].is_string()) {
				std::string version = sceneJson["version"].get<std::string>();
				vfLogInfo("Loading scene version: {}", version);
			}
		}
		catch (const json::parse_error& e) {
			vfLogError("JSON parse error while loading scene: {}", e.what());
			return false;
		}
		catch (const std::exception& e) {
			vfLogError("Failed to read scene file: {}", e.what());
			return false;
		}

		// Phase 2: File validated successfully - now safe to clear and load
		try {
			sceneGraph.clearScene();

			// Deserialize root entity
			scene::Entity& root = sceneGraph.GetRoot();
			deserializeEntity(sceneJson["root"], root, sceneGraph, true);

			vfLogInfo("Scene loaded successfully from: {}", filename);
			return true;
		}
		catch (const std::exception& e) {
			vfLogError("Failed to deserialize scene: {}", e.what());
			// Scene is in partial state - clear to avoid corruption
			sceneGraph.clearScene();
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
