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
		j["showBoundingBox"] = mesh.showBoundingBox;
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

		if (entity.hasComponent<components::MaterialComponent>()) {
			componentsJson["material"] = serializeMaterial(entity.getComponent<components::MaterialComponent>());
		}

		if (entity.hasComponent<components::BillboardComponent>()) {
			componentsJson["billboard"] = serializeBillboard(entity.getComponent<components::BillboardComponent>());
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

	void SceneSerialization::deserializeMesh(const json& j, components::MeshComponent& mesh) {
		if (auto it = j.find("meshPath"); it != j.end() && it->is_string()) {
			mesh.meshPath = it->get<std::string>();
		}
		if (auto it = j.find("showBoundingBox"); it != j.end() && it->is_boolean()) {
			mesh.showBoundingBox = it->get<bool>();
		}
	}

	json SceneSerialization::serializeMaterial(const components::MaterialComponent& material) {
		json j;

		// Clean and save default material path
		std::string cleanDefaultPath = material.defaultMaterial;
		if (auto pos = cleanDefaultPath.find('\0'); pos != std::string::npos) {
			cleanDefaultPath.resize(pos);
		}
		j["defaultMaterial"] = cleanDefaultPath;

		// Serialize submesh materials map
		json subMeshMaterialsJson = json::object();
		for (const auto& [submeshName, matPath] : material.subMeshMaterials) {
			std::string cleanMatPath = matPath;
			if (auto pos = cleanMatPath.find('\0'); pos != std::string::npos) {
				cleanMatPath.resize(pos);
			}
			subMeshMaterialsJson[submeshName] = cleanMatPath;
		}
		j["subMeshMaterials"] = subMeshMaterialsJson;

		// Serialize parameter overrides
		json paramOverridesJson = json::object();
		for (const auto& [paramName, value] : material.parameterOverrides) {
			paramOverridesJson[paramName] = value;
		}
		j["parameterOverrides"] = paramOverridesJson;

		return j;
	}

	void SceneSerialization::deserializeMaterial(const json& j, components::MaterialComponent& material) {
		if (auto it = j.find("defaultMaterial"); it != j.end() && it->is_string()) {
			material.defaultMaterial = it->get<std::string>();
		}

		if (auto it = j.find("subMeshMaterials"); it != j.end() && it->is_object()) {
			material.subMeshMaterials.clear();
			for (auto& [key, value] : it->items()) {
				if (value.is_string()) {
					material.subMeshMaterials[key] = value.get<std::string>();
				}
			}
		}

		if (auto it = j.find("parameterOverrides"); it != j.end() && it->is_object()) {
			material.parameterOverrides.clear();
			for (auto& [key, value] : it->items()) {
				if (value.is_number()) {
					material.parameterOverrides[key] = value.get<float>();
				}
			}
		}
	}

	// Billboard enum conversion helpers
	static std::string billboardSizeModeToString(components::BillboardSizeMode mode) {
		switch (mode) {
		case components::BillboardSizeMode::WorldSpace: return "worldSpace";
		default: return "screenSpace";
		}
	}

	static components::BillboardSizeMode stringToBillboardSizeMode(const std::string& str) {
		if (str == "worldSpace") return components::BillboardSizeMode::WorldSpace;
		return components::BillboardSizeMode::ScreenSpace;
	}

	static std::string billboardIconTypeToString(components::BillboardIconType type) {
		switch (type) {
		case components::BillboardIconType::Light: return "light";
		case components::BillboardIconType::Camera: return "camera";
		case components::BillboardIconType::AudioSource: return "audioSource";
		case components::BillboardIconType::Particle: return "particle";
		default: return "custom";
		}
	}

	static components::BillboardIconType stringToBillboardIconType(const std::string& str) {
		if (str == "light") return components::BillboardIconType::Light;
		if (str == "camera") return components::BillboardIconType::Camera;
		if (str == "audioSource") return components::BillboardIconType::AudioSource;
		if (str == "particle") return components::BillboardIconType::Particle;
		return components::BillboardIconType::Custom;
	}

	json SceneSerialization::serializeBillboard(const components::BillboardComponent& billboard) {
		json j;
		j["iconType"] = billboardIconTypeToString(billboard.iconType);
		j["atlasIndex"] = billboard.atlasIndex;
		j["sizeMode"] = billboardSizeModeToString(billboard.sizeMode);
		j["size"] = json::array({ billboard.size.x, billboard.size.y });
		j["colorTint"] = json::array({ billboard.colorTint.r, billboard.colorTint.g, billboard.colorTint.b, billboard.colorTint.a });
		j["editorOnly"] = billboard.editorOnly;
		j["selectable"] = billboard.selectable;
		return j;
	}

	void SceneSerialization::deserializeBillboard(const json& j, components::BillboardComponent& billboard) {
		billboard.iconType = stringToBillboardIconType(j.value("iconType", "custom"));
		billboard.atlasIndex = j.value("atlasIndex", 0u);
		billboard.sizeMode = stringToBillboardSizeMode(j.value("sizeMode", "screenSpace"));
		if (j.contains("size") && j["size"].is_array() && j["size"].size() >= 2) {
			billboard.size = glm::vec2(j["size"][0].get<float>(), j["size"][1].get<float>());
		}
		if (j.contains("colorTint") && j["colorTint"].is_array() && j["colorTint"].size() >= 4) {
			billboard.colorTint = glm::vec4(
				j["colorTint"][0].get<float>(), j["colorTint"][1].get<float>(),
				j["colorTint"][2].get<float>(), j["colorTint"][3].get<float>()
			);
		}
		billboard.editorOnly = j.value("editorOnly", true);
		billboard.selectable = j.value("selectable", true);
	}

	void SceneSerialization::deserializeChildren(const json& childrenJson, scene::Entity& parent, scene::SceneGraphSystem& sceneGraph,
											   SceneLoadProgressCallback progressCallback, size_t& entitiesLoaded, size_t totalEntities) {
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
			deserializeEntity(childJson, child, sceneGraph, false, progressCallback, entitiesLoaded, totalEntities);
		}
	}

	// Main entity deserialization (handles both root and children)
	void SceneSerialization::deserializeEntity(const json& entityJson, scene::Entity& entity, scene::SceneGraphSystem& sceneGraph, bool isRoot,
											   SceneLoadProgressCallback progressCallback, size_t& entitiesLoaded, size_t totalEntities) {
		// Set name
		std::string entityName = "Unnamed";
		if (entityJson.contains("name")) {
			entityName = entityJson["name"].get<std::string>();
			entity.setName(entityName);
		}

		// Report progress
		if (progressCallback) {
			progressCallback(entityName, entitiesLoaded, totalEntities);
		}
		++entitiesLoaded;

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

			// Camera component (auto-adds billboard if not explicitly defined)
			if (componentsJson.contains("camera")) {
				auto& camera = entity.addOrReplaceComponent<components::CameraComponent>();
				deserializeCamera(componentsJson["camera"], camera);
				// Auto-add camera billboard if no billboard component is defined
				if (!componentsJson.contains("billboard")) {
					auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
					billboard.iconType = components::BillboardIconType::Camera;
				}
			}

			// IBL component
			if (componentsJson.contains("ibl")) {
				std::string iblFileName = deserializeIBL(componentsJson["ibl"]);
				if (!iblFileName.empty()) {
					entity.addOrReplaceComponent<components::IBLComponent>().fileName = iblFileName;
				}
			}

			if (componentsJson.contains("mesh")) {
				auto& meshComp = entity.addOrReplaceComponent<components::MeshComponent>();
				deserializeMesh(componentsJson["mesh"], meshComp);
			}

			if (componentsJson.contains("material")) {
				auto& matComp = entity.addOrReplaceComponent<components::MaterialComponent>();
				deserializeMaterial(componentsJson["material"], matComp);
			}

			if (componentsJson.contains("billboard")) {
				auto& billboardComp = entity.addOrReplaceComponent<components::BillboardComponent>();
				deserializeBillboard(componentsJson["billboard"], billboardComp);
			}
		}

		// Deserialize children recursively
		if (entityJson.contains("children") && entityJson["children"].is_array()) {
			deserializeChildren(entityJson["children"], entity, sceneGraph, progressCallback, entitiesLoaded, totalEntities);
		}
	}

	scene::SceneGraphSystem SceneSerialization::loadScene(std::string_view filename)
	{
		scene::SceneGraphSystem sceneGraph;
		loadSceneInto(filename, sceneGraph);
		return sceneGraph;
	}

	size_t SceneSerialization::countEntities(const json& entityJson) {
		size_t count = 1; // Count this entity
		if (entityJson.contains("children") && entityJson["children"].is_array()) {
			for (const auto& child : entityJson["children"]) {
				count += countEntities(child);
			}
		}
		return count;
	}

	bool SceneSerialization::loadSceneInto(std::string_view filename, scene::SceneGraphSystem& sceneGraph,
										   SceneLoadProgressCallback progressCallback)
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
			// Count total entities for progress tracking
			size_t totalEntities = countEntities(sceneJson["root"]);
			size_t entitiesLoaded = 0;

			sceneGraph.clearScene();

			// Deserialize root entity
			scene::Entity& root = sceneGraph.GetRoot();
			deserializeEntity(sceneJson["root"], root, sceneGraph, true, progressCallback, entitiesLoaded, totalEntities);

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
