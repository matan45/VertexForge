#include "OrbitCamera.hpp"
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

namespace editor {

    OrbitCamera::OrbitCamera()
    {
        updateMatrices();
    }

    void OrbitCamera::fitToBounds(const math::AABB& bounds)
    {
        // Set target to center of bounding box
        target = bounds.getCenter();

        // Calculate distance to fit the entire mesh in view
        glm::vec3 extents = bounds.getExtents();
        float maxExtent = glm::max(glm::max(extents.x, extents.y), extents.z);

        // Calculate distance based on FOV to ensure mesh fits in view
        float fovRadians = glm::radians(fieldOfView);
        distance = (maxExtent * 2.0f) / std::tan(fovRadians * 0.5f);

        // Add some padding
        distance *= 1.5f;

        // Dynamically adjust limits based on mesh size
        maxDistance = distance * 10.0f;
        minDistance = distance * 0.01f;
        farPlane = maxDistance * 2.0f;
        nearPlane = std::max(0.01f, minDistance * 0.1f);

        // Clamp to valid range
        distance = glm::clamp(distance, minDistance, maxDistance);

        // Reset angles to default view
        yaw = 45.0f;
        pitch = 30.0f;

        updateMatrices();
    }

    void OrbitCamera::processMouseDrag(float xOffset, float yOffset)
    {
        yaw += xOffset * orbitSensitivity;
        pitch += yOffset * orbitSensitivity;

        clampPitch();
        updateMatrices();
    }

    void OrbitCamera::processScroll(float delta)
    {
        distance -= delta * zoomSensitivity * distance * 0.1f;
        distance = glm::clamp(distance, minDistance, maxDistance);
        updateMatrices();
    }

    glm::vec3 OrbitCamera::getPosition() const
    {
        // Convert spherical coordinates to Cartesian
        float pitchRad = glm::radians(pitch);
        float yawRad = glm::radians(yaw);

        float x = distance * std::cos(pitchRad) * std::sin(yawRad);
        float y = distance * std::sin(pitchRad);
        float z = distance * std::cos(pitchRad) * std::cos(yawRad);

        return target + glm::vec3(x, y, z);
    }

    void OrbitCamera::setAspectRatio(float aspect)
    {
        if (aspectRatio != aspect) {
            aspectRatio = aspect;
            updateMatrices();
        }
    }

    void OrbitCamera::setDistance(float dist)
    {
        distance = glm::clamp(dist, minDistance, maxDistance);
        updateMatrices();
    }

    void OrbitCamera::updateMatrices()
    {
        glm::vec3 position = getPosition();
        viewMatrix = glm::lookAt(position, target, glm::vec3(0.0f, 1.0f, 0.0f));
        projectionMatrix = glm::perspective(glm::radians(fieldOfView), aspectRatio, nearPlane, farPlane);
    }

    void OrbitCamera::clampPitch()
    {
        // Clamp pitch to avoid gimbal lock and going upside down
        pitch = glm::clamp(pitch, -89.0f, 89.0f);
    }

}
