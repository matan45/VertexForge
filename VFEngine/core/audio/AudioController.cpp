#include "AudioController.hpp"
#include "print/Log.hpp"

namespace core::audio
{
    AudioController::AudioController()
        : audioSystem(std::make_unique<AudioSystem>())
          , bufferManager(std::make_unique<AudioBufferManager>())
          , sourceManager(std::make_unique<AudioSourceManager>(0))
          , listener(std::make_unique<AudioListener>())
          , streamingManager(std::make_unique<StreamingAudioManager>())
          , busManager(std::make_unique<AudioBusManager>())
    {
    }

    AudioController::~AudioController()
    {
        if (initialized)
        {
            cleanUp();
        }
    }

    bool AudioController::init()
    {
        if (initialized)
        {
            return true;
        }

        if (!audioSystem->init())
        {
            vfLogError("Failed to initialize AudioSystem");
            return false;
        }

        sourceManager->initPool(32);

        busManager->init([this](AudioHandle handle, float effectiveVolume)
        {
            if (StreamingAudioManager::isStreamingHandle(handle))
            {
                streamingManager->setVolume(handle, effectiveVolume);
            }
            else
            {
                AudioSource* source = sourceManager->getSource(handle);
                if (source)
                {
                    source->setVolume(effectiveVolume);
                }
            }
        });
        busManager->createDefaultBuses();

        initialized = true;
        lastUpdateTime = std::chrono::steady_clock::now();
        return true;
    }

    void AudioController::cleanUp()
    {
        if (!initialized)
        {
            return;
        }

        stopAll();

        busManager->cleanUp();
        sourceManager.reset();
        streamingManager.reset();
        bufferManager.reset();
        audioSystem->cleanUp();

        initialized = false;
    }

    void AudioController::update()
    {
        if (!initialized) return;

        auto now = std::chrono::steady_clock::now();
        float deltaTime = std::chrono::duration<float>(now - lastUpdateTime).count();
        lastUpdateTime = now;

        sourceManager->update();
        streamingManager->update();
        sourceManager->updateFilters(listener->getPosition(), deltaTime);
    }

