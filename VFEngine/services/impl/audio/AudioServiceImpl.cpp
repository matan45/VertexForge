#include "AudioServiceImpl.hpp"
#include "../../events/audio/AudioEvents.hpp"
#include "../../events/audio/AudioSettingsEvents.hpp"
#include "../../events/audio/AudioBusEvents.hpp"
#include "../../events/audio/AudioEffectEvents.hpp"
#include "../../events/audio/AudioSnapshotEvents.hpp"
#include "../../events/EventDispatcher.hpp"
#include <cassert>

namespace services {

    AudioServiceImpl::AudioServiceImpl(IAudioProvider* audioProvider)
        : audioProvider(audioProvider) {
        assert(audioProvider && "AudioProvider must not be null");
    }

    AudioServiceImpl::~AudioServiceImpl() = default;

    void AudioServiceImpl::registerEventHandlers() {
        auto& dispatcher = ::events::EventDispatcher::instance();

        // Listener Commands
        dispatcher.registerCommandHandler<events::audio::SetListenerPositionCommand>(
            [this](const auto& cmd) {
                setListenerPosition(cmd.position, cmd.forward, cmd.up, cmd.velocity);
                // VK-1511: cache the pose so the editor's 3D-audition mode can read it
                // back on the main thread (race-free — this handler is the sole writer).
                cachedListener = {cmd.position, cmd.forward, cmd.up, /*valid=*/true};
            });

        dispatcher.registerQueryHandler<events::audio::GetListenerStateQuery>(
            [this](const auto&) {
                return cachedListener;
            });

        // Sound Playback Commands
        dispatcher.registerCommandHandler<events::audio::PlaySound3DCommand>(
            [this](const auto& cmd) {
                return playSound3D(cmd.path, cmd.position, cmd.params);
            });

        dispatcher.registerCommandHandler<events::audio::StopSoundCommand>(
            [this](const auto& cmd) {
                stopSound(cmd.handle);
            });

        dispatcher.registerCommandHandler<events::audio::FadeOutAndReleaseSoundCommand>(
            [this](const auto& cmd) {
                if (audioProvider)
                    audioProvider->fadeOutAndRelease(cmd.handle.id, cmd.fadeDurationMs);
            });

        dispatcher.registerCommandHandler<::events::audio::snapshot::FadeOutAudioCommand>(
            [this](const ::events::audio::snapshot::FadeOutAudioCommand& cmd) {
                if (audioProvider)
                    audioProvider->fadeOutAndRelease(cmd.handleId, cmd.fadeDurationMs);
            });

        dispatcher.registerCommandHandler<::events::audio::snapshot::PlayRestoredAudio3DCommand>(
            [this](const ::events::audio::snapshot::PlayRestoredAudio3DCommand& cmd) -> uint64_t {
                if (!audioProvider) return 0;
                AudioPlayParams params;
                params.volume = cmd.volume;
                params.pitch = cmd.pitch;
                params.loop = cmd.loop;
                params.is3D = true;
                params.minDistance = cmd.minDistance;
                params.maxDistance = cmd.maxDistance;
                params.busName = cmd.busName;
                params.priority = cmd.priority;
                return audioProvider->playSound3D(cmd.path, cmd.position, params);
            });

        dispatcher.registerCommandHandler<::events::audio::snapshot::PlayRestoredAudio2DCommand>(
            [this](const ::events::audio::snapshot::PlayRestoredAudio2DCommand& cmd) -> uint64_t {
                if (!audioProvider) return 0;
                AudioPlayParams params;
                params.volume = cmd.volume;
                params.pitch = cmd.pitch;
                params.loop = cmd.loop;
                params.busName = cmd.busName;
                params.priority = cmd.priority;
                return audioProvider->playStreamingSound(cmd.path, params);
            });

        dispatcher.registerQueryHandler<::events::audio::snapshot::GetAudioPlaybackPositionQuery>(
            [this](const ::events::audio::snapshot::GetAudioPlaybackPositionQuery& query) -> float {
                if (!audioProvider) return 0.0f;
                return audioProvider->getPlaybackPosition(query.handleId);
            });

        dispatcher.registerQueryHandler<::events::audio::snapshot::IsAudioPlayingQuery>(
            [this](const ::events::audio::snapshot::IsAudioPlayingQuery& query) -> bool {
                if (!audioProvider) return false;
                return audioProvider->isPlaying(query.handleId);
            });

        dispatcher.registerCommandHandler<::events::audio::snapshot::SeekAudioCommand>(
            [this](const ::events::audio::snapshot::SeekAudioCommand& cmd) {
                if (audioProvider)
                    audioProvider->setPlaybackPosition(cmd.handleId, cmd.seconds);
            });

        dispatcher.registerCommandHandler<events::audio::PauseSoundCommand>(
            [this](const auto& cmd) {
                pauseSound(cmd.handle);
            });

        dispatcher.registerCommandHandler<events::audio::ResumeSoundCommand>(
            [this](const auto& cmd) {
                resumeSound(cmd.handle);
            });

        dispatcher.registerCommandHandler<events::audio::SetSoundVolumeCommand>(
            [this](const auto& cmd) {
                setVolume(cmd.handle, cmd.volume);
            });

        dispatcher.registerCommandHandler<events::audio::SetSoundPitchCommand>(
            [this](const auto& cmd) {
                setPitch(cmd.handle, cmd.pitch);
            });

        // VK-1505: per-frame emitter transform re-sync. Engine-internal loop (driven by
        // AudioSceneUpdater), not a script-facing verb, so it calls the provider directly
        // rather than adding a method to IAudioService (mirrors the snapshot handlers above).
        dispatcher.registerCommandHandler<events::audio::SetSoundTransformCommand>(
            [this](const auto& cmd) {
                if (audioProvider)
                    audioProvider->setSourceTransform(cmd.handle.id, cmd.position,
                                                      cmd.direction, cmd.velocity);
            });

        // VK-1518: per-emitter geometry-occlusion verdict. Engine-internal loop (driven by
        // AudioSceneUpdater's raycast), so it calls the provider directly for the same
        // reason SetSoundTransformCommand does.
        dispatcher.registerCommandHandler<events::audio::SetSoundOcclusionCommand>(
            [this](const auto& cmd) {
                if (audioProvider)
                    audioProvider->setSourceOcclusion(cmd.handle.id, cmd.occlusion,
                                                      cmd.lpfAmount, cmd.volumeAmount);
            });

        // Streaming Audio Commands
        dispatcher.registerCommandHandler<events::audio::PlayStreamingSoundCommand>(
            [this](const auto& cmd) {
                return playStreamingSound(cmd.path, cmd.params);
            });

        dispatcher.registerCommandHandler<events::audio::SetPlaybackPositionCommand>(
            [this](const auto& cmd) {
                return setPlaybackPosition(cmd.handle, cmd.seconds);
            });

        // Queries
        dispatcher.registerQueryHandler<events::audio::IsSoundPlayingQuery>(
            [this](const auto& query) {
                return isPlaying(query.handle);
            });

        dispatcher.registerQueryHandler<events::audio::GetPlaybackPositionQuery>(
            [this](const auto& query) {
                return getPlaybackPosition(query.handle);
            });

        dispatcher.registerQueryHandler<events::audio::GetDurationQuery>(
            [this](const auto& query) {
                return getDuration(query.handle);
            });

        // Audio Settings Commands/Queries
        dispatcher.registerCommandHandler<events::audio::ApplyAudioSettingsCommand>(
            [this](const auto& cmd) {
                audioProvider->applySettings(cmd.settings);
                // VK-1506: let main-thread consumers cache the doppler teleport guard.
                events::audio::AudioSettingsChangedNotification note;
                note.maxDopplerSpeed = cmd.settings.maxDopplerSpeed;
                ::events::EventDispatcher::instance().publish(note);
                return true;
            });

        dispatcher.registerQueryHandler<events::audio::GetAudioSettingsQuery>(
            [this](const auto&) {
                return audioProvider->getCurrentSettings();
            });

        // VK-1508: report the live device HRTF status for the editor's status line.
        dispatcher.registerQueryHandler<events::audio::GetHrtfStatusQuery>(
            [this](const auto&) {
                return audioProvider->getHrtfStatus();
            });

        // VK-1513: report real-voice budget occupancy for the editor's voice readout.
        dispatcher.registerQueryHandler<events::audio::GetVoiceCountQuery>(
            [this](const auto&) {
                return audioProvider->getVoiceStats();
            });

        // VK-1515: the active-sounds overlay. Diagnostics, so both go straight to the
        // provider and stay out of IAudioService — same call as voice stats and bus levels.
        dispatcher.registerQueryHandler<events::audio::GetActiveVoicesQuery>(
            [this](const auto&) {
                return audioProvider->getActiveVoices();
            });

        dispatcher.registerCommandHandler<events::audio::SetVoiceDebugEnabledCommand>(
            [this](const auto& cmd) {
                audioProvider->setVoiceDebugEnabled(cmd.enabled);
            });

        // Audio Bus Commands
        dispatcher.registerCommandHandler<events::audio::CreateBusCommand>(
            [this](const auto& cmd) {
                createBus(cmd.busName, cmd.parentName);
            });

        dispatcher.registerCommandHandler<events::audio::SetBusVolumeCommand>(
            [this](const auto& cmd) {
                setBusVolume(cmd.busName, cmd.volume);
            });

        dispatcher.registerCommandHandler<events::audio::SetBusMutedCommand>(
            [this](const auto& cmd) {
                setBusMuted(cmd.busName, cmd.muted);
            });

        dispatcher.registerCommandHandler<events::audio::SetBusSoloedCommand>(
            [this](const auto& cmd) {
                setBusSoloed(cmd.busName, cmd.soloed);
            });

        dispatcher.registerCommandHandler<events::audio::SetBusDuckCommand>(
            [this](const auto& cmd) {
                setBusDuck(cmd.targetBus, cmd.config);
            });

        dispatcher.registerCommandHandler<events::audio::RemoveBusDuckCommand>(
            [this](const auto& cmd) {
                removeBusDuck(cmd.targetBus);
            });

        dispatcher.registerCommandHandler<events::audio::SaveMixSnapshotCommand>(
            [this](const auto& cmd) {
                saveMixSnapshot(cmd.name);
            });

        dispatcher.registerCommandHandler<events::audio::LoadMixSnapshotCommand>(
            [this](const auto& cmd) {
                loadMixSnapshot(cmd.name);
            });

        dispatcher.registerCommandHandler<events::audio::DeleteMixSnapshotCommand>(
            [this](const auto& cmd) {
                deleteMixSnapshot(cmd.name);
            });

        // Audio Bus Queries
        dispatcher.registerQueryHandler<events::audio::GetBusVolumeQuery>(
            [this](const auto& query) {
                return getBusVolume(query.busName);
            });

        dispatcher.registerQueryHandler<events::audio::IsBusMutedQuery>(
            [this](const auto& query) {
                return isBusMuted(query.busName);
            });

        dispatcher.registerQueryHandler<events::audio::IsBusSoloedQuery>(
            [this](const auto& query) {
                return isBusSoloed(query.busName);
            });

        dispatcher.registerQueryHandler<events::audio::GetBusNamesQuery>(
            [this](const auto&) {
                return getBusNames();
            });

        // VK-1514: diagnostic query goes directly to the provider, like voice stats.
        dispatcher.registerQueryHandler<events::audio::GetBusLevelsQuery>(
            [this](const auto&) {
                return audioProvider->getBusLevels();
            });

        dispatcher.registerQueryHandler<events::audio::GetBusDuckQuery>(
            [this](const auto& query) {
                return getBusDuck(query.targetBus);
            });

        dispatcher.registerQueryHandler<events::audio::GetSnapshotNamesQuery>(
            [this](const auto&) {
                return getSnapshotNames();
            });

        // Audio Effect Commands
        dispatcher.registerCommandHandler<events::audio::AddBusEffectCommand>(
            [this](const auto& cmd) {
                return addBusEffect(cmd.busName, cmd.config);
            });

        dispatcher.registerCommandHandler<events::audio::RemoveBusEffectCommand>(
            [this](const auto& cmd) {
                return removeBusEffect(cmd.busName, cmd.effectId);
            });

        dispatcher.registerCommandHandler<events::audio::UpdateBusEffectCommand>(
            [this](const auto& cmd) {
                return updateBusEffect(cmd.busName, cmd.effectId, cmd.config);
            });

        dispatcher.registerCommandHandler<events::audio::SetBusEffectEnabledCommand>(
            [this](const auto& cmd) {
                return setBusEffectEnabled(cmd.busName, cmd.effectId, cmd.enabled);
            });

        dispatcher.registerCommandHandler<events::audio::SetBusEffectWetDryCommand>(
            [this](const auto& cmd) {
                return setBusEffectWetDry(cmd.busName, cmd.effectId, cmd.wetDryMix);
            });

        // Audio Effect Queries
        dispatcher.registerQueryHandler<events::audio::GetBusEffectChainQuery>(
            [this](const auto& query) {
                return getBusEffectChain(query.busName);
            });

        dispatcher.registerQueryHandler<events::audio::GetMaxEffectsPerBusQuery>(
            [this](const auto&) {
                return getMaxEffectsPerBus();
            });
    }

