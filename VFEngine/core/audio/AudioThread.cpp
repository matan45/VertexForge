#include "AudioThread.hpp"
#include "print/Log.hpp"
#include <chrono>

namespace core::audio
{
    namespace
    {
        // VK-1513: the AL distance properties a voice is scored against. minDistance is
        // AL_REFERENCE_DISTANCE, maxDistance is AL_MAX_DISTANCE (AudioSource.cpp:258-296).
        AttenuationParams attenFromParams(const PlaySoundParams& p, types::AudioDistanceModel model)
        {
            AttenuationParams a;
            a.model = model;
            a.refDistance = p.minDistance;
            a.maxDistance = p.maxDistance;
            a.rolloffFactor = p.rolloffFactor;
            return a;
        }
    }

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
                        // VK-1513: arbitrate against the real-voice budget before taking a
                        // pool slot. Deliberately AFTER loadBuffer so a failed load can never
                        // cost a live voice its slot; the decode is a cached lookup for every
                        // repeat of a path, so a flood of one-shots sharing an asset pays
                        // nothing extra.
                        //
                        // The size check short-circuits the common under-budget case, where
                        // decidePlay would return Allow regardless: gathering costs an
                        // alGetSourcef round-trip PER LIVE VOICE, so paying it on every play
                        // when there is a free slot would be pure waste. voiceRecords.size()
                        // is an upper bound on live.size() (a record whose source has gone is
                        // skipped), so this can only ever over-estimate and fall through to
                        // decidePlay, which re-checks. `live` is named rather than inlined
                        // because decidePlay takes a span and must not view a dead temporary.
                        const int cap = maxRealVoices.load(std::memory_order_relaxed);
                        VoiceDecision decision{VoiceDecisionKind::Allow, kInvalidVoiceHandle};
                        if (cap > 0 && static_cast<int>(voiceRecords.size()) >= cap)
                        {
                            const std::vector<VoiceCandidate> live = gatherLiveVoices();
                            decision = decidePlay(live, makeCandidate(command), cap);
                        }

                        if (decision.kind == VoiceDecisionKind::Steal)
                        {
                            // Hard release, not a fade: startFadeOut holds the pool slot until
                            // the ramp finishes, so a faded steal would leave the incoming
                            // voice with nowhere to go and silently blow the cap. The victim
                            // is the least audible of a full budget and is replaced in the
                            // same tick by something louder, so the cut is masked.
                            releaseVoice(decision.victim);
                        }

                        if (decision.kind != VoiceDecisionKind::Deny)
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

