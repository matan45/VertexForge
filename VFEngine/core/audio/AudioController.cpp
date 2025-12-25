#include "AudioController.hpp"
#include <spdlog/spdlog.h>

namespace core::audio {

    AudioController::AudioController()
        : audioSystem(std::make_unique<AudioSystem>())
        , bufferManager(std::make_unique<AudioBufferManager>())
        , sourceManager(std::make_unique<AudioSourceManager>(0))
        , listener(std::make_unique<AudioListener>())
        , streamingManager(std::make_unique<StreamingAudioManager>()) {
    }

    AudioController::~AudioController() {
        if (initialized) {
            cleanUp();
        }
    }

    bool AudioController::init() {
        if (initialized) {
            spdlog::warn("AudioController already initialized");
            return true;
        }

        if (!audioSystem->init()) {
            spdlog::error("Failed to initialize AudioSystem");
            return false;
        }

        sourceManager->initPool(32);

        initialized = true;
        spdlog::info("AudioController initialized successfully");
        return true;
    }

    void AudioController::cleanUp() {
        if (!initialized) {
            return;
        }

        stopAll();
        streamingManager->stopAll();

        sourceManager.reset();
        streamingManager.reset();
        bufferManager->unloadAll();
        audioSystem->cleanUp();

        sourceManager = std::make_unique<AudioSourceManager>(0);
        bufferManager = std::make_unique<AudioBufferManager>();
        streamingManager = std::make_unique<StreamingAudioManager>();

        initialized = false;
        spdlog::info("AudioController cleaned up");
    }

    void AudioController::update() {
        if (!initialized) return;
        sourceManager->update();
        streamingManager->update();
    }

    AudioHandle AudioController::playSound(const std::string& path, const PlaySoundParams& params) {
        if (!initialized) return InvalidAudioHandle;

        if (params.streaming) {
            return playStreamingSound(path, params);
        }

        ALuint bufferId = bufferManager->loadBuffer(path);
        if (bufferId == 0) {
            spdlog::error("Failed to load audio buffer: {}", path);
            return InvalidAudioHandle;
        }

        AudioHandle handle = sourceManager->acquireSource();
        if (handle == InvalidAudioHandle) {
            spdlog::error("Failed to acquire audio source");
            return InvalidAudioHandle;
        }

        AudioSource* source = sourceManager->getSource(handle);
        if (!source) {
            sourceManager->releaseSource(handle);
            return InvalidAudioHandle;
        }

        source->setBuffer(bufferId);

        AudioSourceConfig config;
        config.volume = params.volume;
        config.pitch = params.pitch;
        config.loop = params.loop;
        config.is3D = params.is3D;
        config.position = params.position;
        config.velocity = params.velocity;
        config.minDistance = params.minDistance;
        config.maxDistance = params.maxDistance;
        config.rolloffFactor = params.rolloffFactor;
        source->applyConfig(config);

        source->play();

        return handle;
    }

    AudioHandle AudioController::playSound3D(const std::string& path, const glm::vec3& position,
                                              const PlaySoundParams& params) {
        PlaySoundParams params3D = params;
        params3D.is3D = true;
        params3D.position = position;
        return playSound(path, params3D);
    }

    AudioHandle AudioController::playStreamingSound(const std::string& path, const PlaySoundParams& params) {
        if (!initialized) return InvalidAudioHandle;

        AudioSourceConfig config;
        config.volume = params.volume;
        config.pitch = params.pitch;
        config.loop = params.loop;
        config.is3D = params.is3D;
        config.position = params.position;
        config.velocity = params.velocity;
        config.minDistance = params.minDistance;
        config.maxDistance = params.maxDistance;
        config.rolloffFactor = params.rolloffFactor;

        return streamingManager->playStreaming(path, config);
    }

    void AudioController::stopSound(AudioHandle handle) {
        if (!initialized) return;

        if (StreamingAudioManager::isStreamingHandle(handle)) {
            streamingManager->stop(handle);
        } else {
            AudioSource* source = sourceManager->getSource(handle);
            if (source) {
                source->stop();
            }
            sourceManager->releaseSource(handle);
        }
    }

    void AudioController::pauseSound(AudioHandle handle) {
        if (!initialized) return;

        if (StreamingAudioManager::isStreamingHandle(handle)) {
            streamingManager->pause(handle);
        } else {
            AudioSource* source = sourceManager->getSource(handle);
            if (source) {
                source->pause();
            }
        }
    }

    void AudioController::resumeSound(AudioHandle handle) {
        if (!initialized) return;

        if (StreamingAudioManager::isStreamingHandle(handle)) {
            streamingManager->resume(handle);
        } else {
            AudioSource* source = sourceManager->getSource(handle);
            if (source) {
                source->play();
            }
        }
    }

    bool AudioController::isPlaying(AudioHandle handle) const {
        if (!initialized) return false;

        if (StreamingAudioManager::isStreamingHandle(handle)) {
            return streamingManager->isPlaying(handle);
        } else {
            const AudioSource* source = sourceManager->getSource(handle);
            return source && source->isPlaying();
        }
    }

    void AudioController::setVolume(AudioHandle handle, float volume) {
        if (!initialized) return;

        if (StreamingAudioManager::isStreamingHandle(handle)) {
            streamingManager->setVolume(handle, volume);
        } else {
            AudioSource* source = sourceManager->getSource(handle);
            if (source) {
                source->setVolume(volume);
            }
        }
    }

    void AudioController::setPitch(AudioHandle handle, float pitch) {
        if (!initialized) return;

        if (StreamingAudioManager::isStreamingHandle(handle)) {
            streamingManager->setPitch(handle, pitch);
        } else {
            AudioSource* source = sourceManager->getSource(handle);
            if (source) {
                source->setPitch(pitch);
            }
        }
    }

    void AudioController::stopAll() {
        if (!initialized) return;
        sourceManager->stopAll();
        streamingManager->stopAll();
    }

    void AudioController::setListenerPosition(const glm::vec3& position, const glm::vec3& forward,
                                               const glm::vec3& up) {
        if (!initialized) return;
        listener->setPosition(position);
        listener->setOrientation(forward, up);
    }

    float AudioController::getPlaybackPosition(AudioHandle handle) const {
        if (!initialized) return 0.0f;

        if (StreamingAudioManager::isStreamingHandle(handle)) {
            return streamingManager->getPlaybackPosition(handle);
        } else {
            const AudioSource* source = sourceManager->getSource(handle);
            return source ? source->getPlaybackPosition() : 0.0f;
        }
    }

    bool AudioController::setPlaybackPosition(AudioHandle handle, float seconds) {
        if (!initialized) return false;

        if (StreamingAudioManager::isStreamingHandle(handle)) {
            return streamingManager->setPlaybackPosition(handle, seconds);
        } else {
            AudioSource* source = sourceManager->getSource(handle);
            if (source) {
                source->setPlaybackPosition(seconds);
                return true;
            }
            return false;
        }
    }

    float AudioController::getDuration(AudioHandle handle) const {
        if (!initialized) return 0.0f;

        if (StreamingAudioManager::isStreamingHandle(handle)) {
            return streamingManager->getDuration(handle);
        }
        return 0.0f;
    }

}
