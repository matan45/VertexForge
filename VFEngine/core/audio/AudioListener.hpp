#pragma once
#include <glm/glm.hpp>

namespace core::audio {

    class AudioListener {
    private:
        glm::vec3 currentPosition{0.0f};
        glm::vec3 currentForward{0.0f, 0.0f, -1.0f};
        glm::vec3 currentUp{0.0f, 1.0f, 0.0f};
        glm::vec3 currentVelocity{0.0f};
    public:
        explicit AudioListener() = default;
        ~AudioListener() = default;

        void setPosition(const glm::vec3& position);
        glm::vec3 getPosition() const;

        void setOrientation(const glm::vec3& forward, const glm::vec3& up);

        // VK-1506: AL_VELOCITY on the listener for doppler. Fed by the runtime camera
        // (editor listener velocity stays zero — see AudioSceneUpdater).
        void setVelocity(const glm::vec3& velocity);
    };

}