    void AudioServiceImpl::setListenerPosition(const glm::vec3& position,
                                                const glm::vec3& forward,
                                                const glm::vec3& up,
                                                const glm::vec3& velocity) {
        audioProvider->setListenerPosition(position, forward, up, velocity);
    }

    AudioHandle AudioServiceImpl::playSound3D(const std::string& path, const glm::vec3& position,
                                               const AudioParams& params) {
        AudioPlayParams playParams = convertParams(params);
        AudioHandleId handleId = audioProvider->playSound3D(path, position, playParams);
        return AudioHandle{handleId};
    }

    AudioHandle AudioServiceImpl::playStreamingSound(const std::string& path, const AudioParams& params) {
        AudioPlayParams playParams = convertParams(params);
        playParams.streaming = true;
        AudioHandleId handleId = audioProvider->playStreamingSound(path, playParams);
        return AudioHandle{handleId};
    }

    void AudioServiceImpl::stopSound(AudioHandle handle) {
        audioProvider->stopSound(handle.id);
    }

    void AudioServiceImpl::pauseSound(AudioHandle handle) {
        audioProvider->pauseSound(handle.id);
    }

    void AudioServiceImpl::resumeSound(AudioHandle handle) {
        audioProvider->resumeSound(handle.id);
    }

    bool AudioServiceImpl::isPlaying(AudioHandle handle) const {
        return audioProvider->isPlaying(handle.id);
    }

