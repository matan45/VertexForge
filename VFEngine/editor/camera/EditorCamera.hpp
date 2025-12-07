#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
namespace editor {

    /**
     * EditorCamera - Standalone camera for editor viewport navigation.
     * 
     * This camera is NOT part of the ECS/scene graph. It exists purely for
     * editor scene navigation and is separate from any CameraComponent entities
     * in the scene (which are used during game runtime/play mode).
     * 
     * Following the pattern used by Unity (SceneView camera), Unreal (FEditorViewportClient),
     * and Godot (internal Editor Camera3D).
     */
    class EditorCamera {
    public:
        EditorCamera();
        ~EditorCamera() = default;

        // Transform state
        glm::vec3 position{ 0.0f, 2.0f, 5.0f };
        glm::vec3 rotation{ 0.0f };  // Euler angles (pitch, yaw, roll) in degrees

        // Projection settings
        float fieldOfView = 60.0f;
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
        float aspectRatio = 1.778f;  // 16:9 default

        // Navigation settings
        float moveSpeed = 5.0f;
        float mouseSensitivity = 0.1f;

        // Matrix getters
        const glm::mat4& getViewMatrix() const { return viewMatrix; }
        const glm::mat4& getProjectionMatrix() const { return projectionMatrix; }

        // Update matrices after changing position/rotation
        void updateViewMatrix();
        void updateProjectionMatrix();

        // Set aspect ratio (typically called on viewport resize)
        void setAspectRatio(float aspect);

        // Process input for camera movement
        // Called by ViewPort when the viewport is focused/hovered
        void processKeyboardInput(float deltaTime, bool forward, bool backward, 
                                   bool left, bool right, bool up, bool down, bool sprint);
        void processMouseMovement(float xOffset, float yOffset);

    private:
        glm::mat4 viewMatrix{ 1.0f };
        glm::mat4 projectionMatrix{ 1.0f };

        // Calculate forward direction from rotation
        glm::vec3 getForwardDirection() const;
        glm::vec3 getRightDirection() const;
    };

}
