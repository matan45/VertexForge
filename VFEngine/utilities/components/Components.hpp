#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <entt/entt.hpp>
#include "../uuid/UUID.hpp"

namespace components {

	// Forward declarations for type list
	struct IBLComponent;
	struct CameraComponent;
	struct MeshComponent;

	// Type list of optional components that can be removed during cleanup
	// Add new optional component types here when they are created
	using OptionalComponents = entt::type_list<IBLComponent, CameraComponent, MeshComponent>;

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

}
