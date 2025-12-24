#pragma once
#include <glm/glm.hpp>

namespace core::audio {

    /**
     * AudioSceneUpdater handles runtime audio updates:
     * - Updating the listener position from camera
     * - Updating 3D audio source positions from entity transforms
     * - Stopping all audio on scene unload
     */
    class AudioSceneUpdater {
    public:
        AudioSceneUpdater() = default;
        ~AudioSceneUpdater() = default;

        // Called each frame to update 3D audio positions
        void update();

        // Update listener position from editor camera
        void updateListenerFromCamera(const glm::vec3& position,
                                       const glm::vec3& forward,
                                       const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f));

        // Update listener from entity with primary CameraComponent (for runtime)
        void updateListenerFromPrimaryCamera();

        // Update all 3D audio source positions from entity transforms
        void updateAudioSourcePositions();

        // Stop all audio sources (called on scene unload)
        void stopAllAudioSources();
    };

}
