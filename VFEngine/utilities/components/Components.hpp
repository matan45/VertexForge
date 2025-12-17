#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <map>
#include <optional>
#include <entt/entt.hpp>
#include "../uuid/UUID.hpp"

namespace components {
	
	struct IBLComponent;
	struct CameraComponent;
	struct MeshComponent;
	struct MaterialComponent;
	struct BillboardComponent;

	// Type list of optional components that can be removed during cleanup
	// Add new optional component types here when they are created
	using OptionalComponents = entt::type_list<IBLComponent, CameraComponent, MeshComponent, MaterialComponent, BillboardComponent>;

	struct WorldTransformComponent
	{
		glm::mat4 worldMatrix;
	};

	struct ParentComponent {
		entt::entity parent = entt::null; // Parent entity handle
	};

	struct ChildrenComponent {
		std::vector<entt::entity> children; // List of child entity handles
	};

	struct NameComponent {
		std::string name;
	};

	struct UUIDComponent {
		uuid::UUID id;

		UUIDComponent() : id() {}  // Generates new UUID
		explicit UUIDComponent(uuid::UUID existingId) : id(existingId) {}
		explicit UUIDComponent(uint64_t existingId) : id(existingId) {}
	};

	struct IBLComponent {
		std::string fileName;
	};

	struct TransformComponent {
		glm::vec3 position{ 0.0f };
		glm::vec3 rotation{ 0.0f }; // Euler angles
		glm::vec3 scale{ 1.0f };
		bool isDirty = true;

		// Mark as dirty when transform changes
		void setPosition(const glm::vec3& newPos) {
			position = newPos;
			isDirty = true;
		}

		void setRotation(const glm::vec3& newRot) {
			rotation = newRot;
			isDirty = true;
		}

		void setScale(const glm::vec3& newScale) {
			scale = newScale;
			isDirty = true;
		}

		// Compute transformation matrix without setting isDirty
		glm::mat4 GetMatrix() const {
			auto transform = glm::mat4(1.0f);
			transform = glm::translate(transform, position);
			transform = glm::rotate(transform, glm::radians(rotation.x), glm::vec3(1, 0, 0));
			transform = glm::rotate(transform, glm::radians(rotation.y), glm::vec3(0, 1, 0));
			transform = glm::rotate(transform, glm::radians(rotation.z), glm::vec3(0, 0, 1));
			transform = glm::scale(transform, scale);
			return transform;
		}
	};

	struct CameraComponent
	{
		glm::mat4 projectionMatrix{1.0f};
		glm::mat4 viewMatrix{1.0f};
		bool isPerspective = true; // True for perspective, false for orthographic
		float fieldOfView = 90.0f; // For perspective cameras, in degrees
		float orthoSize = 10.0f; // For orthographic cameras, half the height of the view
		float nearPlane = 0.1f;
		float farPlane = 1000.0f;
		float aspectRatio = 1.778f; // Typically screen width / height

		// Default constructor - initializes projection matrix with default values
		CameraComponent() {
			updateProjectionMatrix();
			// Initialize view matrix looking down -Z axis
			viewMatrix = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		}

		// Update the projection matrix based on the current settings
		void updateProjectionMatrix()
		{
			if (isPerspective) {
				projectionMatrix = glm::perspective(
					glm::radians(fieldOfView),
					aspectRatio,
					nearPlane,
					farPlane
				);
			} else {
				float orthoHalfWidth = orthoSize * aspectRatio;
				projectionMatrix = glm::ortho(
					-orthoHalfWidth,
					orthoHalfWidth,
					-orthoSize,
					orthoSize,
					nearPlane,
					farPlane
				);
			}
			// Flip Y for Vulkan coordinate system (GLM is designed for OpenGL)
			projectionMatrix[1][1] *= -1;
		}
		// Update the view matrix based on the camera's position, rotation, and direction
		void updateViewMatrix(const glm::vec3& position, const glm::vec3& rotation) {
			// For FPS-style camera: apply yaw (Y) first, then pitch (X)
			// This ensures pitch always rotates around the camera's local X axis
			glm::mat4 transform = glm::mat4(1.0f);
			transform = glm::rotate(transform, glm::radians(rotation.y), glm::vec3(0, 1, 0)); // Yaw
			transform = glm::rotate(transform, glm::radians(rotation.x), glm::vec3(1, 0, 0)); // Pitch
			transform = glm::rotate(transform, glm::radians(rotation.z), glm::vec3(0, 0, 1)); // Roll
			transform = glm::translate(transform, -position);

			// View matrix is the inverse of the transformation matrix.
			viewMatrix = glm::inverse(transform);
		}
	};

