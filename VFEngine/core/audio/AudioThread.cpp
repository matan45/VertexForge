#include "AudioThread.hpp"
#include "print/Log.hpp"
#include <chrono>

namespace core::audio
{
    AudioThread::AudioThread(AudioCommandQueue& commandQueue, Dependencies deps)
        : commandQueue(commandQueue)
        , deps(deps)
        , lastUpdateTime(std::chrono::steady_clock::now())
    {
    }

    AudioThread::~AudioThread()
    {
        stop();
    }

    void AudioThread::start()
    {
        if (running.load()) return;
        running.store(true);
        thread = std::thread(&AudioThread::threadLoop, this);
        vfLogDebug("AudioThread started");
    }

    void AudioThread::stop()
    {
        // ShutdownCmd is the single authority that exits the thread loop.
        // This method only joins; enqueue ShutdownCmd before calling stop().
        if (thread.joinable())
        {
            thread.join();
        }
        running.store(false);
        vfLogDebug("AudioThread stopped");
    }

    AudioStateSnapshot AudioThread::getSnapshot() const
    {
        // Return by value to avoid TOCTOU: the caller gets a consistent copy
        // even if the audio thread flips the index during the caller's reads
        return snapshots[readIndex.load(std::memory_order_acquire)];
    }

    void AudioThread::threadLoop()
    {
        // Claim OpenAL context on this thread
        deps.audioSystem->acquireContext();
        vfLogDebug("AudioThread acquired OpenAL context");

        std::vector<AudioCommand> commands;
        commands.reserve(64);

        while (running.load(std::memory_order_relaxed))
        {
            auto now = std::chrono::steady_clock::now();
            // VK-992: audio runs on real (unscaled) wall-clock by design — UI sounds,
            // music, and SFX stay real-time during slow-mo / freeze. Scaled/pitched
            // gameplay SFX is a deferred follow-up (needs per-voice gameplay-vs-UI tagging).
            float deltaTime = std::chrono::duration<float>(now - lastUpdateTime).count();
            lastUpdateTime = now;

            // Drain and process all pending commands
            commands.clear();
            commandQueue.tryDequeueAll(commands);

            for (auto& cmd : commands)
            {
                // Check for shutdown
                if (std::holds_alternative<ShutdownCmd>(cmd))
                {
                    deps.sourceManager->stopAll();
                    deps.streamingManager->stopAll();
                    // Release context from audio thread so main thread can reclaim it for cleanup
                    deps.audioSystem->releaseContext();
                    running.store(false, std::memory_order_release);
                    vfLogInfo("AudioThread received shutdown");
                    return;
                }

                processCommand(cmd);
            }

            // Per-tick audio update (the work that was on the main thread)
            deps.busManager->flushDirtyVolumes();
            deps.sourceManager->update();
            deps.streamingManager->update();
            deps.sourceManager->updateFilters(listenerPosition, deltaTime);
            deps.sourceManager->updateFades(deltaTime * 1000.0f);

            // Publish state snapshot for main-thread queries
            publishSnapshot();

            // Sleep until next command or 5ms timeout (~200Hz update rate)
            commandQueue.waitForCommands(std::chrono::milliseconds(5));
        }

        // Loop exited via ShutdownCmd — cleanup already done there
    }

