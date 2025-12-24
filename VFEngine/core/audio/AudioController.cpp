#include "AudioController.hpp"
#include <spdlog/spdlog.h>

namespace core::audio {

    AudioController::AudioController()
        : audioSystem(std::make_unique<AudioSystem>())
        , bufferManager(std::make_unique<AudioBufferManager>())
        , sourceManager(std::make_unique<AudioSourceManager>(0))  // Don't create sources yet - no context
        , listener(std::make_unique<AudioListener>()) {
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

        // Now that we have a valid OpenAL context, initialize the source pool
        sourceManager->initPool(32);

        initialized = true;
        spdlog::info("AudioController initialized successfully");
        return true;
    }

    void AudioController::cleanUp() {
        if (!initialized) {
            return;
        }

        // Stop all sources first while context is still valid
        stopAll();

        // Destroy source manager while context is still valid (this deletes OpenAL sources)
        sourceManager.reset();

        // Unload all buffers while context is still valid
        bufferManager->unloadAll();

        // Now destroy the OpenAL context
        audioSystem->cleanUp();

        // Recreate empty managers for potential reinitialization
        sourceManager = std::make_unique<AudioSourceManager>(0);
        bufferManager = std::make_unique<AudioBufferManager>();

        initialized = false;
        spdlog::info("AudioController cleaned up");
    }

    void AudioController::update() {
        if (!initialized) return;
        sourceManager->update();
    }

    void AudioController::setMasterVolume(float volume) {
        masterVolume = volume;
        listener->setGain(volume);
    }

    AudioHandle AudioController::playSound(const std::string& path, const PlaySoundParams& params) {
        if (!initialized) return InvalidAudioHandle;

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

    void AudioController::stopSound(AudioHandle handle) {
        if (!initialized) return;

        AudioSource* source = sourceManager->getSource(handle);
        if (source) {
            source->stop();
        }
        sourceManager->releaseSource(handle);
    }

    void AudioController::pauseSound(AudioHandle handle) {
        if (!initialized) return;

        AudioSource* source = sourceManager->getSource(handle);
        if (source) {
            source->pause();
        }
    }

    void AudioController::resumeSound(AudioHandle handle) {
        if (!initialized) return;

        AudioSource* source = sourceManager->getSource(handle);
        if (source) {
            source->play();
        }
    }

    bool AudioController::isPlaying(AudioHandle handle) const {
        if (!initialized) return false;

        const AudioSource* source = sourceManager->getSource(handle);
        return source && source->isPlaying();
    }

    void AudioController::setVolume(AudioHandle handle, float volume) {
        if (!initialized) return;

        AudioSource* source = sourceManager->getSource(handle);
        if (source) {
            source->setVolume(volume);
        }
    }

    void AudioController::setPitch(AudioHandle handle, float pitch) {
        if (!initialized) return;

        AudioSource* source = sourceManager->getSource(handle);
        if (source) {
            source->setPitch(pitch);
        }
    }

    void AudioController::setPosition(AudioHandle handle, const glm::vec3& position) {
        if (!initialized) return;

        AudioSource* source = sourceManager->getSource(handle);
        if (source) {
            source->setPosition(position);
        }
    }

    void AudioController::setLooping(AudioHandle handle, bool loop) {
        if (!initialized) return;

        AudioSource* source = sourceManager->getSource(handle);
        if (source) {
            source->setLooping(loop);
        }
    }

    void AudioController::stopAll() {
        if (!initialized) return;
        sourceManager->stopAll();
    }

    void AudioController::pauseAll() {
        if (!initialized) return;
        sourceManager->pauseAll();
    }

    void AudioController::resumeAll() {
        if (!initialized) return;
        sourceManager->resumeAll();
    }

    void AudioController::setListenerPosition(const glm::vec3& position, const glm::vec3& forward,
                                               const glm::vec3& up) {
        if (!initialized) return;
        listener->setPosition(position);
        listener->setOrientation(forward, up);
    }

    void AudioController::setListenerVelocity(const glm::vec3& velocity) {
        if (!initialized) return;
        listener->setVelocity(velocity);
    }

    void AudioController::setDistanceModel(DistanceModel model) {
        if (!initialized) return;
        audioSystem->setDistanceModel(model);
    }

    void AudioController::setDopplerFactor(float factor) {
        if (!initialized) return;
        audioSystem->setDopplerFactor(factor);
    }

    void AudioController::setSpeedOfSound(float speed) {
        if (!initialized) return;
        audioSystem->setSpeedOfSound(speed);
    }

    bool AudioController::loadBuffer(const std::string& path) {
        if (!initialized) return false;
        return bufferManager->loadBuffer(path) != 0;
    }

    void AudioController::unloadBuffer(const std::string& path) {
        if (!initialized) return;
        bufferManager->unloadBuffer(path);
    }

    bool AudioController::isBufferLoaded(const std::string& path) const {
        if (!initialized) return false;
        return bufferManager->isLoaded(path);
    }

    size_t AudioController::getActiveSourceCount() const {
        if (!initialized) return 0;
        return sourceManager->getActiveCount();
    }

    size_t AudioController::getLoadedBufferCount() const {
        if (!initialized) return 0;
        return bufferManager->getLoadedCount();
    }

}
