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

        // The AL-facing half of a play request. Was written out field-by-field in both the
        // streaming and pooled branches of PlaySoundCmd; VK-1515's revive path would have
        // made that a third identical copy, so it is one function now. PlaySoundParams
        // carries what OpenAL cannot (bus, priority, streaming), which is why the two
        // structs stay distinct rather than merging.
        AudioSourceConfig configFromParams(const PlaySoundParams& p)
        {
            AudioSourceConfig config;
            config.volume = p.volume;
            config.pitch = p.pitch;
            config.loop = p.loop;
            config.is3D = p.is3D;
            config.position = p.position;
            config.minDistance = p.minDistance;
            config.maxDistance = p.maxDistance;
            config.rolloffFactor = p.rolloffFactor;
            config.enableDistanceFilter = p.enableDistanceFilter;
            config.filterStartDistance = p.filterStartDistance;
            config.filterMaxDistance = p.filterMaxDistance;
            config.filterIntensity = p.filterIntensity;
            config.innerConeAngle = p.innerConeAngle;
            config.outerConeAngle = p.outerConeAngle;
            config.outerConeGain = p.outerConeGain;
            config.direction = p.direction;
            return config;
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

            // VK-1515: advance the simulated clocks and retire anything that ran out, then
            // move voices between real and virtual as the scene changes. Both run before
            // publishSnapshot so the snapshot reports this tick's outcome, and both mutate
            // activeHandles — which publishSnapshot iterates, so they must not overlap it.
            updateVirtualClocks(deltaTime);
            rebalanceRealVirtual(deltaTime);

            // Publish state snapshot for main-thread queries
            publishSnapshot(deltaTime);

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
                ALuint bufferId = 0;
                if (command.params.streaming)
                {
                    const AudioSourceConfig config = configFromParams(command.params);

                    handle = deps.streamingManager->playStreaming(command.path, config);
                    if (handle != InvalidAudioHandle)
                    {
                        deps.busManager->assignSource(handle, command.params.busName, command.params.volume);
                    }
                }
                else
                {
                    bufferId = deps.bufferManager->loadBuffer(command.path);
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
                            // VK-1515: the victim is demoted, not destroyed — it keeps its
                            // playback clock and can be revived once it is worth hearing
                            // again. Still an immediate AL release rather than a fade:
                            // startFadeOut holds the pool slot until the ramp finishes, so a
                            // faded steal would leave the incoming voice with nowhere to go
                            // and silently blow the cap. The victim is the least audible of a
                            // full budget and is replaced in the same tick by something
                            // louder, so the cut is masked.
                            demoteVoice(decision.victim);
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
                                    source->applyConfig(configFromParams(command.params));
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
                        // VK-1515: denied by the budget. Keep it on a clock rather than
                        // dropping it outright, so walking toward a sound that lost the
                        // arbitration still brings it in — at the offset it would have
                        // reached, not from the top. Past the virtual ceiling it is dropped,
                        // which is exactly VK-1513's pre-virtualization behaviour.
                        else if (static_cast<int>(virtualVoices.size()) < kDefaultMaxVirtualVoices)
                        {
                            virtualizeVoice(command.preAssignedHandle, command.path,
                                            command.params, bufferId, 0.0f, false);
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
                    if (command.params.streaming)
                    {
                        // VK-1515: but the overlay still has to be able to name it, and
                        // nothing downstream of here remembers what a stream is playing.
                        streamingRecords[command.preAssignedHandle] =
                            StreamingVoiceRecord{command.path, command.params.busName};
                    }
                    else
                    {
                        VoiceRecord rec;
                        rec.internal = handle;
                        rec.bufferId = bufferId;
                        rec.path = command.path;
                        rec.params = command.params;
                        voiceRecords[command.preAssignedHandle] = std::move(rec);
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
                // VK-1515: a virtual voice has no AL source, so pausing it means freezing
                // its clock — otherwise it would silently run on and revive at an offset the
                // game never played.
                if (const auto it = virtualVoices.find(command.handle); it != virtualVoices.end())
                {
                    it->second.paused = true;
                    return;
                }
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
                if (const auto it = virtualVoices.find(command.handle); it != virtualVoices.end())
                {
                    it->second.paused = false;
                    return;
                }
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
                // VK-1515: keep the record in step. A virtual voice is ranked on its params,
                // so a volume change must reach them or it would be judged on its spawn
                // value forever; a pooled one may yet be demoted and replayed from them.
                if (PlaySoundParams* params = findVoiceParams(command.handle))
                    params->volume = command.volume;
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
                // Pitch also scales a virtual voice's simulated clock (updateVirtualClocks).
                if (PlaySoundParams* params = findVoiceParams(command.handle))
                    params->pitch = command.pitch;
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
                    //
                    // VK-1515: this now reaches virtual voices too, and for them it is the
                    // whole mechanism — a virtual voice is only ever revived because its
                    // audibility changed, and its audibility is computed from these params.
                    // Miss this and a virtualized sound attached to a moving emitter could
                    // never come back.
                    if (PlaySoundParams* params = findVoiceParams(command.handle))
                    {
                        params->position = command.position;
                        params->direction = command.direction;
                    }
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
                // VK-1515: seeking a virtual voice moves the clock it will be revived at.
                if (const auto it = virtualVoices.find(command.handle); it != virtualVoices.end())
                {
                    it->second.clock = command.seconds;
                    return;
                }
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
                // VK-1515: nothing to ramp — a virtual voice is already silent. Without this
                // it would leak: resolveHandle gives Invalid, so the guard below skips, and
                // the streaming-only forgetVoice at the end never fires either.
                if (virtualVoices.contains(command.handle))
                {
                    forgetVoice(command.handle);
                    return;
                }
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
                        // Preserve the old immediate EFX/reverb detach while retaining
                        // the bus record long enough to meter the audible fade ramp.
                        deps.busManager->detachSourceRouting(internal);
                        deps.sourceManager->startFadeOut(internal, command.fadeDurationMs);
                    }
                }
                if (StreamingAudioManager::isStreamingHandle(internal))
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
                // VK-1515: the other two registries leak the same way if missed.
                virtualVoices.clear();
                streamingRecords.clear();
            }
            else if constexpr (std::is_same_v<T, SetVoiceDebugCmd>)
            {
                voiceDebugEnabled.store(command.enabled, std::memory_order_relaxed);
                // Publish on the very next tick rather than up to kVoiceDebugInterval later,
                // so opening the overlay does not stare at an empty table first.
                voiceDebugAccum = kVoiceDebugInterval;
                if (!command.enabled)
                {
                    // Drop the rows on the way out: kept, they would be served to the next
                    // reader as if live, and a stale voice list is worse than no voice list.
                    std::unique_lock lock(voiceDebugMutex);
                    voiceDebugRows.clear();
                    voiceDebugRows.shrink_to_fit();
                }
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
            c.priority = rec.params.priority;
            // AL_GAIN already carries userVolume * effectiveBusVolume (applied by
            // AudioBusManager), so bus volume, mute and solo are folded in for free — mute
            // a bus and its voices become stealable. A 2D voice is AL_SOURCE_RELATIVE at
            // the origin, so it is scored at distance 0 and never attenuates.
            //
            // VK-1515: the attenuation is rebuilt from params against the CURRENT distance
            // model rather than a copy cached at play time. That copy went stale the moment
            // ApplySettingsCmd changed the model — and publishSnapshot already overrode it
            // (`attenuation.model = distanceModel`), so the two rankers disagreed. They no
            // longer can.
            c.audibleGain = estimateAudibleGain(
                source->getVolume(), attenFromParams(rec.params, distanceModel),
                rec.params.is3D ? glm::distance(listenerPosition, rec.params.position) : 0.0f);
            live.push_back(c);
        }
        return live;
    }

    // VK-1515. The virtual half of the ranking, on the same footing as gatherLiveVoices().
    std::vector<VoiceCandidate> AudioThread::gatherVirtualVoices() const
    {
        std::vector<VoiceCandidate> out;
        out.reserve(virtualVoices.size());
        for (const auto& [extHandle, v] : virtualVoices)
        {
            VoiceCandidate c;
            c.handle = extHandle;
            c.priority = v.params.priority;
            // No AL source, so no AL_GAIN to read the bus multiplier out of — fold it in by
            // hand exactly as makeCandidate() does for an incoming request. Skip this and a
            // virtual voice on a muted bus scores at full volume and evicts audible sound.
            const float busGain = deps.busManager->getBusEffectiveVolume(v.params.busName);
            c.audibleGain = estimateAudibleGain(
                v.params.volume * busGain,
                attenFromParams(v.params, distanceModel),
                v.params.is3D ? glm::distance(listenerPosition, v.params.position) : 0.0f);
            out.push_back(c);
        }
        return out;
    }

    PlaySoundParams* AudioThread::findVoiceParams(AudioHandle externalHandle)
    {
        if (const auto it = voiceRecords.find(externalHandle); it != voiceRecords.end())
            return &it->second.params;
        if (const auto it = virtualVoices.find(externalHandle); it != virtualVoices.end())
            return &it->second.params;
        return nullptr; // streaming (keeps no params) or an unknown handle
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
        if (const auto it = externalToInternal.find(externalHandle);
            it != externalToInternal.end())
        {
            deps.busManager->removeSource(it->second);
            externalToInternal.erase(it);
        }
        voiceRecords.erase(externalHandle);
        // VK-1515: the sole GC choke point for a handle, so it has to clear every registry
        // that handle could be in. A virtual voice has no externalToInternal entry at all,
        // so nothing above would have touched it.
        virtualVoices.erase(externalHandle);
        streamingRecords.erase(externalHandle);
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

    void AudioThread::virtualizeVoice(AudioHandle externalHandle, std::string path,
                                      const PlaySoundParams& params, ALuint bufferId,
                                      float startClock, bool paused)
    {
        VirtualVoice v;
        v.path = std::move(path);
        v.params = params;
        v.bufferId = bufferId;
        v.clock = startClock;
        v.paused = paused;
        // Length is what lets a non-looping virtual voice retire on its own instead of
        // lingering forever on a clock nobody will ever hear. Unknown length (0) simply
        // means it never self-expires; StopSound still reaps it.
        if (const auto info = deps.bufferManager->getBufferInfo(v.path))
            v.duration = info->durationSeconds;

        virtualVoices[externalHandle] = std::move(v);
        // Still playing as far as the game is concerned — isPlaying() must not start
        // reporting false just because the mixer ran out of room.
        activeHandles.insert(externalHandle);
    }

    void AudioThread::demoteVoice(AudioHandle externalHandle)
    {
        const auto it = voiceRecords.find(externalHandle);
        if (it == voiceRecords.end())
            return;

        const AudioHandle internal = it->second.internal;
        float clock = 0.0f;
        bool paused = false;
        if (const AudioSource* source = deps.sourceManager->getSource(internal))
        {
            // The whole point: where it got to is preserved, so a revive resumes rather
            // than restarts.
            clock = source->getPlaybackPosition();
            paused = source->isPaused();
        }

        VoiceRecord rec = std::move(it->second);
        voiceRecords.erase(it);
        // Deliberately not forgetVoice(): the voice is not going away, it is changing
        // registry, and forgetVoice would drop it out of activeHandles and stop the game
        // seeing it as playing.
        deps.busManager->removeSource(internal);
        deps.sourceManager->releaseSource(internal);
        externalToInternal.erase(externalHandle);

        virtualizeVoice(externalHandle, std::move(rec.path), rec.params, rec.bufferId,
                        clock, paused);
    }

    bool AudioThread::reviveVoice(AudioHandle externalHandle)
    {
        const auto it = virtualVoices.find(externalHandle);
        if (it == virtualVoices.end())
            return false;

        // Re-resolve the buffer by path rather than trusting the cached id. A virtual voice
        // holds no AL source, so nothing stops UnloadBufferCmd (or an asset release) from
        // deleting its buffer while it waits — alDeleteBuffers only refuses buffers that are
        // still attached to one. loadBuffer() returns the cached id when it is still there,
        // so the common revive costs a map lookup and only a genuinely evicted asset pays to
        // decode again. A pooled voice cannot hit this: its source keeps the buffer alive.
        const ALuint bufferId = deps.bufferManager->loadBuffer(it->second.path);
        if (bufferId == 0)
        {
            // The asset is gone for good. Drop the voice rather than retry it every
            // rebalance forever.
            forgetVoice(externalHandle);
            return false;
        }

        // Acquire BEFORE moving anything out of the record: a failed acquire has to leave
        // the virtual voice completely intact so the next rebalance can retry it.
        const AudioHandle internal = deps.sourceManager->acquireSource();
        if (internal == InvalidAudioHandle)
            return false;

        AudioSource* source = deps.sourceManager->getSource(internal);
        if (!source)
        {
            deps.sourceManager->releaseSource(internal);
            return false;
        }

        VirtualVoice v = std::move(it->second);
        virtualVoices.erase(it);
        v.bufferId = bufferId;

        source->setBuffer(v.bufferId);
        source->applyConfig(configFromParams(v.params));
        deps.busManager->assignSource(internal, v.params.busName, v.params.volume);
        // Seek before play, not after: playing first would emit however many milliseconds
        // of the wrong part of the clip the seek takes to land, which is audible as a click.
        source->setPlaybackPosition(v.clock);
        if (!v.paused)
            source->play();

        VoiceRecord rec;
        rec.internal = internal;
        rec.bufferId = v.bufferId;
        rec.path = std::move(v.path);
        rec.params = std::move(v.params);
        voiceRecords[externalHandle] = std::move(rec);
        externalToInternal[externalHandle] = internal;
        return true;
    }

    void AudioThread::updateVirtualClocks(float deltaTime)
    {
        if (virtualVoices.empty())
            return;

        std::vector<AudioHandle> expired;
        for (auto& [extHandle, v] : virtualVoices)
        {
            if (v.paused)
                continue;

            // Pitch is a playback-rate multiplier, so it scales the simulated clock too —
            // without it a half-speed voice would revive at twice the offset it should.
            v.clock += deltaTime * v.params.pitch;
            if (v.clock < 0.0f)
                v.clock = 0.0f;

            if (v.duration <= 0.0f)
                continue; // unknown length: cannot know when it would have ended

            if (v.params.loop)
                v.clock = std::fmod(v.clock, v.duration);
            else if (v.clock >= v.duration)
                expired.push_back(extHandle);
        }

        // A one-shot that ran out while inaudible is simply over — reviving it would play
        // its tail from a clip the listener already "missed".
        for (const AudioHandle h : expired)
            forgetVoice(h);
    }

    void AudioThread::rebalanceRealVirtual(float deltaTime)
    {
        rebalanceAccum += deltaTime;
        if (rebalanceAccum < kRebalanceInterval)
            return;
        rebalanceAccum = 0.0f;

        const int cap = maxRealVoices.load(std::memory_order_relaxed);
        // Nothing virtualized and inside budget: nothing to promote, nothing to shed. This
        // is the steady state for any content that never reaches the cap, and it must cost
        // nothing — gatherLiveVoices() alone is an alGetSourcef per live voice.
        if (virtualVoices.empty() && (cap <= 0 || static_cast<int>(voiceRecords.size()) <= cap))
            return;

        const std::vector<VoiceCandidate> real = gatherLiveVoices();
        const std::vector<VoiceCandidate> virt = gatherVirtualVoices();
        const VoiceRebalance plan = rebalanceVoices(real, virt, cap);

        // Demote first: the promotions below are counting on the slots this frees.
        for (const uint64_t h : plan.demote)
            demoteVoice(h);
        for (const uint64_t h : plan.promote)
            reviveVoice(h); // may decline if the pool refuses; it stays virtual and retries
    }

    bool AudioThread::shouldPublishVoiceDebug(float deltaTime)
    {
        // The gate is the whole point: with the overlay closed this is the only cost the
        // feature has, and it is a relaxed load.
        if (!voiceDebugEnabled.load(std::memory_order_relaxed))
            return false;

        voiceDebugAccum += deltaTime;
        if (voiceDebugAccum < kVoiceDebugInterval)
            return false;
        voiceDebugAccum = 0.0f;
        return true;
    }

    void AudioThread::commitVoiceDebug()
    {
        std::unique_lock lock(voiceDebugMutex);
        // Swap rather than assign: the reader's old buffer becomes next tick's scratch, so
        // its capacity is recycled and a steady-state overlay stops allocating entirely.
        voiceDebugRows.swap(voiceDebugScratch);
    }

    std::vector<types::AudioVoiceRow> AudioThread::getVoiceDebugRows() const
    {
        std::shared_lock lock(voiceDebugMutex);
        return voiceDebugRows;
    }

    void AudioThread::publishSnapshot(float deltaTime)
    {
        int writeIdx = 1 - readIndex.load(std::memory_order_acquire);
        auto& snapshot = snapshots[writeIdx];
        snapshot.sources.clear();

        // Build snapshot using external handles (what the main thread knows)
        std::vector<AudioHandle> finished;
        sourceMeterSamples.clear();
        sourceMeterSamples.reserve(activeHandles.size());

        // VK-1515: the overlay's rows are built inside this loop rather than in a pass of
        // their own — every input they need (liveness, playhead, envelope, distance) is
        // already being computed here, and a second pass would repeat each AL round-trip.
        const bool debugTick = shouldPublishVoiceDebug(deltaTime);
        if (debugTick)
        {
            voiceDebugScratch.clear();
            voiceDebugScratch.reserve(activeHandles.size());
        }
        for (AudioHandle extHandle : activeHandles)
        {
            // VK-1515: virtual voices first, and it has to be first. They have no
            // externalToInternal entry, so resolveHandle returns Invalid, getSource(Invalid)
            // is null, and the pooled branch below reads that as "finished" — which would
            // reap every virtual voice on the tick after it was created.
            if (const auto vit = virtualVoices.find(extHandle); vit != virtualVoices.end())
            {
                const VirtualVoice& v = vit->second;
                AudioStateSnapshot::SourceState state;
                state.playing = !v.paused;
                state.playbackPosition = v.clock;
                state.duration = v.duration;
                snapshot.sources[extHandle] = state;

                if (debugTick)
                {
                    types::AudioVoiceRow row;
                    row.handle = extHandle;
                    row.path = v.path;
                    row.busName = v.params.busName;
                    row.kind = types::AudioVoiceKind::Virtual;
                    row.priority = v.params.priority;
                    row.playing = !v.paused;
                    row.position = v.params.position;
                    row.distance = v.params.is3D
                        ? glm::distance(listenerPosition, v.params.position)
                        : 0.0f;
                    row.playbackPosition = v.clock;
                    // What it WOULD be heard at if revived — the number that explains why
                    // it is still virtual. Same formula as a pooled row, with the bus gain
                    // folded in by hand because there is no AL_GAIN to read it from.
                    row.level = metering::finiteNonNegative(
                        deps.bufferManager->sampleEnvelope(v.bufferId, v.clock)
                        * estimateAudibleGain(
                            v.params.volume * deps.busManager->getBusEffectiveVolume(v.params.busName),
                            attenFromParams(v.params, distanceModel),
                            row.distance));
                    voiceDebugScratch.push_back(std::move(row));
                }
                continue;
            }

            AudioHandle internal = resolveHandle(extHandle);
            AudioStateSnapshot::SourceState state;
            bool sourceFinished = false;
            float sourceRms = 0.0f;

            if (StreamingAudioManager::isStreamingHandle(internal))
            {
                state.playing = deps.streamingManager->isPlaying(internal);
                const StreamingPlaybackMetrics metrics =
                    deps.streamingManager->getPlaybackMetrics(internal);
                state.playbackPosition = metrics.positionSeconds;
                sourceRms = metrics.rms;
                state.duration = deps.streamingManager->getDuration(internal);
                sourceFinished = deps.streamingManager->isFinished(internal);

                // Not for a voice that ended on this very tick: the GC below is about to drop
                // it, and listing a dead sound for another 100ms is exactly the kind of lie
                // that sends someone hunting a bug that is not there.
                if (debugTick && !sourceFinished)
                {
                    if (const auto sit = streamingRecords.find(extHandle);
                        sit != streamingRecords.end())
                    {
                        types::AudioVoiceRow row;
                        row.handle = extHandle;
                        row.path = sit->second.path;
                        row.busName = sit->second.busName;
                        row.kind = types::AudioVoiceKind::Stream;
                        row.playing = state.playing;
                        row.playbackPosition = state.playbackPosition;
                        // No distance term: a stream is AL_SOURCE_RELATIVE and never
                        // attenuates (see SetSourceTransformCmd). Its AL_GAIN still carries
                        // userVolume * effectiveBusVolume, so scaling the chunk RMS by it
                        // puts the row on the same footing as a pooled voice's.
                        row.level = metering::finiteNonNegative(
                            metrics.rms * deps.streamingManager->getVolume(internal));
                        voiceDebugScratch.push_back(std::move(row));
                    }
                }
            }
            else
            {
                const AudioSource* source = deps.sourceManager->getSource(internal);
                if (source)
                {
                    state.playing = source->isPlaying();
                    state.playbackPosition = source->getPlaybackPosition();
                    sourceFinished = source->isStopped();

                    const auto recordIt = voiceRecords.find(extHandle);
                    if (recordIt != voiceRecords.end())
                    {
                        const VoiceRecord& record = recordIt->second;
                        const float envelope = deps.bufferManager->sampleEnvelope(
                            record.bufferId, state.playbackPosition);
                        const AttenuationParams attenuation =
                            attenFromParams(record.params, distanceModel);
                        const float distance = record.params.is3D
                            ? glm::distance(listenerPosition, record.params.position)
                            : 0.0f;
                        const float fadeGain = deps.sourceManager->getFadeGain(internal);
                        sourceRms = envelope
                            * estimateAudibleGain(1.0f, attenuation, distance)
                            * fadeGain;

                        if (debugTick && !sourceFinished)
                        {
                            types::AudioVoiceRow row;
                            row.handle = extHandle;
                            row.path = record.path;
                            row.busName = record.params.busName;
                            row.kind = record.params.is3D ? types::AudioVoiceKind::Sound3D
                                                          : types::AudioVoiceKind::Sound2D;
                            row.priority = record.params.priority;
                            row.playing = state.playing;
                            row.position = record.params.position;
                            row.distance = distance;
                            row.playbackPosition = state.playbackPosition;
                            // "What you hear", which is NOT sourceRms above: that one feeds
                            // the bus meters and so deliberately omits the source's own gain
                            // (the bus applies userVolume itself, downstream). Here AL_GAIN
                            // is exactly what we want folded in — it carries userVolume *
                            // effectiveBusVolume, so muting a bus drops the row to 0 while it
                            // still visibly holds its slot, which is the point of the view.
                            //
                            // Deliberately NOT scaled by fadeGain, unlike sourceRms: a fading
                            // voice's AL_GAIN has ALREADY been ramped down by updateFades
                            // (it writes baseVolume * t straight into it), and getFadeGain
                            // returns that same t. Applying both squares the ramp. sourceRms
                            // needs the explicit term only because it passes 1.0f and so
                            // never reads AL_GAIN at all. gatherLiveVoices makes the same
                            // call, which is why a fading voice ranks consistently here and
                            // in the budget.
                            row.level = metering::finiteNonNegative(
                                envelope
                                * estimateAudibleGain(source->getVolume(), attenuation, distance));
                            voiceDebugScratch.push_back(std::move(row));
                        }
                    }
                }
                else
                {
                    finished.push_back(extHandle);
                    continue;
                }
            }

            if (state.playing && !sourceFinished && sourceRms > 0.0f && std::isfinite(sourceRms))
                sourceMeterSamples.push_back({internal, sourceRms});

            snapshot.sources[extHandle] = state;

            // Only GC truly finished sources — paused sources must keep their
            // handle mappings so resume/seek commands still resolve
            if (sourceFinished)
            {
                finished.push_back(extHandle);
            }
        }

        deps.busManager->updateBusMeters(sourceMeterSamples, deltaTime);

        for (AudioHandle h : finished)
        {
            forgetVoice(h); // VK-1513: also drops the voice record, or the budget would leak
        }

        // VK-1513: published for the editor's voice readout. voiceRecords holds exactly the
        // pooled voices the budget governs, so its size IS the real-voice count.
        realVoiceCount.store(static_cast<int>(voiceRecords.size()), std::memory_order_relaxed);
        virtualVoiceCount.store(static_cast<int>(virtualVoices.size()), std::memory_order_relaxed);

        if (debugTick)
            commitVoiceDebug();

        // Swap read index
        readIndex.store(writeIdx, std::memory_order_release);
    }
}
