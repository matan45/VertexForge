#include "StreamingAudioManager.hpp"
#include "print/Logger.hpp"
#include <algorithm>

namespace core::audio {

    StreamingAudioManager::~StreamingAudioManager() {
        stopAll();
        activeSources.clear();
    }

    AudioHandle StreamingAudioManager::generateHandle() {
        return StreamingHandleBase | (nextHandleId++);
    }

    AudioHandle StreamingAudioManager::playStreaming(const std::string& path,
                                                      const AudioSourceConfig& config,
                                                      const StreamingConfig& streamConfig) {
        loggerInfo("StreamingAudioManager::playStreaming - path: {}", path);
        loggerInfo("  config: volume={}, pitch={}, loop={}", config.volume, config.pitch, config.loop);
        loggerInfo("  streamConfig: bufferCount={}, bufferDuration={}",
                   streamConfig.bufferCount, streamConfig.bufferDurationSeconds);

        auto source = std::make_unique<StreamingAudioSource>();

        loggerInfo("  Opening streaming source...");
        if (!source->open(path, streamConfig)) {
            loggerError("Failed to open streaming audio: {}", path);
            return InvalidAudioHandle;
        }

        loggerInfo("  Applying config...");
        source->applyConfig(config);

        loggerInfo("  Starting playback...");
        source->play();

        AudioHandle handle = generateHandle();
        activeSources[handle] = std::move(source);

        loggerInfo("  Playback started, handle: {}", handle);
        return handle;
    }

    void StreamingAudioManager::stop(AudioHandle handle) {
        auto it = activeSources.find(handle);
        if (it != activeSources.end()) {
            it->second->stop();
            activeSources.erase(it);
        }
    }

    void StreamingAudioManager::pause(AudioHandle handle) {
        auto it = activeSources.find(handle);
        if (it != activeSources.end()) {
            it->second->pause();
        }
    }

    void StreamingAudioManager::resume(AudioHandle handle) {
        auto it = activeSources.find(handle);
        if (it != activeSources.end()) {
            it->second->play();
        }
    }

    void StreamingAudioManager::setVolume(AudioHandle handle, float volume) {
        auto it = activeSources.find(handle);
        if (it != activeSources.end()) {
            it->second->setVolume(volume);
        }
    }

    void StreamingAudioManager::setPitch(AudioHandle handle, float pitch) {
        auto it = activeSources.find(handle);
        if (it != activeSources.end()) {
            it->second->setPitch(pitch);
        }
    }

    void StreamingAudioManager::setLooping(AudioHandle handle, bool loop) {
        auto it = activeSources.find(handle);
        if (it != activeSources.end()) {
            it->second->setLooping(loop);
        }
    }

    bool StreamingAudioManager::isPlaying(AudioHandle handle) const {
        auto it = activeSources.find(handle);
        if (it != activeSources.end()) {
            return it->second->isPlaying();
        }
        return false;
    }

    float StreamingAudioManager::getPlaybackPosition(AudioHandle handle) const {
        auto it = activeSources.find(handle);
        if (it != activeSources.end()) {
            return it->second->getPlaybackPosition();
        }
        return 0.0f;
    }

    bool StreamingAudioManager::setPlaybackPosition(AudioHandle handle, float seconds) {
        auto it = activeSources.find(handle);
        if (it != activeSources.end()) {
            return it->second->setPlaybackPosition(seconds);
        }
        return false;
    }

    float StreamingAudioManager::getDuration(AudioHandle handle) const {
        auto it = activeSources.find(handle);
        if (it != activeSources.end()) {
            return it->second->getDuration();
        }
        return 0.0f;
    }

    void StreamingAudioManager::stopAll() {
        for (auto& [handle, source] : activeSources) {
            source->stop();
        }
        activeSources.clear();
    }

    void StreamingAudioManager::update() {
        // Update all active streaming sources
        for (auto& [handle, source] : activeSources) {
            source->update();
        }

        // Clean up finished non-looping sources
        cleanupFinishedSources();
    }

    void StreamingAudioManager::cleanupFinishedSources() {
        for (auto it = activeSources.begin(); it != activeSources.end(); ) {
            if (it->second->isFinished()) {
                it = activeSources.erase(it);
            } else {
                ++it;
            }
        }
    }

}
