#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
namespace editor {
    
    class EditorCamera {
    public:
        EditorCamera();
        ~EditorCamera() = default;
        
        glm::vec3 position{ 0.0f, 2.0f, 5.0f };
        glm::vec3 rotation{ 0.0f };  // Euler angles (pitch, yaw, roll) in degrees
        
        float fieldOfView = 60.0f;
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
        float aspectRatio = 1.778f;  // 16:9 default
        
        float moveSpeed = 5.0f;
        float mouseSensitivity = 0.1f;
        
        const glm::mat4& getViewMatrix() const { return viewMatrix; }
        const glm::mat4& getProjectionMatrix() const { return projectionMatrix; }
        
        void updateViewMatrix();
        void updateProjectionMatrix();
        
        void setAspectRatio(float aspect);
        
        void processKeyboardInput(float deltaTime, bool forward, bool backward, 
                                   bool left, bool right, bool up, bool down, bool sprint);
        void processMouseMovement(float xOffset, float yOffset);

        glm::vec3 getForwardDirection() const;
        glm::vec3 getRightDirection() const;

    private:
        glm::mat4 viewMatrix{ 1.0f };
        glm::mat4 projectionMatrix{ 1.0f };
    };

}
