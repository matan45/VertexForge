#include "AudioListener.hpp"
#include "AudioSystem.hpp"
#include <AL/al.h>

namespace core::audio {

    void AudioListener::setPosition(const glm::vec3& position) {
        currentPosition = position;
        alListener3f(AL_POSITION, position.x, position.y, position.z);
        AudioSystem::checkError("setListenerPosition");
    }

    glm::vec3 AudioListener::getPosition() const {
        return currentPosition;
    }

    void AudioListener::setOrientation(const glm::vec3& forward, const glm::vec3& up) {
        currentForward = forward;
        currentUp = up;

        float orientation[6] = {
            forward.x, forward.y, forward.z,
            up.x, up.y, up.z
        };
        alListenerfv(AL_ORIENTATION, orientation);
        AudioSystem::checkError("setListenerOrientation");
    }

}