    void AudioServiceImpl::setVolume(AudioHandle handle, float volume) {
        audioProvider->setVolume(handle.id, volume);
    }

    void AudioServiceImpl::setPitch(AudioHandle handle, float pitch) {
        audioProvider->setPitch(handle.id, pitch);
    }

    float AudioServiceImpl::getPlaybackPosition(AudioHandle handle) const {
        return audioProvider->getPlaybackPosition(handle.id);
    }

    bool AudioServiceImpl::setPlaybackPosition(AudioHandle handle, float seconds) {
        return audioProvider->setPlaybackPosition(handle.id, seconds);
    }

    float AudioServiceImpl::getDuration(AudioHandle handle) const {
        return audioProvider->getDuration(handle.id);
    }

    AudioPlayParams AudioServiceImpl::convertParams(const AudioParams& params) const {
        AudioPlayParams playParams;
        playParams.volume = params.volume;
        playParams.pitch = params.pitch;
        playParams.loop = params.loop;
        playParams.is3D = params.is3D;
        playParams.position = params.position;
        playParams.minDistance = params.minDistance;
        playParams.maxDistance = params.maxDistance;
        playParams.rolloffFactor = params.rolloffFactor;
        playParams.streaming = params.streaming;
        playParams.enableDistanceFilter = params.enableDistanceFilter;
        playParams.filterStartDistance = params.filterStartDistance;
        playParams.filterMaxDistance = params.filterMaxDistance;
        playParams.filterIntensity = params.filterIntensity;
        playParams.innerConeAngle = params.innerConeAngle;
        playParams.outerConeAngle = params.outerConeAngle;
        playParams.outerConeGain = params.outerConeGain;
        playParams.direction = params.direction;
        playParams.busName = params.busName;
        playParams.priority = params.priority;
        return playParams;
    }