	struct MeshComponent {
		std::string meshPath;  // Path to .vfmesh file
		bool showBoundingBox = false;  // Debug: render AABB wireframe
	};

	struct MaterialComponent {
		std::string defaultMaterial;  // .vfMat path for unmapped submeshes
		std::map<std::string, std::string> subMeshMaterials;  // submesh NAME -> .vfMat path
		std::map<std::string, float> parameterOverrides;  // Runtime parameter tweaks

		// Set material for a specific submesh by name
		void setSubMeshMaterial(const std::string& submeshName, const std::string& matPath) {
			subMeshMaterials[submeshName] = matPath;
		}

		// Set the default material for all unmapped submeshes
		void setDefaultMaterial(const std::string& matPath) {
			defaultMaterial = matPath;
		}

		// Get material path for a submesh, falling back to default if not mapped
		std::string getMaterialForSubmesh(const std::string& submeshName) const {
			auto it = subMeshMaterials.find(submeshName);
			if (it != subMeshMaterials.end()) {
				return it->second;
			}
			return defaultMaterial;
		}

		// Check if a submesh has a specific material assigned
		bool hasSubmeshMaterial(const std::string& submeshName) const {
			return subMeshMaterials.find(submeshName) != subMeshMaterials.end();
		}

		// Clear all submesh material assignments
		void clearSubMeshMaterials() {
			subMeshMaterials.clear();
		}

		// Set a runtime parameter override
		void setParameterOverride(const std::string& paramName, float value) {
			parameterOverrides[paramName] = value;
		}

		// Get a parameter override value, returns nullopt if not set
		std::optional<float> getParameterOverride(const std::string& paramName) const {
			auto it = parameterOverrides.find(paramName);
			if (it != parameterOverrides.end()) {
				return it->second;
			}
			return std::nullopt;
		}
	};

	// Billboard sizing mode
	enum class BillboardSizeMode : uint8_t {
		ScreenSpace,  // Constant on-screen size regardless of distance
		WorldSpace    // Size scales with distance
	};

	// Predefined icon types for common entity types
	enum class BillboardIconType : uint8_t {
		Custom = 0,   // Use atlasIndex for custom icon
		Light,        // Light icon
		Camera,       // Camera icon
		AudioSource,  // Speaker/audio icon
		Particle      // Particle emitter icon
	};

	struct BillboardComponent {
		BillboardIconType iconType = BillboardIconType::Custom;
		uint32_t atlasIndex = 0;
		// Sizing
		BillboardSizeMode sizeMode = BillboardSizeMode::ScreenSpace;
		glm::vec2 size{ 32.0f, 32.0f };  // Pixels (screen-space) or world units

		// Appearance
		glm::vec4 colorTint{ 1.0f, 1.0f, 1.0f, 1.0f };  // RGBA

		// Flags
		bool editorOnly = true;   // Only render in editor
		bool selectable = true;   // Allow entity selection via click

		// Get effective atlas index (resolves iconType to atlas position)
		uint32_t getEffectiveAtlasIndex() const {
			return (iconType == BillboardIconType::Custom)
				? atlasIndex
				: static_cast<uint32_t>(iconType);
		}
	};

}
