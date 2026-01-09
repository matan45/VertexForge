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
		j["cameraId"] = camera.cameraId;
		j["fieldOfView"] = camera.fieldOfView;
		j["nearPlane"] = camera.nearPlane;
		j["farPlane"] = camera.farPlane;
		j["aspectRatio"] = camera.aspectRatio;
		j["isPerspective"] = camera.isPerspective;
		j["isPrimary"] = camera.isPrimary;
		j["showFrustum"] = camera.showFrustum;
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

		// Active state
		if (entity.hasComponent<components::NameComponent>()) {
			entityJson["isActive"] = entity.getComponent<components::NameComponent>().isActive;
		}

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

		if (entity.hasComponent<components::AudioSource2DComponent>()) {
			componentsJson["audioSource2D"] = serializeAudioSource2D(entity.getComponent<components::AudioSource2DComponent>());
		}

		if (entity.hasComponent<components::AudioSource3DComponent>()) {
			componentsJson["audioSource3D"] = serializeAudioSource3D(entity.getComponent<components::AudioSource3DComponent>());
		}

		if (entity.hasComponent<components::ScriptComponent>()) {
			componentsJson["script"] = serializeScript(entity.getComponent<components::ScriptComponent>());
		}

		if (entity.hasComponent<components::ColliderComponent>()) {
			componentsJson["collider"] = serializeCollider(entity.getComponent<components::ColliderComponent>());
		}

		if (entity.hasComponent<components::RigidBodyComponent>()) {
			componentsJson["rigidBody"] = serializeRigidBody(entity.getComponent<components::RigidBodyComponent>());
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
		// Restore cameraId if present (for snapshot restore)
		if (auto it = j.find("cameraId"); it != j.end() && it->is_number_unsigned())
			camera.cameraId = it->get<uint32_t>();
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
		if (auto it = j.find("isPrimary"); it != j.end() && it->is_boolean())
			camera.isPrimary = it->get<bool>();
		if (auto it = j.find("showFrustum"); it != j.end() && it->is_boolean())
			camera.showFrustum = it->get<bool>();
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

	json SceneSerialization::serializeAudioSource2D(const components::AudioSource2DComponent& audioSource) {
		json j;
		// Clean the audio file path
		std::string cleanPath = audioSource.audioFilePath;
		if (auto pos = cleanPath.find('\0'); pos != std::string::npos) {
			cleanPath.resize(pos);
		}
		j["audioFilePath"] = cleanPath;
		j["volume"] = audioSource.volume;
		j["pitch"] = audioSource.pitch;
		j["loop"] = audioSource.loop;
		// Note: activeHandle and isPlaying are runtime state, not serialized
		return j;
	}

	void SceneSerialization::deserializeAudioSource2D(const json& j, components::AudioSource2DComponent& audioSource) {
		if (auto it = j.find("audioFilePath"); it != j.end() && it->is_string()) {
			audioSource.audioFilePath = it->get<std::string>();
		}
		if (auto it = j.find("volume"); it != j.end() && it->is_number()) {
			audioSource.volume = it->get<float>();
		}
		if (auto it = j.find("pitch"); it != j.end() && it->is_number()) {
			audioSource.pitch = it->get<float>();
		}
		if (auto it = j.find("loop"); it != j.end() && it->is_boolean()) {
			audioSource.loop = it->get<bool>();
		}
		// Reset runtime state
		audioSource.activeHandle = 0;
		audioSource.isPlaying = false;
	}

	json SceneSerialization::serializeAudioSource3D(const components::AudioSource3DComponent& audioSource) {
		json j;
		// Clean the audio file path
		std::string cleanPath = audioSource.audioFilePath;
		if (auto pos = cleanPath.find('\0'); pos != std::string::npos) {
			cleanPath.resize(pos);
		}
		j["audioFilePath"] = cleanPath;
		j["volume"] = audioSource.volume;
		j["pitch"] = audioSource.pitch;
		j["loop"] = audioSource.loop;
		j["minDistance"] = audioSource.minDistance;
		j["maxDistance"] = audioSource.maxDistance;
		j["showDebugSpheres"] = audioSource.showDebugSpheres;
		// Note: activeHandle and isPlaying are runtime state, not serialized
		return j;
	}

	void SceneSerialization::deserializeAudioSource3D(const json& j, components::AudioSource3DComponent& audioSource) {
		if (auto it = j.find("audioFilePath"); it != j.end() && it->is_string()) {
			audioSource.audioFilePath = it->get<std::string>();
		}
		if (auto it = j.find("volume"); it != j.end() && it->is_number()) {
			audioSource.volume = it->get<float>();
		}
		if (auto it = j.find("pitch"); it != j.end() && it->is_number()) {
			audioSource.pitch = it->get<float>();
		}
		if (auto it = j.find("loop"); it != j.end() && it->is_boolean()) {
			audioSource.loop = it->get<bool>();
		}
		if (auto it = j.find("minDistance"); it != j.end() && it->is_number()) {
			audioSource.minDistance = it->get<float>();
		}
		if (auto it = j.find("maxDistance"); it != j.end() && it->is_number()) {
			audioSource.maxDistance = it->get<float>();
		}
		if (auto it = j.find("showDebugSpheres"); it != j.end() && it->is_boolean()) {
			audioSource.showDebugSpheres = it->get<bool>();
		}
		// Reset runtime state
		audioSource.activeHandle = 0;
		audioSource.isPlaying = false;
	}

	json SceneSerialization::serializeScript(const components::ScriptComponent& script) {
		json j;
		json scriptsArray = json::array();
		for (const auto& entry : script.scripts) {
			json entryJson;
			// Clean the script path
			std::string cleanPath = entry.scriptPath;
			if (auto pos = cleanPath.find('\0'); pos != std::string::npos) {
				cleanPath.resize(pos);
			}
			entryJson["scriptPath"] = cleanPath;
			entryJson["enabled"] = entry.enabled;
			// Note: started, instanceId, hasOnStart, hasOnUpdate, hasOnDestroy are runtime state
			scriptsArray.push_back(entryJson);
		}
		j["scripts"] = scriptsArray;
		return j;
	}

	void SceneSerialization::deserializeScript(const json& j, components::ScriptComponent& script) {
		script.scripts.clear();
		if (j.contains("scripts") && j["scripts"].is_array()) {
			for (const auto& entryJson : j["scripts"]) {
				components::ScriptEntry entry;
				if (entryJson.contains("scriptPath") && entryJson["scriptPath"].is_string()) {
					entry.scriptPath = entryJson["scriptPath"].get<std::string>();
				}
				if (entryJson.contains("enabled") && entryJson["enabled"].is_boolean()) {
					entry.enabled = entryJson["enabled"].get<bool>();
				}
				// Reset runtime state
				entry.started = false;
				entry.instanceId = 0;
				script.scripts.push_back(entry);
			}
		}
	}

	// Physics enum conversion helpers
	static std::string rigidBodyTypeToString(components::RigidBodyType type) {
		switch (type) {
		case components::RigidBodyType::Static: return "static";
		case components::RigidBodyType::Kinematic: return "kinematic";
		default: return "dynamic";
		}
	}

	static components::RigidBodyType stringToRigidBodyType(const std::string& str) {
		if (str == "static") return components::RigidBodyType::Static;
		if (str == "kinematic") return components::RigidBodyType::Kinematic;
		return components::RigidBodyType::Dynamic;
	}

	static std::string colliderShapeToString(components::ColliderShape shape) {
		switch (shape) {
		case components::ColliderShape::Sphere: return "sphere";
		case components::ColliderShape::Capsule: return "capsule";
		case components::ColliderShape::ConvexMesh: return "convexMesh";
		case components::ColliderShape::TriangleMesh: return "triangleMesh";
		default: return "box";
		}
	}

	static components::ColliderShape stringToColliderShape(const std::string& str) {
		if (str == "sphere") return components::ColliderShape::Sphere;
		if (str == "capsule") return components::ColliderShape::Capsule;
		if (str == "convexMesh") return components::ColliderShape::ConvexMesh;
		if (str == "triangleMesh") return components::ColliderShape::TriangleMesh;
		return components::ColliderShape::Box;
	}

	json SceneSerialization::serializeCollider(const components::ColliderComponent& collider) {
		json j;
		j["shape"] = colliderShapeToString(collider.shape);
		j["size"] = json::array({ collider.size.x, collider.size.y, collider.size.z });
		j["height"] = collider.height;
		j["offset"] = json::array({ collider.offset.x, collider.offset.y, collider.offset.z });
		// Clean mesh path
		std::string cleanPath = collider.meshPath;
		if (auto pos = cleanPath.find('\0'); pos != std::string::npos) {
			cleanPath.resize(pos);
		}
		j["meshPath"] = cleanPath;
		j["isTrigger"] = collider.isTrigger;
		j["collisionLayer"] = collider.collisionLayer;
		j["friction"] = collider.friction;
		j["restitution"] = collider.restitution;
		return j;
	}

	void SceneSerialization::deserializeCollider(const json& j, components::ColliderComponent& collider) {
		if (auto it = j.find("shape"); it != j.end() && it->is_string()) {
			collider.shape = stringToColliderShape(it->get<std::string>());
		}
		if (auto it = j.find("size"); it != j.end() && it->is_array() && it->size() >= 3) {
			collider.size = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
		}
		if (auto it = j.find("height"); it != j.end() && it->is_number()) {
			collider.height = it->get<float>();
		}
		if (auto it = j.find("offset"); it != j.end() && it->is_array() && it->size() >= 3) {
			collider.offset = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
		}
		if (auto it = j.find("meshPath"); it != j.end() && it->is_string()) {
			collider.meshPath = it->get<std::string>();
		}
		if (auto it = j.find("isTrigger"); it != j.end() && it->is_boolean()) {
			collider.isTrigger = it->get<bool>();
		}
		if (auto it = j.find("collisionLayer"); it != j.end() && it->is_number_unsigned()) {
			collider.collisionLayer = it->get<uint8_t>();
		}
		if (auto it = j.find("friction"); it != j.end() && it->is_number()) {
			collider.friction = it->get<float>();
		}
		if (auto it = j.find("restitution"); it != j.end() && it->is_number()) {
			collider.restitution = it->get<float>();
		}
	}

	json SceneSerialization::serializeRigidBody(const components::RigidBodyComponent& rigidBody) {
		json j;
		j["type"] = rigidBodyTypeToString(rigidBody.type);
		j["mass"] = rigidBody.mass;
		j["linearDamping"] = rigidBody.linearDamping;
		j["angularDamping"] = rigidBody.angularDamping;
		j["freezePositionX"] = rigidBody.freezePositionX;
		j["freezePositionY"] = rigidBody.freezePositionY;
		j["freezePositionZ"] = rigidBody.freezePositionZ;
		j["freezeRotationX"] = rigidBody.freezeRotationX;
		j["freezeRotationY"] = rigidBody.freezeRotationY;
		j["freezeRotationZ"] = rigidBody.freezeRotationZ;
		return j;
	}

	void SceneSerialization::deserializeRigidBody(const json& j, components::RigidBodyComponent& rigidBody) {
		if (auto it = j.find("type"); it != j.end() && it->is_string()) {
			rigidBody.type = stringToRigidBodyType(it->get<std::string>());
		}
		if (auto it = j.find("mass"); it != j.end() && it->is_number()) {
			rigidBody.mass = it->get<float>();
		}
		if (auto it = j.find("linearDamping"); it != j.end() && it->is_number()) {
			rigidBody.linearDamping = it->get<float>();
		}
		if (auto it = j.find("angularDamping"); it != j.end() && it->is_number()) {
			rigidBody.angularDamping = it->get<float>();
		}
		if (auto it = j.find("freezePositionX"); it != j.end() && it->is_boolean()) {
			rigidBody.freezePositionX = it->get<bool>();
		}
		if (auto it = j.find("freezePositionY"); it != j.end() && it->is_boolean()) {
			rigidBody.freezePositionY = it->get<bool>();
		}
		if (auto it = j.find("freezePositionZ"); it != j.end() && it->is_boolean()) {
			rigidBody.freezePositionZ = it->get<bool>();
		}
		if (auto it = j.find("freezeRotationX"); it != j.end() && it->is_boolean()) {
			rigidBody.freezeRotationX = it->get<bool>();
		}
		if (auto it = j.find("freezeRotationY"); it != j.end() && it->is_boolean()) {
			rigidBody.freezeRotationY = it->get<bool>();
		}
		if (auto it = j.find("freezeRotationZ"); it != j.end() && it->is_boolean()) {
			rigidBody.freezeRotationZ = it->get<bool>();
		}
	}

	json SceneSerialization::serializePhysicsSettings(const types::PhysicsSettings& settings) {
		json j;

		// Gravity
		j["gravity"] = json::array({settings.gravity.x, settings.gravity.y, settings.gravity.z});
		j["gravityScale"] = settings.gravityScale;

		// Simulation
		j["simulation"] = {
			{"fixedTimestep", settings.fixedTimestep},
			{"maxAccumulator", settings.maxAccumulator},
			{"maxStepsPerFrame", settings.maxStepsPerFrame}
		};

		// Sleep thresholds
		j["sleepThresholds"] = {
			{"linearVelocity", settings.linearSleepThreshold},
			{"angularVelocity", settings.angularSleepThreshold},
			{"timeToSleep", settings.timeToSleep}
		};

		// Collision layers
		j["collisionLayers"] = json::array();
		for (const auto& layer : settings.layers) {
			j["collisionLayers"].push_back({
				{"index", layer.index},
				{"name", layer.name},
				{"builtIn", layer.isBuiltIn}
			});
		}

		// Collision matrix
		j["collisionMatrix"] = json::array();
		for (size_t i = 0; i < settings.layers.size(); ++i) {
			json row = json::array();
			for (size_t k = 0; k < settings.layers.size(); ++k) {
				row.push_back(settings.collisionMatrix[i].test(k));
			}
			j["collisionMatrix"].push_back(row);
		}

		return j;
	}

	void SceneSerialization::deserializePhysicsSettings(const json& j, types::PhysicsSettings& settings) {
		// Gravity
		if (j.contains("gravity") && j["gravity"].is_array() && j["gravity"].size() == 3) {
			settings.gravity.x = j["gravity"][0].get<float>();
			settings.gravity.y = j["gravity"][1].get<float>();
			settings.gravity.z = j["gravity"][2].get<float>();
		}
		if (j.contains("gravityScale")) {
			settings.gravityScale = j["gravityScale"].get<float>();
		}

		// Simulation
		if (j.contains("simulation")) {
			const auto& sim = j["simulation"];
			if (sim.contains("fixedTimestep"))
				settings.fixedTimestep = sim["fixedTimestep"].get<double>();
			if (sim.contains("maxAccumulator"))
				settings.maxAccumulator = sim["maxAccumulator"].get<double>();
			if (sim.contains("maxStepsPerFrame"))
				settings.maxStepsPerFrame = sim["maxStepsPerFrame"].get<int>();
		}

		// Sleep thresholds
		if (j.contains("sleepThresholds")) {
			const auto& sleep = j["sleepThresholds"];
			if (sleep.contains("linearVelocity"))
				settings.linearSleepThreshold = sleep["linearVelocity"].get<float>();
			if (sleep.contains("angularVelocity"))
				settings.angularSleepThreshold = sleep["angularVelocity"].get<float>();
			if (sleep.contains("timeToSleep"))
				settings.timeToSleep = sleep["timeToSleep"].get<float>();
		}

		// Collision layers
		if (j.contains("collisionLayers") && j["collisionLayers"].is_array()) {
			settings.layers.clear();
			for (const auto& layerJson : j["collisionLayers"]) {
				types::CollisionLayer layer;
				if (layerJson.contains("index"))
					layer.index = layerJson["index"].get<uint8_t>();
				if (layerJson.contains("name"))
					layer.name = layerJson["name"].get<std::string>();
				if (layerJson.contains("builtIn"))
					layer.isBuiltIn = layerJson["builtIn"].get<bool>();
				settings.layers.push_back(layer);
			}
		}

		// Collision matrix
		if (j.contains("collisionMatrix") && j["collisionMatrix"].is_array()) {
			// Reset all collision matrix entries
			for (auto& row : settings.collisionMatrix) {
				row.reset();
			}

			const auto& matrix = j["collisionMatrix"];
			for (size_t i = 0; i < matrix.size() && i < types::PhysicsSettings::MAX_LAYERS; ++i) {
				if (matrix[i].is_array()) {
					for (size_t k = 0; k < matrix[i].size() && k < types::PhysicsSettings::MAX_LAYERS; ++k) {
						if (matrix[i][k].is_boolean() && matrix[i][k].get<bool>()) {
							settings.collisionMatrix[i].set(k);
						}
					}
				}
			}
		}
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
			
			deserializeEntity(childJson, child, sceneGraph, false, progressCallback, entitiesLoaded, totalEntities);
		}
	}
	
	void SceneSerialization::deserializeEntity(const json& entityJson, scene::Entity& entity, scene::SceneGraphSystem& sceneGraph, bool isRoot,
											   SceneLoadProgressCallback progressCallback, size_t& entitiesLoaded, size_t totalEntities) {

		std::string entityName = "Unnamed";
		if (entityJson.contains("name")) {
			entityName = entityJson["name"].get<std::string>();
			entity.setName(entityName);
		}

		// Restore active state
		if (entityJson.contains("isActive") && entityJson["isActive"].is_boolean()) {
			if (entity.hasComponent<components::NameComponent>()) {
				entity.getComponent<components::NameComponent>().isActive = entityJson["isActive"].get<bool>();
			}
		}

		if (progressCallback) {
			progressCallback(entityName, entitiesLoaded, totalEntities);
		}
		++entitiesLoaded;
		
		if (isRoot && entityJson.contains("uuid")) {
			uint64_t uuidValue = entityJson["uuid"].get<uint64_t>();
			entity.addOrReplaceComponent<components::UUIDComponent>(uuidValue);
		}

		
		if (entityJson.contains("transform")) {
			auto& transform = entity.getComponent<components::TransformComponent>();
			deserializeTransform(entityJson["transform"], transform);
		}
		
		if (entityJson.contains("components")) {
			const auto& componentsJson = entityJson["components"];
			
			if (componentsJson.contains("camera")) {
				auto& camera = entity.addOrReplaceComponent<components::CameraComponent>();
				deserializeCamera(componentsJson["camera"], camera);
				if (!componentsJson.contains("billboard")) {
					auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
					billboard.iconType = components::BillboardIconType::Camera;
				}
			}
			
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

			if (componentsJson.contains("audioSource2D")) {
				auto& audioComp = entity.addOrReplaceComponent<components::AudioSource2DComponent>();
				deserializeAudioSource2D(componentsJson["audioSource2D"], audioComp);
				if (!componentsJson.contains("billboard")) {
					auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
					billboard.iconType = components::BillboardIconType::AudioSource;
				}
			}

			if (componentsJson.contains("audioSource3D")) {
				auto& audioComp = entity.addOrReplaceComponent<components::AudioSource3DComponent>();
				deserializeAudioSource3D(componentsJson["audioSource3D"], audioComp);
				if (!componentsJson.contains("billboard")) {
					auto& billboard = entity.addOrReplaceComponent<components::BillboardComponent>();
					billboard.iconType = components::BillboardIconType::AudioSource;
				}
			}

			if (componentsJson.contains("script")) {
				auto& scriptComp = entity.addOrReplaceComponent<components::ScriptComponent>();
				deserializeScript(componentsJson["script"], scriptComp);
			}

			if (componentsJson.contains("collider")) {
				auto& colliderComp = entity.addOrReplaceComponent<components::ColliderComponent>();
				deserializeCollider(componentsJson["collider"], colliderComp);
			}

			if (componentsJson.contains("rigidBody")) {
				auto& rigidBodyComp = entity.addOrReplaceComponent<components::RigidBodyComponent>();
				deserializeRigidBody(componentsJson["rigidBody"], rigidBodyComp);
			}
		}

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

			// Deserialize physics settings if present
			if (sceneJson.contains("physicsSettings") && sceneJson["physicsSettings"].is_object()) {
				types::PhysicsSettings settings = types::PhysicsSettings::createDefault();
				deserializePhysicsSettings(sceneJson["physicsSettings"], settings);
				sceneGraph.setPhysicsSettings(settings);
				vfLogInfo("Physics settings loaded from scene file");
			} else {
				// Use default physics settings for older scenes
				sceneGraph.setPhysicsSettings(types::PhysicsSettings::createDefault());
			}

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
			sceneJson["version"] = "1.1";

			scene::Entity& root = sceneGraph.GetRoot();

			sceneJson["root"] = serializeEntity(root);

			// Serialize physics settings at scene level
			sceneJson["physicsSettings"] = serializePhysicsSettings(sceneGraph.getPhysicsSettings());

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

	json SceneSerialization::createSnapshot(scene::SceneGraphSystem& sceneGraph)
	{
		try {
			json snapshot;
			snapshot["version"] = "1.1";

			scene::Entity& root = sceneGraph.GetRoot();
			snapshot["root"] = serializeEntity(root);

			// Include physics settings in snapshot
			snapshot["physicsSettings"] = serializePhysicsSettings(sceneGraph.getPhysicsSettings());

			return snapshot;
		}
		catch (const std::exception& e) {
			vfLogError("Failed to create scene snapshot: {}", e.what());
			return json();
		}
	}

	bool SceneSerialization::restoreFromSnapshot(const json& snapshot, scene::SceneGraphSystem& sceneGraph)
	{
		try {
			// Validate snapshot structure
			if (!snapshot.is_object()) {
				vfLogError("Invalid snapshot: not a JSON object");
				return false;
			}

			if (!snapshot.contains("root") || !snapshot["root"].is_object()) {
				vfLogError("Invalid snapshot: missing or invalid 'root' object");
				return false;
			}

			// Clear current scene and restore from snapshot
			sceneGraph.clearScene();

			// Restore physics settings if present
			if (snapshot.contains("physicsSettings") && snapshot["physicsSettings"].is_object()) {
				types::PhysicsSettings settings = types::PhysicsSettings::createDefault();
				deserializePhysicsSettings(snapshot["physicsSettings"], settings);
				sceneGraph.setPhysicsSettings(settings);
			}

			// Deserialize root entity (no progress callback for snapshot restore)
			scene::Entity& root = sceneGraph.GetRoot();
			size_t entitiesLoaded = 0;
			size_t totalEntities = countEntities(snapshot["root"]);
			deserializeEntity(snapshot["root"], root, sceneGraph, true, nullptr, entitiesLoaded, totalEntities);

			vfLogInfo("Scene restored from snapshot successfully");
			return true;
		}
		catch (const std::exception& e) {
			vfLogError("Failed to restore scene from snapshot: {}", e.what());
			// Scene is in partial state - clear to avoid corruption
			sceneGraph.clearScene();
			return false;
		}
	}

}