    void AudioThread::processCommand(AudioCommand& cmd)
    {
        std::visit([this](auto& command)
        {
            using T = std::decay_t<decltype(command)>;

            if constexpr (std::is_same_v<T, PlaySoundCmd>)
            {
                AudioHandle handle = InvalidAudioHandle;
                if (command.params.streaming)
                {
                    AudioSourceConfig config;
                    config.volume = command.params.volume;
                    config.pitch = command.params.pitch;
                    config.loop = command.params.loop;
                    config.is3D = command.params.is3D;
                    config.position = command.params.position;
                    config.minDistance = command.params.minDistance;
                    config.maxDistance = command.params.maxDistance;
                    config.rolloffFactor = command.params.rolloffFactor;
                    config.enableDistanceFilter = command.params.enableDistanceFilter;
                    config.filterStartDistance = command.params.filterStartDistance;
                    config.filterMaxDistance = command.params.filterMaxDistance;
                    config.filterIntensity = command.params.filterIntensity;
                    config.innerConeAngle = command.params.innerConeAngle;
                    config.outerConeAngle = command.params.outerConeAngle;
                    config.outerConeGain = command.params.outerConeGain;
                    config.direction = command.params.direction;

                    handle = deps.streamingManager->playStreaming(command.path, config);
                    if (handle != InvalidAudioHandle)
                    {
                        deps.busManager->assignSource(handle, command.params.busName, command.params.volume);
                    }
                }
                else
                {
                    ALuint bufferId = deps.bufferManager->loadBuffer(command.path);
                    if (bufferId != 0)
                    {
                        handle = deps.sourceManager->acquireSource();
                        if (handle != InvalidAudioHandle)
                        {
                            AudioSource* source = deps.sourceManager->getSource(handle);
                            if (source)
                            {
                                source->setBuffer(bufferId);

                                AudioSourceConfig config;
                                config.volume = command.params.volume;
                                config.pitch = command.params.pitch;
                                config.loop = command.params.loop;
                                config.is3D = command.params.is3D;
                                config.position = command.params.position;
                                config.minDistance = command.params.minDistance;
                                config.maxDistance = command.params.maxDistance;
                                config.rolloffFactor = command.params.rolloffFactor;
                                config.enableDistanceFilter = command.params.enableDistanceFilter;
                                config.filterStartDistance = command.params.filterStartDistance;
                                config.filterMaxDistance = command.params.filterMaxDistance;
                                config.filterIntensity = command.params.filterIntensity;
                                config.innerConeAngle = command.params.innerConeAngle;
                                config.outerConeAngle = command.params.outerConeAngle;
                                config.outerConeGain = command.params.outerConeGain;
                                config.direction = command.params.direction;
                                source->applyConfig(config);

                                deps.busManager->assignSource(handle, command.params.busName, command.params.volume);
                                source->play();
                            }
                            else
                            {
                                deps.sourceManager->releaseSource(handle);
                                handle = InvalidAudioHandle;
                            }
                        }
                    }
                }
                if (handle != InvalidAudioHandle)
                {
                    externalToInternal[command.preAssignedHandle] = handle;
                    activeHandles.insert(command.preAssignedHandle);
                }
            }
            else if constexpr (std::is_same_v<T, StopSoundCmd>)
            {
                AudioHandle internal = resolveHandle(command.handle);
                if (StreamingAudioManager::isStreamingHandle(internal))
                {
                    deps.streamingManager->stop(internal);
                }
                else
                {
                    AudioSource* source = deps.sourceManager->getSource(internal);
                    if (source) source->stop();
                }
                deps.busManager->removeSource(internal);
                if (!StreamingAudioManager::isStreamingHandle(internal))
                {
                    deps.sourceManager->releaseSource(internal);
                }
                activeHandles.erase(command.handle);
                externalToInternal.erase(command.handle);
            }
            else if constexpr (std::is_same_v<T, PauseSoundCmd>)
            {
                AudioHandle internal = resolveHandle(command.handle);
                if (StreamingAudioManager::isStreamingHandle(internal))
                    deps.streamingManager->pause(internal);
                else
                {
                    AudioSource* source = deps.sourceManager->getSource(internal);
                    if (source) source->pause();
                }
            }
            else if constexpr (std::is_same_v<T, ResumeSoundCmd>)
            {
                AudioHandle internal = resolveHandle(command.handle);
                if (StreamingAudioManager::isStreamingHandle(internal))
                    deps.streamingManager->resume(internal);
                else
                {
                    AudioSource* source = deps.sourceManager->getSource(internal);
                    if (source) source->play();
                }
            }
            else if constexpr (std::is_same_v<T, SetVolumeCmd>)
            {
                AudioHandle internal = resolveHandle(command.handle);
                deps.busManager->setSourceUserVolume(internal, command.volume);
            }
            else if constexpr (std::is_same_v<T, SetPitchCmd>)
            {
                AudioHandle internal = resolveHandle(command.handle);
                if (StreamingAudioManager::isStreamingHandle(internal))
                    deps.streamingManager->setPitch(internal, command.pitch);
                else
                {
                    AudioSource* source = deps.sourceManager->getSource(internal);
                    if (source) source->setPitch(command.pitch);
                }
            }
            else if constexpr (std::is_same_v<T, SetListenerCmd>)
            {
                listenerPosition = command.position;
                deps.listener->setPosition(command.position);
                deps.listener->setOrientation(command.forward, command.up);
            }
            else if constexpr (std::is_same_v<T, SetPlaybackPosCmd>)
            {
                AudioHandle internal = resolveHandle(command.handle);
                if (StreamingAudioManager::isStreamingHandle(internal))
                    deps.streamingManager->setPlaybackPosition(internal, command.seconds);
                else
                {
                    AudioSource* source = deps.sourceManager->getSource(internal);
                    if (source)
                        source->setPlaybackPosition(command.seconds);
                }
            }
            else if constexpr (std::is_same_v<T, ApplySettingsCmd>)
            {
                deps.audioSystem->applySettings(command.settings);
            }
            else if constexpr (std::is_same_v<T, BusVolumeCmd>)
            {
                deps.busManager->setBusVolume(command.busName, command.volume);
            }
            else if constexpr (std::is_same_v<T, BusMuteCmd>)
            {
                deps.busManager->setBusMuted(command.busName, command.muted);
            }
            else if constexpr (std::is_same_v<T, BusSoloCmd>)
            {
                deps.busManager->setBusSoloed(command.busName, command.soloed);
            }
            else if constexpr (std::is_same_v<T, CreateBusCmd>)
            {
                deps.busManager->createBus(command.name, command.parentName);
            }
            else if constexpr (std::is_same_v<T, AddBusEffectCmd>)
            {
                deps.busManager->addBusEffect(command.busName, command.config);
            }
            else if constexpr (std::is_same_v<T, RemoveBusEffectCmd>)
            {
                deps.busManager->removeBusEffect(command.busName, command.effectId);
            }
            else if constexpr (std::is_same_v<T, UpdateBusEffectCmd>)
            {
                deps.busManager->updateBusEffect(command.busName, command.effectId, command.config);
            }
            else if constexpr (std::is_same_v<T, SetBusEffectEnabledCmd>)
            {
                deps.busManager->setBusEffectEnabled(command.busName, command.effectId, command.enabled);
            }
            else if constexpr (std::is_same_v<T, SetBusEffectWetDryCmd>)
            {
                deps.busManager->setBusEffectWetDry(command.busName, command.effectId, command.wetDry);
            }
            else if constexpr (std::is_same_v<T, LoadSnapshotCmd>)
            {
                deps.busManager->loadSnapshot(command.name);
            }
            else if constexpr (std::is_same_v<T, SaveSnapshotCmd>)
            {
                deps.busManager->saveSnapshot(command.name);
            }
            else if constexpr (std::is_same_v<T, DeleteSnapshotCmd>)
            {
                deps.busManager->deleteSnapshot(command.name);
            }
            else if constexpr (std::is_same_v<T, UnloadBufferCmd>)
            {
                deps.bufferManager->unloadBuffer(command.path);
            }
            else if constexpr (std::is_same_v<T, FadeOutAndReleaseCmd>)
            {
                AudioHandle internal = resolveHandle(command.handle);
                if (internal != InvalidAudioHandle)
                {
                    if (StreamingAudioManager::isStreamingHandle(internal))
                    {
                        // Streaming sources cannot be faded — stop immediately
                        deps.streamingManager->stop(internal);
                    }
                    else
                    {
                        // Unroute from reverb before fading
                        if (deps.reverbZoneManager)
                        {
                            AudioSource* source = deps.sourceManager->getSource(internal);
                            if (source)
                                deps.reverbZoneManager->unrouteSource(source->getId());
                        }
                        deps.busManager->removeSource(internal);
                        deps.sourceManager->startFadeOut(internal, command.fadeDurationMs);
                    }
                }
                activeHandles.erase(command.handle);
                externalToInternal.erase(command.handle);
            }
            else if constexpr (std::is_same_v<T, StopAllCmd>)
            {
                deps.sourceManager->stopAll();
                deps.streamingManager->stopAll();
                activeHandles.clear();
            }
            else if constexpr (std::is_same_v<T, ShutdownCmd>)
            {
                // Handled in threadLoop before processCommand
            }
        }, cmd);
    }

