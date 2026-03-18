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
        vfLogInfo("AudioThread started");
    }

    void AudioThread::stop()
    {
        if (!running.load()) return;
        running.store(false);
        commandQueue.notify();

        if (thread.joinable())
        {
            thread.join();
        }
        vfLogInfo("AudioThread stopped");
    }

    const AudioStateSnapshot& AudioThread::getSnapshot() const
    {
        return snapshots[readIndex.load(std::memory_order_acquire)];
    }

    void AudioThread::threadLoop()
    {
        // Claim OpenAL context on this thread
        deps.audioSystem->acquireContext();
        vfLogInfo("AudioThread acquired OpenAL context");

        std::vector<AudioCommand> commands;
        commands.reserve(64);

        while (running.load(std::memory_order_relaxed))
        {
            auto now = std::chrono::steady_clock::now();
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
                    // Stop all sources before exiting
                    deps.sourceManager->stopAll();
                    deps.streamingManager->stopAll();
                    deps.audioSystem->releaseContext();
                    vfLogInfo("AudioThread received shutdown, released context");
                    return;
                }

                processCommand(cmd);
            }

            // Per-tick audio update (the work that was on the main thread)
            deps.busManager->flushDirtyVolumes();
            deps.sourceManager->update();
            deps.streamingManager->update();
            deps.sourceManager->updateFilters(listenerPosition, deltaTime);

            // Publish state snapshot for main-thread queries
            publishSnapshot();

            // Sleep until next command or 5ms timeout (~200Hz update rate)
            commandQueue.waitForCommands(std::chrono::milliseconds(5));
        }

        // Fallback cleanup if running was set to false without ShutdownCmd
        deps.sourceManager->stopAll();
        deps.streamingManager->stopAll();
        deps.audioSystem->releaseContext();
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
                    activeHandles.insert(handle);
                command.result.set_value(handle);
            }
            else if constexpr (std::is_same_v<T, StopSoundCmd>)
            {
                if (StreamingAudioManager::isStreamingHandle(command.handle))
                {
                    deps.streamingManager->stop(command.handle);
                }
                else
                {
                    AudioSource* source = deps.sourceManager->getSource(command.handle);
                    if (source) source->stop();
                }
                deps.busManager->removeSource(command.handle);
                if (!StreamingAudioManager::isStreamingHandle(command.handle))
                {
                    deps.sourceManager->releaseSource(command.handle);
                }
                activeHandles.erase(command.handle);
            }
            else if constexpr (std::is_same_v<T, PauseSoundCmd>)
            {
                if (StreamingAudioManager::isStreamingHandle(command.handle))
                    deps.streamingManager->pause(command.handle);
                else
                {
                    AudioSource* source = deps.sourceManager->getSource(command.handle);
                    if (source) source->pause();
                }
            }
            else if constexpr (std::is_same_v<T, ResumeSoundCmd>)
            {
                if (StreamingAudioManager::isStreamingHandle(command.handle))
                    deps.streamingManager->resume(command.handle);
                else
                {
                    AudioSource* source = deps.sourceManager->getSource(command.handle);
                    if (source) source->play();
                }
            }
            else if constexpr (std::is_same_v<T, SetVolumeCmd>)
            {
                deps.busManager->setSourceUserVolume(command.handle, command.volume);
            }
            else if constexpr (std::is_same_v<T, SetPitchCmd>)
            {
                if (StreamingAudioManager::isStreamingHandle(command.handle))
                    deps.streamingManager->setPitch(command.handle, command.pitch);
                else
                {
                    AudioSource* source = deps.sourceManager->getSource(command.handle);
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
                bool result = false;
                if (StreamingAudioManager::isStreamingHandle(command.handle))
                    result = deps.streamingManager->setPlaybackPosition(command.handle, command.seconds);
                else
                {
                    AudioSource* source = deps.sourceManager->getSource(command.handle);
                    if (source)
                    {
                        source->setPlaybackPosition(command.seconds);
                        result = true;
                    }
                }
                command.result.set_value(result);
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
                command.result.set_value(deps.busManager->addBusEffect(command.busName, command.config));
            }
            else if constexpr (std::is_same_v<T, RemoveBusEffectCmd>)
            {
                command.result.set_value(deps.busManager->removeBusEffect(command.busName, command.effectId));
            }
            else if constexpr (std::is_same_v<T, UpdateBusEffectCmd>)
            {
                command.result.set_value(deps.busManager->updateBusEffect(command.busName, command.effectId, command.config));
            }
            else if constexpr (std::is_same_v<T, SetBusEffectEnabledCmd>)
            {
                command.result.set_value(deps.busManager->setBusEffectEnabled(command.busName, command.effectId, command.enabled));
            }
            else if constexpr (std::is_same_v<T, SetBusEffectWetDryCmd>)
            {
                command.result.set_value(deps.busManager->setBusEffectWetDry(command.busName, command.effectId, command.wetDry));
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
            else if constexpr (std::is_same_v<T, ShutdownCmd>)
            {
                // Handled in threadLoop before processCommand
            }
        }, cmd);
    }

    void AudioThread::publishSnapshot()
    {
        int writeIdx = 1 - readIndex.load(std::memory_order_acquire);
        auto& snapshot = snapshots[writeIdx];
        snapshot.sources.clear();

        // Remove handles for sources that finished playing
        std::vector<AudioHandle> finished;
        for (AudioHandle handle : activeHandles)
        {
            AudioStateSnapshot::SourceState state;

            if (StreamingAudioManager::isStreamingHandle(handle))
            {
                state.playing = deps.streamingManager->isPlaying(handle);
                state.playbackPosition = deps.streamingManager->getPlaybackPosition(handle);
                state.duration = deps.streamingManager->getDuration(handle);
            }
            else
            {
                const AudioSource* source = deps.sourceManager->getSource(handle);
                if (source)
                {
                    state.playing = source->isPlaying();
                    state.playbackPosition = source->getPlaybackPosition();
                }
                else
                {
                    finished.push_back(handle);
                    continue;
                }
            }

            snapshot.sources[handle] = state;

            if (!state.playing)
            {
                finished.push_back(handle);
            }
        }

        for (AudioHandle h : finished)
        {
            activeHandles.erase(h);
        }

        // Swap read index
        readIndex.store(writeIdx, std::memory_order_release);
    }
}
