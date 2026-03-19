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
          , effectManager(std::make_unique<AudioEffectManager>())
          , reverbZoneManager(std::make_unique<ReverbZoneManager>())
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

        // Initialize effect manager and reverb zone manager
        int maxSends = audioSystem->getMaxAuxiliarySends();
        int zoneSend = -1;
        int busEffectSends = maxSends;

        if (maxSends >= 2)
        {
            zoneSend = maxSends - 1;
            busEffectSends = maxSends - 1;
        }

        effectManager->init(busEffectSends);
        reverbZoneManager->init(zoneSend);

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
        busManager->setEffectManager(effectManager.get());
        busManager->setReverbZoneManager(reverbZoneManager.get());
        busManager->setSourceResolveCallback([this](AudioHandle handle) -> ALuint
        {
            if (StreamingAudioManager::isStreamingHandle(handle))
            {
                return streamingManager->getSourceId(handle);
            }
            else
            {
                AudioSource* source = sourceManager->getSource(handle);
                return source ? source->getId() : 0;
            }
        });
        busManager->createDefaultBuses();

        // Create command queue and audio thread
        commandQueue = std::make_unique<AudioCommandQueue>();

        AudioThread::Dependencies deps;
        deps.audioSystem = audioSystem.get();
        deps.sourceManager = sourceManager.get();
        deps.streamingManager = streamingManager.get();
        deps.busManager = busManager.get();
        deps.effectManager = effectManager.get();
        deps.bufferManager = bufferManager.get();
        deps.listener = listener.get();

        audioThread = std::make_unique<AudioThread>(*commandQueue, deps);

        // Release OpenAL context from main thread so audio thread can claim it
        audioSystem->releaseContext();
        audioThread->start();

        initialized = true;
        return true;
    }

    void AudioController::cleanUp()
    {
        if (!initialized)
        {
            return;
        }

        // Signal audio thread to shutdown and wait
        if (commandQueue && audioThread)
        {
            commandQueue->enqueue(ShutdownCmd{});
            audioThread->stop();
            audioThread.reset();
            commandQueue.reset();
        }

        // Audio thread released context before exiting — reclaim on main thread for cleanup
        if (audioSystem)
        {
            audioSystem->acquireContext();
        }

        reverbZoneManager->cleanUp();
        effectManager->cleanUp();
        busManager->cleanUp();
        sourceManager.reset();
        streamingManager.reset();
        bufferManager.reset();
        audioSystem->cleanUp();

        initialized = false;
    }

    void AudioController::update()
    {
        // No-op: all audio work now happens on the dedicated audio thread
    }

    // === Sound Playback (enqueue commands) ===

    AudioHandle AudioController::generateHandle(bool streaming)
    {
        uint64_t id = nextMainThreadHandle.fetch_add(1, std::memory_order_relaxed);
        return streaming ? (id | StreamingHandleBase) : id;
    }

    AudioHandle AudioController::playSound(const std::string& path, const PlaySoundParams& params)
    {
        if (!initialized || !commandQueue) return InvalidAudioHandle;

        AudioHandle handle = generateHandle(params.streaming);
        PlaySoundCmd cmd;
        cmd.preAssignedHandle = handle;
        cmd.path = path;
        cmd.params = params;
        commandQueue->enqueue(std::move(cmd));
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
        PlaySoundParams streamParams = params;
        streamParams.streaming = true;
        return playSound(path, streamParams);
    }

    void AudioController::stopSound(AudioHandle handle)
    {
        if (!initialized || !commandQueue) return;
        commandQueue->enqueue(StopSoundCmd{handle});
    }

    void AudioController::pauseSound(AudioHandle handle)
    {
        if (!initialized || !commandQueue) return;
        commandQueue->enqueue(PauseSoundCmd{handle});
    }

    void AudioController::resumeSound(AudioHandle handle)
    {
        if (!initialized || !commandQueue) return;
        commandQueue->enqueue(ResumeSoundCmd{handle});
    }

    // === Queries (read from snapshot) ===

    bool AudioController::isPlaying(AudioHandle handle) const
    {
        if (!initialized || !audioThread) return false;
        const auto& snapshot = audioThread->getSnapshot();
        auto it = snapshot.sources.find(handle);
        return it != snapshot.sources.end() && it->second.playing;
    }

    void AudioController::setVolume(AudioHandle handle, float volume)
    {
        if (!initialized || !commandQueue) return;
        commandQueue->enqueue(SetVolumeCmd{handle, volume});
    }

    void AudioController::setPitch(AudioHandle handle, float pitch)
    {
        if (!initialized || !commandQueue) return;
        commandQueue->enqueue(SetPitchCmd{handle, pitch});
    }

    void AudioController::stopAll()
    {
        if (!initialized || !commandQueue) return;
        commandQueue->enqueue(StopAllCmd{});
    }

    void AudioController::setListenerPosition(const glm::vec3& position, const glm::vec3& forward,
                                              const glm::vec3& up)
    {
        if (!initialized || !commandQueue) return;
        commandQueue->enqueue(SetListenerCmd{position, forward, up});
    }

    // === Playback Position ===

    float AudioController::getPlaybackPosition(AudioHandle handle) const
    {
        if (!initialized || !audioThread) return 0.0f;
        const auto& snapshot = audioThread->getSnapshot();
        auto it = snapshot.sources.find(handle);
        return it != snapshot.sources.end() ? it->second.playbackPosition : 0.0f;
    }

    bool AudioController::setPlaybackPosition(AudioHandle handle, float seconds)
    {
        if (!initialized || !commandQueue) return false;
        commandQueue->enqueue(SetPlaybackPosCmd{handle, seconds});
        return true;
    }

    float AudioController::getDuration(AudioHandle handle) const
    {
        if (!initialized || !audioThread) return 0.0f;
        const auto& snapshot = audioThread->getSnapshot();
        auto it = snapshot.sources.find(handle);
        return it != snapshot.sources.end() ? it->second.duration : 0.0f;
    }

    // === Audio Settings ===

    void AudioController::applySettings(const types::AudioSettings& settings)
    {
        if (!initialized || !commandQueue) return;
        commandQueue->enqueue(ApplySettingsCmd{settings});
    }

    types::AudioSettings AudioController::getCurrentSettings() const
    {
        if (!initialized) return types::AudioSettings::createDefault();
        // Settings are cached on main thread (not modified by audio thread)
        return audioSystem->getCurrentSettings();
    }

    // === Audio Buses ===

    void AudioController::createBus(const std::string& busName, const std::string& parentName)
    {
        if (!initialized || !commandQueue) return;
        commandQueue->enqueue(CreateBusCmd{busName, parentName});
    }

    void AudioController::setBusVolume(const std::string& busName, float volume)
    {
        if (!initialized || !commandQueue) return;
        commandQueue->enqueue(BusVolumeCmd{busName, volume});
    }

    void AudioController::setBusMuted(const std::string& busName, bool muted)
    {
        if (!initialized || !commandQueue) return;
        commandQueue->enqueue(BusMuteCmd{busName, muted});
    }

    void AudioController::setBusSoloed(const std::string& busName, bool soloed)
    {
        if (!initialized || !commandQueue) return;
        commandQueue->enqueue(BusSoloCmd{busName, soloed});
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
        if (!initialized || !commandQueue) return;
        commandQueue->enqueue(SaveSnapshotCmd{name});
    }

    void AudioController::loadMixSnapshot(const std::string& name)
    {
        if (!initialized || !commandQueue) return;
        commandQueue->enqueue(LoadSnapshotCmd{name});
    }

    void AudioController::deleteMixSnapshot(const std::string& name)
    {
        if (!initialized || !commandQueue) return;
        commandQueue->enqueue(DeleteSnapshotCmd{name});
    }

    std::vector<std::string> AudioController::getSnapshotNames() const
    {
        if (!initialized) return {};
        return busManager->getSnapshotNames();
    }

    // === Audio Effects ===

    bool AudioController::addBusEffect(const std::string& busName, const types::BusEffectConfig& config)
    {
        if (!initialized || !commandQueue) return false;
        commandQueue->enqueue(AddBusEffectCmd{busName, config});
        return true;
    }

    bool AudioController::removeBusEffect(const std::string& busName, uint32_t effectId)
    {
        if (!initialized || !commandQueue) return false;
        commandQueue->enqueue(RemoveBusEffectCmd{busName, effectId});
        return true;
    }

    bool AudioController::updateBusEffect(const std::string& busName, uint32_t effectId,
                                           const types::BusEffectConfig& config)
    {
        if (!initialized || !commandQueue) return false;
        commandQueue->enqueue(UpdateBusEffectCmd{busName, effectId, config});
        return true;
    }

    bool AudioController::setBusEffectEnabled(const std::string& busName, uint32_t effectId, bool enabled)
    {
        if (!initialized || !commandQueue) return false;
        commandQueue->enqueue(SetBusEffectEnabledCmd{busName, effectId, enabled});
        return true;
    }

    bool AudioController::setBusEffectWetDry(const std::string& busName, uint32_t effectId, float wetDry)
    {
        if (!initialized || !commandQueue) return false;
        commandQueue->enqueue(SetBusEffectWetDryCmd{busName, effectId, wetDry});
        return true;
    }

    std::vector<types::BusEffectConfig> AudioController::getBusEffectChain(const std::string& busName) const
    {
        if (!initialized) return {};
        return busManager->getBusEffectChain(busName);
    }

    int AudioController::getMaxEffectsPerBus() const
    {
        if (!initialized) return 0;
        return busManager->getMaxEffectsPerBus();
    }

    void AudioController::unloadAudioBuffer(const std::string& path)
    {
        if (!initialized || !commandQueue) return;
        commandQueue->enqueue(UnloadBufferCmd{path});
    }
}
