#include "AudioAdapter.hpp"
#include "../audio/AudioController.hpp"

namespace core {

    AudioAdapter::AudioAdapter()
        : audioController(std::make_unique<audio::AudioController>()) {
    }

    AudioAdapter::~AudioAdapter() {
        if (audioController && audioController->isInitialized()) {
            audioController->cleanUp();
        }
    }

    bool AudioAdapter::init() {
        return audioController->init();
    }

    void AudioAdapter::cleanUp() {
        audioController->cleanUp();
    }

    void AudioAdapter::update() {
        audioController->update();
    }

    bool AudioAdapter::isInitialized() const {
        return audioController->isInitialized();
    }

    services::AudioHandleId AudioAdapter::playSound3D(const std::string& path, const glm::vec3& position,
                                                       const services::AudioPlayParams& params) {
        audio::PlaySoundParams coreParams;
        coreParams.volume = params.volume;
        coreParams.pitch = params.pitch;
        coreParams.loop = params.loop;
        coreParams.is3D = true;
        coreParams.position = position;
        coreParams.velocity = params.velocity;
        coreParams.minDistance = params.minDistance;
        coreParams.maxDistance = params.maxDistance;
        coreParams.rolloffFactor = params.rolloffFactor;
        coreParams.streaming = params.streaming;

        return audioController->playSound3D(path, position, coreParams);
    }

    services::AudioHandleId AudioAdapter::playStreamingSound(const std::string& path,
                                                              const services::AudioPlayParams& params) {
        audio::PlaySoundParams coreParams;
        coreParams.volume = params.volume;
        coreParams.pitch = params.pitch;
        coreParams.loop = params.loop;
        coreParams.is3D = params.is3D;
        coreParams.position = params.position;
        coreParams.velocity = params.velocity;
        coreParams.minDistance = params.minDistance;
        coreParams.maxDistance = params.maxDistance;
        coreParams.rolloffFactor = params.rolloffFactor;
        coreParams.streaming = true;

        return audioController->playStreamingSound(path, coreParams);
    }

    void AudioAdapter::stopSound(services::AudioHandleId handle) {
        audioController->stopSound(handle);
    }

    void AudioAdapter::pauseSound(services::AudioHandleId handle) {
        audioController->pauseSound(handle);
    }

    void AudioAdapter::resumeSound(services::AudioHandleId handle) {
        audioController->resumeSound(handle);
    }

    bool AudioAdapter::isPlaying(services::AudioHandleId handle) const {
        return audioController->isPlaying(handle);
    }

    void AudioAdapter::setVolume(services::AudioHandleId handle, float volume) {
        audioController->setVolume(handle, volume);
    }

    void AudioAdapter::setPitch(services::AudioHandleId handle, float pitch) {
        audioController->setPitch(handle, pitch);
    }

    void AudioAdapter::setListenerPosition(const glm::vec3& position, const glm::vec3& forward,
                                            const glm::vec3& up) {
        audioController->setListenerPosition(position, forward, up);
    }

    float AudioAdapter::getPlaybackPosition(services::AudioHandleId handle) const {
        return audioController->getPlaybackPosition(handle);
    }

    bool AudioAdapter::setPlaybackPosition(services::AudioHandleId handle, float seconds) {
        return audioController->setPlaybackPosition(handle, seconds);
    }

    float AudioAdapter::getDuration(services::AudioHandleId handle) const {
        return audioController->getDuration(handle);
    }

}