    void AudioServiceImpl::createBus(const std::string& busName, const std::string& parentName) {
        audioProvider->createBus(busName, parentName);
    }

    void AudioServiceImpl::setBusVolume(const std::string& busName, float volume) {
        audioProvider->setBusVolume(busName, volume);
    }

    void AudioServiceImpl::setBusMuted(const std::string& busName, bool muted) {
        audioProvider->setBusMuted(busName, muted);
    }

    void AudioServiceImpl::setBusSoloed(const std::string& busName, bool soloed) {
        audioProvider->setBusSoloed(busName, soloed);
    }

    float AudioServiceImpl::getBusVolume(const std::string& busName) const {
        return audioProvider->getBusVolume(busName);
    }

    bool AudioServiceImpl::isBusMuted(const std::string& busName) const {
        return audioProvider->isBusMuted(busName);
    }

    bool AudioServiceImpl::isBusSoloed(const std::string& busName) const {
        return audioProvider->isBusSoloed(busName);
    }

    std::vector<std::string> AudioServiceImpl::getBusNames() const {
        return audioProvider->getBusNames();
    }

    void AudioServiceImpl::setBusDuck(const std::string& targetBus,
                                      const types::BusDuckConfig& config) {
        audioProvider->setBusDuck(targetBus, config);
    }

