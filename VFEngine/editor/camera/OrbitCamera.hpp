#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "math/Frustum.hpp"

namespace editor {

    class OrbitCamera {
    public:
        OrbitCamera();
        ~OrbitCamera() = default;

        // Orbit target (center point to orbit around)
        glm::vec3 target{ 0.0f };

        // Spherical coordinates
        float distance = 5.0f;      // Distance from target
        float yaw = 0.0f;           // Horizontal angle in degrees
        float pitch = 30.0f;        // Vertical angle in degrees (clamped to avoid gimbal lock)

        // Projection parameters
        float fieldOfView = 45.0f;
        float nearPlane = 0.01f;
        float farPlane = 10000.0f;
        float aspectRatio = 1.0f;

        // Input sensitivity
        float orbitSensitivity = 0.5f;
        float zoomSensitivity = 0.5f;
        float minDistance = 0.01f;
        float maxDistance = 10000.0f;

        // Fit camera to show entire bounding box
        void fitToBounds(const math::AABB& bounds);

        // Input processing
        void processMouseDrag(float xOffset, float yOffset);
        void processScroll(float delta);

        // Matrix access
        const glm::mat4& getViewMatrix() const { return viewMatrix; }
        const glm::mat4& getProjectionMatrix() const { return projectionMatrix; }
        glm::vec3 getPosition() const;

        void setAspectRatio(float aspect);
        void updateMatrices();

    private:
        glm::mat4 viewMatrix{ 1.0f };
        glm::mat4 projectionMatrix{ 1.0f };

        void clampPitch();
    };

}