    AudioHandle AudioThread::resolveHandle(AudioHandle externalHandle) const
    {
        auto it = externalToInternal.find(externalHandle);
        return it != externalToInternal.end() ? it->second : externalHandle;
    }

    void AudioThread::publishSnapshot()
    {
        int writeIdx = 1 - readIndex.load(std::memory_order_acquire);
        auto& snapshot = snapshots[writeIdx];
        snapshot.sources.clear();

        // Build snapshot using external handles (what the main thread knows)
        std::vector<AudioHandle> finished;
        for (AudioHandle extHandle : activeHandles)
        {
            AudioHandle internal = resolveHandle(extHandle);
            AudioStateSnapshot::SourceState state;
            bool sourceFinished = false;

            if (StreamingAudioManager::isStreamingHandle(internal))
            {
                state.playing = deps.streamingManager->isPlaying(internal);
                state.playbackPosition = deps.streamingManager->getPlaybackPosition(internal);
                state.duration = deps.streamingManager->getDuration(internal);
                sourceFinished = deps.streamingManager->isFinished(internal);
            }
            else
            {
                const AudioSource* source = deps.sourceManager->getSource(internal);
                if (source)
                {
                    state.playing = source->isPlaying();
                    state.playbackPosition = source->getPlaybackPosition();
                    sourceFinished = source->isStopped();
                }
                else
                {
                    finished.push_back(extHandle);
                    continue;
                }
            }

            snapshot.sources[extHandle] = state;

            // Only GC truly finished sources — paused sources must keep their
            // handle mappings so resume/seek commands still resolve
            if (sourceFinished)
            {
                finished.push_back(extHandle);
            }
        }

        for (AudioHandle h : finished)
        {
            activeHandles.erase(h);
            externalToInternal.erase(h);
        }

        // Swap read index
        readIndex.store(writeIdx, std::memory_order_release);
    }
}