    void AudioServiceImpl::removeBusDuck(const std::string& targetBus) {
        audioProvider->removeBusDuck(targetBus);
    }

    std::optional<types::BusDuckConfig> AudioServiceImpl::getBusDuck(
        const std::string& targetBus) const {
        return audioProvider->getBusDuck(targetBus);
    }

    void AudioServiceImpl::saveMixSnapshot(const std::string& name) {
        audioProvider->saveMixSnapshot(name);
    }

    void AudioServiceImpl::loadMixSnapshot(const std::string& name) {
        audioProvider->loadMixSnapshot(name);
    }

    void AudioServiceImpl::deleteMixSnapshot(const std::string& name) {
        audioProvider->deleteMixSnapshot(name);
    }

    std::vector<std::string> AudioServiceImpl::getSnapshotNames() const {
        return audioProvider->getSnapshotNames();
    }

    // === Audio Effects ===

    bool AudioServiceImpl::addBusEffect(const std::string& busName, const types::BusEffectConfig& config) {
        return audioProvider->addBusEffect(busName, config);
    }

    bool AudioServiceImpl::removeBusEffect(const std::string& busName, uint32_t effectId) {
        return audioProvider->removeBusEffect(busName, effectId);
    }

    bool AudioServiceImpl::updateBusEffect(const std::string& busName, uint32_t effectId,
                                            const types::BusEffectConfig& config) {
        return audioProvider->updateBusEffect(busName, effectId, config);
    }

    bool AudioServiceImpl::setBusEffectEnabled(const std::string& busName, uint32_t effectId, bool enabled) {
        return audioProvider->setBusEffectEnabled(busName, effectId, enabled);
    }

    bool AudioServiceImpl::setBusEffectWetDry(const std::string& busName, uint32_t effectId, float wetDry) {
        return audioProvider->setBusEffectWetDry(busName, effectId, wetDry);
    }

    std::vector<types::BusEffectConfig> AudioServiceImpl::getBusEffectChain(const std::string& busName) const {
        return audioProvider->getBusEffectChain(busName);
    }

    int AudioServiceImpl::getMaxEffectsPerBus() const {
        return audioProvider->getMaxEffectsPerBus();
    }

}
