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

    void AudioAdapter::setMasterVolume(float volume) {
        audioController->setMasterVolume(volume);
    }

    float AudioAdapter::getMasterVolume() const {
        return audioController->getMasterVolume();
    }

    services::AudioHandleId AudioAdapter::playSound(const std::string& path,
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

        return audioController->playSound(path, coreParams);
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

        return audioController->playSound3D(path, position, coreParams);
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

    void AudioAdapter::setPosition(services::AudioHandleId handle, const glm::vec3& position) {
        audioController->setPosition(handle, position);
    }

    void AudioAdapter::setLooping(services::AudioHandleId handle, bool loop) {
        audioController->setLooping(handle, loop);
    }

    void AudioAdapter::stopAll() {
        audioController->stopAll();
    }

    void AudioAdapter::pauseAll() {
        audioController->pauseAll();
    }

    void AudioAdapter::resumeAll() {
        audioController->resumeAll();
    }

    void AudioAdapter::setListenerPosition(const glm::vec3& position, const glm::vec3& forward,
                                            const glm::vec3& up) {
        audioController->setListenerPosition(position, forward, up);
    }

    void AudioAdapter::setListenerVelocity(const glm::vec3& velocity) {
        audioController->setListenerVelocity(velocity);
    }

    void AudioAdapter::setDistanceModel(services::AudioDistanceModel model) {
        audio::DistanceModel coreModel;
        switch (model) {
            case services::AudioDistanceModel::InverseDistance:
                coreModel = audio::DistanceModel::InverseDistance;
                break;
            case services::AudioDistanceModel::InverseDistanceClamped:
                coreModel = audio::DistanceModel::InverseDistanceClamped;
                break;
            case services::AudioDistanceModel::LinearDistance:
                coreModel = audio::DistanceModel::LinearDistance;
                break;
            case services::AudioDistanceModel::LinearDistanceClamped:
                coreModel = audio::DistanceModel::LinearDistanceClamped;
                break;
            case services::AudioDistanceModel::ExponentDistance:
                coreModel = audio::DistanceModel::ExponentDistance;
                break;
            case services::AudioDistanceModel::ExponentDistanceClamped:
                coreModel = audio::DistanceModel::ExponentDistanceClamped;
                break;
            case services::AudioDistanceModel::None:
            default:
                coreModel = audio::DistanceModel::None;
                break;
        }
        audioController->setDistanceModel(coreModel);
    }

    void AudioAdapter::setDopplerFactor(float factor) {
        audioController->setDopplerFactor(factor);
    }

    void AudioAdapter::setSpeedOfSound(float speed) {
        audioController->setSpeedOfSound(speed);
    }

    bool AudioAdapter::loadBuffer(const std::string& path) {
        return audioController->loadBuffer(path);
    }

    void AudioAdapter::unloadBuffer(const std::string& path) {
        audioController->unloadBuffer(path);
    }

    bool AudioAdapter::isBufferLoaded(const std::string& path) const {
        return audioController->isBufferLoaded(path);
    }

    size_t AudioAdapter::getActiveSourceCount() const {
        return audioController->getActiveSourceCount();
    }

    size_t AudioAdapter::getLoadedBufferCount() const {
        return audioController->getLoadedBufferCount();
    }

}