    AudioHandle AudioController::playSound(const std::string& path, const PlaySoundParams& params)
    {
        if (!initialized) return InvalidAudioHandle;

        if (params.streaming)
        {
            return playStreamingSound(path, params);
        }

        ALuint bufferId = bufferManager->loadBuffer(path);
        if (bufferId == 0)
        {
            vfLogError("Failed to load audio buffer: {}", path);
            return InvalidAudioHandle;
        }

        AudioHandle handle = sourceManager->acquireSource();
        if (handle == InvalidAudioHandle)
        {
            vfLogError("Failed to acquire audio source");
            return InvalidAudioHandle;
        }

        AudioSource* source = sourceManager->getSource(handle);
        if (!source)
        {
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
        config.minDistance = params.minDistance;
        config.maxDistance = params.maxDistance;
        config.rolloffFactor = params.rolloffFactor;
        config.enableDistanceFilter = params.enableDistanceFilter;
        config.filterStartDistance = params.filterStartDistance;
        config.filterMaxDistance = params.filterMaxDistance;
        config.filterIntensity = params.filterIntensity;
        config.innerConeAngle = params.innerConeAngle;
        config.outerConeAngle = params.outerConeAngle;
        config.outerConeGain = params.outerConeGain;
        config.direction = params.direction;
        source->applyConfig(config);

        busManager->assignSource(handle, params.busName, params.volume);

        source->play();

        return handle;
    }

    AudioHandle AudioController::playSound3D(const std::string& path, const glm::vec3& position,
                                             const PlaySoundParams& params)
    {
        PlaySoundParams params3D = params;
        params3D.is3D = true;
        params3D.position = position;
        return playSound(path, params3D);
    }

    AudioHandle AudioController::playStreamingSound(const std::string& path, const PlaySoundParams& params)
    {
        if (!initialized) return InvalidAudioHandle;

        AudioSourceConfig config;
        config.volume = params.volume;
        config.pitch = params.pitch;
        config.loop = params.loop;
        config.is3D = params.is3D;
        config.position = params.position;
        config.minDistance = params.minDistance;
        config.maxDistance = params.maxDistance;
        config.rolloffFactor = params.rolloffFactor;
        config.enableDistanceFilter = params.enableDistanceFilter;
        config.filterStartDistance = params.filterStartDistance;
        config.filterMaxDistance = params.filterMaxDistance;
        config.filterIntensity = params.filterIntensity;
        config.innerConeAngle = params.innerConeAngle;
        config.outerConeAngle = params.outerConeAngle;
        config.outerConeGain = params.outerConeGain;
        config.direction = params.direction;

        AudioHandle handle = streamingManager->playStreaming(path, config);

        if (handle != InvalidAudioHandle)
        {
            busManager->assignSource(handle, params.busName, params.volume);
        }

        return handle;
    }

    void AudioController::stopSound(AudioHandle handle)
    {
        if (!initialized) return;

        busManager->removeSource(handle);

        if (StreamingAudioManager::isStreamingHandle(handle))
        {
            streamingManager->stop(handle);
        }
        else
        {
            AudioSource* source = sourceManager->getSource(handle);
            if (source)
            {
                source->stop();
                sourceManager->releaseSource(handle);
            }
        }
    }

    void AudioController::pauseSound(AudioHandle handle)
    {
        if (!initialized) return;

        if (StreamingAudioManager::isStreamingHandle(handle))
        {
            streamingManager->pause(handle);
        }
        else
        {
            AudioSource* source = sourceManager->getSource(handle);
            if (source)
            {
                source->pause();
            }
        }
    }

    void AudioController::resumeSound(AudioHandle handle)
    {
        if (!initialized) return;

        if (StreamingAudioManager::isStreamingHandle(handle))
        {
            streamingManager->resume(handle);
        }
        else
        {
            AudioSource* source = sourceManager->getSource(handle);
            if (source)
            {
                source->play();
            }
        }
    }

    bool AudioController::isPlaying(AudioHandle handle) const
    {
        if (!initialized) return false;

        if (StreamingAudioManager::isStreamingHandle(handle))
        {
            return streamingManager->isPlaying(handle);
        }
        else
        {
            const AudioSource* source = sourceManager->getSource(handle);
            return source && source->isPlaying();
        }
    }

    void AudioController::setVolume(AudioHandle handle, float volume)
    {
        if (!initialized) return;
        busManager->setSourceUserVolume(handle, volume);
    }

    void AudioController::setPitch(AudioHandle handle, float pitch)
    {
        if (!initialized) return;

        if (StreamingAudioManager::isStreamingHandle(handle))
        {
            streamingManager->setPitch(handle, pitch);
        }
        else
        {
            AudioSource* source = sourceManager->getSource(handle);
            if (source)
            {
                source->setPitch(pitch);
            }
        }
    }

    void AudioController::stopAll()
    {
        if (!initialized) return;
        sourceManager->stopAll();
        streamingManager->stopAll();
    }

    void AudioController::setListenerPosition(const glm::vec3& position, const glm::vec3& forward,
                                              const glm::vec3& up)
    {
        if (!initialized) return;
        listener->setPosition(position);
        listener->setOrientation(forward, up);
    }

    float AudioController::getPlaybackPosition(AudioHandle handle) const
    {
        if (!initialized) return 0.0f;

        if (StreamingAudioManager::isStreamingHandle(handle))
        {
            return streamingManager->getPlaybackPosition(handle);
        }
        else
        {
            const AudioSource* source = sourceManager->getSource(handle);
            return source ? source->getPlaybackPosition() : 0.0f;
        }
    }

    bool AudioController::setPlaybackPosition(AudioHandle handle, float seconds)
    {
        if (!initialized) return false;

        if (StreamingAudioManager::isStreamingHandle(handle))
        {
            return streamingManager->setPlaybackPosition(handle, seconds);
        }
        else
        {
            AudioSource* source = sourceManager->getSource(handle);
            if (source)
            {
                source->setPlaybackPosition(seconds);
                return true;
            }
            return false;
        }
    }

    float AudioController::getDuration(AudioHandle handle) const
    {
        if (!initialized) return 0.0f;

        if (StreamingAudioManager::isStreamingHandle(handle))
        {
            return streamingManager->getDuration(handle);
        }
        return 0.0f;
    }

    void AudioController::applySettings(const types::AudioSettings& settings)
    {
        if (!initialized) return;
        audioSystem->applySettings(settings);

        if (!settings.busDefinitions.empty())
        {
            busManager->loadBusDefinitions(settings.busDefinitions);
        }

        busManager->setBusVolume("Master", settings.masterVolume);
        busManager->loadSnapshots(settings.mixSnapshots);
    }

    types::AudioSettings AudioController::getCurrentSettings() const
    {
        if (!initialized) return types::AudioSettings::createDefault();
        return audioSystem->getCurrentSettings();
    }

    // === Audio Buses ===

    void AudioController::createBus(const std::string& busName, const std::string& parentName)
    {
        if (!initialized) return;
        busManager->createBus(busName, parentName);
    }

    void AudioController::setBusVolume(const std::string& busName, float volume)
    {
        if (!initialized) return;
        busManager->setBusVolume(busName, volume);
    }

    void AudioController::setBusMuted(const std::string& busName, bool muted)
    {
        if (!initialized) return;
        busManager->setBusMuted(busName, muted);
    }

    void AudioController::setBusSoloed(const std::string& busName, bool soloed)
    {
        if (!initialized) return;
        busManager->setBusSoloed(busName, soloed);
    }

    float AudioController::getBusVolume(const std::string& busName) const
    {
        if (!initialized) return 1.0f;
        return busManager->getBusVolume(busName);
    }

    bool AudioController::isBusMuted(const std::string& busName) const
    {
        if (!initialized) return false;
        return busManager->isBusMuted(busName);
    }

    std::vector<std::string> AudioController::getBusNames() const
    {
        if (!initialized) return {};
        return busManager->getBusNames();
    }

    void AudioController::saveMixSnapshot(const std::string& name)
    {
        if (!initialized) return;
        busManager->saveSnapshot(name);
    }

    void AudioController::loadMixSnapshot(const std::string& name)
    {
        if (!initialized) return;
        busManager->loadSnapshot(name);
    }

    void AudioController::deleteMixSnapshot(const std::string& name)
    {
        if (!initialized) return;
        busManager->deleteSnapshot(name);
    }

    std::vector<std::string> AudioController::getSnapshotNames() const
    {
        if (!initialized) return {};
        return busManager->getSnapshotNames();
    }

    void AudioController::unloadAudioBuffer(const std::string& path)
    {
        if (!initialized || !bufferManager) return;
        bufferManager->unloadBuffer(path);
    }
}
