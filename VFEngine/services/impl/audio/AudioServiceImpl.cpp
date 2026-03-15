#include "AudioServiceImpl.hpp"
#include "../../events/audio/AudioEvents.hpp"
#include "../../events/audio/AudioSettingsEvents.hpp"
#include "../../events/audio/AudioBusEvents.hpp"
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
                setListenerPosition(cmd.position, cmd.forward, cmd.up);
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
                return true;
            });

        dispatcher.registerQueryHandler<events::audio::GetAudioSettingsQuery>(
            [this](const auto&) {
                return audioProvider->getCurrentSettings();
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

        dispatcher.registerQueryHandler<events::audio::GetBusNamesQuery>(
            [this](const auto&) {
                return getBusNames();
            });

        dispatcher.registerQueryHandler<events::audio::GetSnapshotNamesQuery>(
            [this](const auto&) {
                return getSnapshotNames();
            });
    }

    void AudioServiceImpl::setListenerPosition(const glm::vec3& position,
                                                const glm::vec3& forward,
                                                const glm::vec3& up) {
        audioProvider->setListenerPosition(position, forward, up);
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

    std::vector<std::string> AudioServiceImpl::getBusNames() const {
        return audioProvider->getBusNames();
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

}
