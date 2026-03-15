#pragma once
#include <glm/glm.hpp>

namespace core::audio {

    class ReverbZoneManager;

    class AudioSceneUpdater {
    public:
        explicit AudioSceneUpdater() = default;
        ~AudioSceneUpdater() = default;

        void setReverbZoneManager(ReverbZoneManager* manager) { reverbZoneManager = manager; }

        void updateListenerFromCamera(const glm::vec3& position,
                                       const glm::vec3& forward,
                                       const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f));

        void updateListenerFromPrimaryCamera();

    private:
        ReverbZoneManager* reverbZoneManager = nullptr;
        glm::vec3 lastListenerPos{0.0f};
    };

}
