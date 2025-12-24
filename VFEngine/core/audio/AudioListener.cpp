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

    void AudioListener::setVelocity(const glm::vec3& velocity) {
        currentVelocity = velocity;
        alListener3f(AL_VELOCITY, velocity.x, velocity.y, velocity.z);
        AudioSystem::checkError("setListenerVelocity");
    }

    glm::vec3 AudioListener::getVelocity() const {
        return currentVelocity;
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

    void AudioListener::getOrientation(glm::vec3& forward, glm::vec3& up) const {
        forward = currentForward;
        up = currentUp;
    }

    void AudioListener::setGain(float gain) {
        alListenerf(AL_GAIN, gain);
        AudioSystem::checkError("setListenerGain");
    }

    float AudioListener::getGain() const {
        float gain;
        alGetListenerf(AL_GAIN, &gain);
        return gain;
    }

    void AudioListener::update(const glm::vec3& position, const glm::vec3& forward,
                                const glm::vec3& up, const glm::vec3& velocity) {
        setPosition(position);
        setVelocity(velocity);
        setOrientation(forward, up);
    }

}
