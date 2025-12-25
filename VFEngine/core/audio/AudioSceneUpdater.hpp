#pragma once
#include <glm/glm.hpp>

namespace core::audio {

    /**
     * AudioSceneUpdater handles runtime audio updates:
     * - Updating the listener position from camera
     */
    class AudioSceneUpdater {
    public:
        AudioSceneUpdater() = default;
        ~AudioSceneUpdater() = default;

        // Update listener position from editor camera
        void updateListenerFromCamera(const glm::vec3& position,
                                       const glm::vec3& forward,
                                       const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f));

        // Update listener from entity with primary CameraComponent (for runtime)
        void updateListenerFromPrimaryCamera();
    };

}
