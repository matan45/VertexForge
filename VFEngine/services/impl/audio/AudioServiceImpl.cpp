#include "AudioServiceImpl.hpp"
#include "../../events/audio/AudioEvents.hpp"
#include "../../events/audio/AudioSettingsEvents.hpp"
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
        return playParams;
    }

}
