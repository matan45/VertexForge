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

	scene::SceneGraphSystem SceneSerialization::loadScene(std::string_view filename)
	{
		// TODO: Implement in VK-39
		return scene::SceneGraphSystem();
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