                                    deps.busManager->assignSource(handle, command.params.busName,
                                                                  command.params.volume);
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
                }
                if (handle != InvalidAudioHandle)
                {
                    externalToInternal[command.preAssignedHandle] = handle;
                    activeHandles.insert(command.preAssignedHandle);

                    // VK-1513: only pooled voices are budgeted. Streaming allocates its own
                    // AL sources and never draws from the pool, so it gets no record and is
                    // never a steal victim.
                    if (!command.params.streaming)
                    {
                        VoiceRecord rec;
                        rec.internal = handle;
                        rec.priority = command.params.priority;
                        rec.is3D = command.params.is3D;
                        rec.position = command.params.position;
                        rec.atten = attenFromParams(command.params, distanceModel);
                        voiceRecords[command.preAssignedHandle] = rec;
                    }
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
                forgetVoice(command.handle);
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
            else if constexpr (std::is_same_v<T, SetSourceTransformCmd>)
            {
                AudioHandle internal = resolveHandle(command.handle);
                if (StreamingAudioManager::isStreamingHandle(internal))
                {
                    // VK-1505: streaming 3D positioning is intentionally a no-op.
                    // StreamingAudioSource::applyConfig ignores is3D/position and never
                    // flips AL_SOURCE_RELATIVE, so a streaming source is listener-relative
                    // and per-frame position would be meaningless. Full streaming-3D
                    // spatialization is a separate follow-up ticket.
                }
                else
                {
                    AudioSource* source = deps.sourceManager->getSource(internal);
                    if (source)
                    {
                        source->setPosition(command.position);
                        source->setDirection(command.direction);
                        source->setVelocity(command.velocity);
                    }
                    // VK-1513: keep the record in step, or a voice that moved after it
                    // started would be scored at its spawn point. Note the listener-moved
                    // half needs no command — gatherLiveVoices() reads listenerPosition,
                    // which SetListenerCmd refreshes unconditionally every frame.
                    if (auto it = voiceRecords.find(command.handle); it != voiceRecords.end())
                        it->second.position = command.position;
                }
            }
            else if constexpr (std::is_same_v<T, SetListenerCmd>)
            {
                listenerPosition = command.position;
                deps.listener->setPosition(command.position);
                deps.listener->setOrientation(command.forward, command.up);
                deps.listener->setVelocity(command.velocity);
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

                // VK-1513. Cached here rather than read back from AudioSystem per tick,
                // because getCurrentSettings() returns by value and would deep-copy the bus
                // and snapshot vectors. Lowering the cap below the live count deliberately
                // does nothing to live voices — it gates new acquisitions only and lets them
                // drain, since mass-stopping voices is an audible cutout.
                maxRealVoices.store(command.settings.maxRealVoices, std::memory_order_relaxed);
                distanceModel = command.settings.distanceModel;
                deps.sourceManager->setMaxVoices(command.settings.maxRealVoices);
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
                forgetVoice(command.handle);
            }
            else if constexpr (std::is_same_v<T, StopAllCmd>)
            {
                deps.sourceManager->stopAll();
                deps.streamingManager->stopAll();
                activeHandles.clear();
                // Drive-by: externalToInternal was never cleared here, so it grew without
                // bound and kept stale mappings alive. Unreachable today (stopAll() has no
                // callers), but the voice records must not leak either.
                externalToInternal.clear();
                voiceRecords.clear();
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
        // Drive-by: this used to fall back to the raw external handle. External handles
        // (AudioController::nextMainThreadHandle) and internal ones (AudioSourceManager::
        // nextHandleId) share a numeric space, both starting at 1, so an unmapped handle
        // resolved to an arbitrary internal one. It is benign today only by an accident:
        // acquireSource() has exactly one call site, inside PlaySoundCmd, so internal can
        // never overtake external and the fallback always lands on a not-yet-minted handle.
        // Returning Invalid is behaviour-preserving (every consumer already no-ops on it:
        // isStreamingHandle(0) is false, getSource(0) is nullptr, releaseSource(0) returns
        // early), it revives the dead guard in FadeOutAndReleaseCmd, and it removes an
        // undocumented invariant that the virtualization follow-up would break — a revive
        // acquires a source with no matching play.
        return it != externalToInternal.end() ? it->second : InvalidAudioHandle;
    }

    std::vector<VoiceCandidate> AudioThread::gatherLiveVoices() const
    {
        std::vector<VoiceCandidate> live;
        live.reserve(voiceRecords.size());
        for (const auto& [extHandle, rec] : voiceRecords)
        {
            const AudioSource* source = deps.sourceManager->getSource(rec.internal);
            if (!source)
                continue;

            VoiceCandidate c;
            c.handle = extHandle;
            c.priority = rec.priority;
            // AL_GAIN already carries userVolume * effectiveBusVolume (applied by
            // AudioBusManager), so bus volume, mute and solo are folded in for free — mute
            // a bus and its voices become stealable. A 2D voice is AL_SOURCE_RELATIVE at
            // the origin, so it is scored at distance 0 and never attenuates.
            c.audibleGain = estimateAudibleGain(
                source->getVolume(), rec.atten,
                rec.is3D ? glm::distance(listenerPosition, rec.position) : 0.0f);
            live.push_back(c);
        }
        return live;
    }

    VoiceCandidate AudioThread::makeCandidate(const PlaySoundCmd& command) const
    {
        VoiceCandidate c;
        c.handle = command.preAssignedHandle;
        c.priority = command.params.priority;
        // The incoming voice has no AL source yet, so fold in the bus multiplier by hand to
        // put it on the same footing as gatherLiveVoices()' AL_GAIN reads. Without this a
        // sound on a muted bus would score at full volume and steal a slot.
        const float busGain = deps.busManager->getBusEffectiveVolume(command.params.busName);
        c.audibleGain = estimateAudibleGain(
            command.params.volume * busGain,
            attenFromParams(command.params, distanceModel),
            command.params.is3D ? glm::distance(listenerPosition, command.params.position) : 0.0f);
        return c;
    }

    void AudioThread::forgetVoice(AudioHandle externalHandle)
    {
        activeHandles.erase(externalHandle);
        externalToInternal.erase(externalHandle);
        voiceRecords.erase(externalHandle);
    }

    void AudioThread::releaseVoice(AudioHandle externalHandle)
    {
        auto it = voiceRecords.find(externalHandle);
        if (it == voiceRecords.end())
            return;

        const AudioHandle internal = it->second.internal;
        // Mirrors StopSoundCmd's hard-stop path: releaseSource() stops the source and
        // returns its pool slot immediately, which is what makes the slot available to the
        // incoming voice in this same command.
        deps.busManager->removeSource(internal);
        deps.sourceManager->releaseSource(internal);
        forgetVoice(externalHandle);
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
            forgetVoice(h); // VK-1513: also drops the voice record, or the budget would leak
        }

        // VK-1513: published for the editor's voice readout. voiceRecords holds exactly the
        // pooled voices the budget governs, so its size IS the real-voice count.
        realVoiceCount.store(static_cast<int>(voiceRecords.size()), std::memory_order_relaxed);

        // Swap read index
        readIndex.store(writeIdx, std::memory_order_release);
    }
}
