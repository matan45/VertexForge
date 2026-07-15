#include "AudioAdapter.hpp"
#include "../../audio/AudioController.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include "../../services/events/lifecycle/AssetLifecycleEvents.hpp"
#include "resource/AssetTypes.hpp"
#include "print/Log.hpp"

namespace core {

    AudioAdapter::AudioAdapter()
        : audioController(std::make_unique<audio::AudioController>())
    {
        auto& dispatcher = events::EventDispatcher::instance();
        assetReleaseToken = dispatcher.subscribe<events::lifecycle::AssetReleaseReadyNotification>(
            [this](const events::lifecycle::AssetReleaseReadyNotification& notification)
            {
                if (notification.type == resource::AssetType::Audio && audioController)
                {
                    audioController->unloadAudioBuffer(notification.path);
                    vfLogInfo("AudioAdapter: Released audio buffer for '{}'", notification.path);
                }
            });
    }

    AudioAdapter::~AudioAdapter() {
        if (assetReleaseToken.isValid())
        {
            events::EventDispatcher::instance().unsubscribe(assetReleaseToken);
        }
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
        coreParams.minDistance = params.minDistance;
        coreParams.maxDistance = params.maxDistance;
        coreParams.rolloffFactor = params.rolloffFactor;
        coreParams.streaming = params.streaming;
        coreParams.enableDistanceFilter = params.enableDistanceFilter;
        coreParams.filterStartDistance = params.filterStartDistance;
        coreParams.filterMaxDistance = params.filterMaxDistance;
        coreParams.filterIntensity = params.filterIntensity;
        coreParams.innerConeAngle = params.innerConeAngle;
        coreParams.outerConeAngle = params.outerConeAngle;
        coreParams.outerConeGain = params.outerConeGain;
        coreParams.direction = params.direction;
        coreParams.busName = params.busName;
        coreParams.priority = params.priority;

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
        coreParams.minDistance = params.minDistance;
        coreParams.maxDistance = params.maxDistance;
        coreParams.rolloffFactor = params.rolloffFactor;
        coreParams.streaming = true;
        coreParams.enableDistanceFilter = params.enableDistanceFilter;
        coreParams.filterStartDistance = params.filterStartDistance;
        coreParams.filterMaxDistance = params.filterMaxDistance;
        coreParams.filterIntensity = params.filterIntensity;
        coreParams.innerConeAngle = params.innerConeAngle;
        coreParams.outerConeAngle = params.outerConeAngle;
        coreParams.outerConeGain = params.outerConeGain;
        coreParams.direction = params.direction;
        coreParams.busName = params.busName;
        coreParams.priority = params.priority;

        return audioController->playStreamingSound(path, coreParams);
    }

    void AudioAdapter::stopSound(services::AudioHandleId handle) {
        audioController->stopSound(handle);
    }

    void AudioAdapter::fadeOutAndRelease(services::AudioHandleId handle, float fadeDurationMs) {
        audioController->fadeOutAndRelease(handle, fadeDurationMs);
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

    void AudioAdapter::setSourceTransform(services::AudioHandleId handle, const glm::vec3& position,
                                          const glm::vec3& direction, const glm::vec3& velocity) {
        audioController->setSourceTransform(handle, position, direction, velocity);
    }

    void AudioAdapter::setSourceOcclusion(services::AudioHandleId handle, float occlusion,
                                          float lpfAmount, float volumeAmount) {
        audioController->setSourceOcclusion(handle, occlusion, lpfAmount, volumeAmount);
    }

    void AudioAdapter::setListenerPosition(const glm::vec3& position, const glm::vec3& forward,
                                            const glm::vec3& up, const glm::vec3& velocity) {
        audioController->setListenerPosition(position, forward, up, velocity);
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

    void AudioAdapter::applySettings(const types::AudioSettings& settings) {
        audioController->applySettings(settings);
    }

    types::AudioSettings AudioAdapter::getCurrentSettings() const {
        return audioController->getCurrentSettings();
    }

    types::AudioHrtfStatus AudioAdapter::getHrtfStatus() const {
        return audioController->getHrtfStatus();
    }

    types::AudioVoiceStats AudioAdapter::getVoiceStats() const {
        return audioController->getVoiceStats();
    }

    std::vector<types::AudioVoiceRow> AudioAdapter::getActiveVoices() const {
        return audioController->getActiveVoices();
    }

    void AudioAdapter::setVoiceDebugEnabled(bool enabled) {
        audioController->setVoiceDebugEnabled(enabled);
    }

    void AudioAdapter::createBus(const std::string& busName, const std::string& parentName) {
        audioController->createBus(busName, parentName);
    }

    void AudioAdapter::setBusVolume(const std::string& busName, float volume) {
        audioController->setBusVolume(busName, volume);
    }

    void AudioAdapter::setBusMuted(const std::string& busName, bool muted) {
        audioController->setBusMuted(busName, muted);
    }

    void AudioAdapter::setBusSoloed(const std::string& busName, bool soloed) {
        audioController->setBusSoloed(busName, soloed);
    }

    float AudioAdapter::getBusVolume(const std::string& busName) const {
        return audioController->getBusVolume(busName);
    }

    bool AudioAdapter::isBusMuted(const std::string& busName) const {
        return audioController->isBusMuted(busName);
    }

    bool AudioAdapter::isBusSoloed(const std::string& busName) const {
        return audioController->isBusSoloed(busName);
    }

    std::vector<std::string> AudioAdapter::getBusNames() const {
        return audioController->getBusNames();
    }

    std::vector<types::AudioBusLevel> AudioAdapter::getBusLevels() const {
        return audioController->getBusLevels();
    }

    void AudioAdapter::saveMixSnapshot(const std::string& name) {
        audioController->saveMixSnapshot(name);
    }

    void AudioAdapter::loadMixSnapshot(const std::string& name) {
        audioController->loadMixSnapshot(name);
    }

    void AudioAdapter::deleteMixSnapshot(const std::string& name) {
        audioController->deleteMixSnapshot(name);
    }

    std::vector<std::string> AudioAdapter::getSnapshotNames() const {
        return audioController->getSnapshotNames();
    }

    // === Audio Effects ===

    bool AudioAdapter::addBusEffect(const std::string& busName, const types::BusEffectConfig& config) {
        return audioController->addBusEffect(busName, config);
    }

    bool AudioAdapter::removeBusEffect(const std::string& busName, uint32_t effectId) {
        return audioController->removeBusEffect(busName, effectId);
    }

    bool AudioAdapter::updateBusEffect(const std::string& busName, uint32_t effectId,
                                        const types::BusEffectConfig& config) {
        return audioController->updateBusEffect(busName, effectId, config);
    }

    bool AudioAdapter::setBusEffectEnabled(const std::string& busName, uint32_t effectId, bool enabled) {
        return audioController->setBusEffectEnabled(busName, effectId, enabled);
    }

    bool AudioAdapter::setBusEffectWetDry(const std::string& busName, uint32_t effectId, float wetDry) {
        return audioController->setBusEffectWetDry(busName, effectId, wetDry);
    }

    std::vector<types::BusEffectConfig> AudioAdapter::getBusEffectChain(const std::string& busName) const {
        return audioController->getBusEffectChain(busName);
    }

    int AudioAdapter::getMaxEffectsPerBus() const {
        return audioController->getMaxEffectsPerBus();
    }

    void* AudioAdapter::getReverbZoneManager() {
        return audioController->getReverbZoneManager();
    }

}
