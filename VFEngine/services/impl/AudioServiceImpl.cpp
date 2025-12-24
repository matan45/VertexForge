#include "AudioServiceImpl.hpp"
#include "../events/AudioEvents.hpp"
#include "../events/EventDispatcher.hpp"
#include <cassert>

namespace services {

    AudioServiceImpl::AudioServiceImpl(IAudioProvider* audioProvider)
        : audioProvider(audioProvider) {
        assert(audioProvider && "AudioProvider must not be null");
    }

    AudioServiceImpl::~AudioServiceImpl() {
        entityAudioSources.clear();
    }

    void AudioServiceImpl::registerEventHandlers() {
        auto& dispatcher = ::events::EventDispatcher::instance();

        // Global Audio Commands
        dispatcher.registerCommandHandler<events::audio::SetMasterVolumeCommand>(
            [this](const auto& cmd) {
                setMasterVolume(cmd.volume);
            });

        dispatcher.registerCommandHandler<events::audio::PauseAllCommand>(
            [this](const auto&) {
                pauseAll();
            });

        dispatcher.registerCommandHandler<events::audio::ResumeAllCommand>(
            [this](const auto&) {
                resumeAll();
            });

        dispatcher.registerCommandHandler<events::audio::StopAllCommand>(
            [this](const auto&) {
                stopAll();
            });

        // Listener Commands
        dispatcher.registerCommandHandler<events::audio::SetListenerPositionCommand>(
            [this](const auto& cmd) {
                setListenerPosition(cmd.position, cmd.forward, cmd.up);
            });

        dispatcher.registerCommandHandler<events::audio::SetListenerVelocityCommand>(
            [this](const auto& cmd) {
                setListenerVelocity(cmd.velocity);
            });

        // Sound Playback Commands
        dispatcher.registerCommandHandler<events::audio::PlaySoundCommand>(
            [this](const auto& cmd) {
                return playSound(cmd.path, cmd.params);
            });

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

        dispatcher.registerCommandHandler<events::audio::SetSoundPositionCommand>(
            [this](const auto& cmd) {
                setPosition(cmd.handle, cmd.position);
            });

        // Entity Audio Commands
        dispatcher.registerCommandHandler<events::audio::AddAudioSourceCommand>(
            [this](const auto& cmd) {
                addAudioSource(cmd.entity, cmd.data);
            });

        dispatcher.registerCommandHandler<events::audio::RemoveAudioSourceCommand>(
            [this](const auto& cmd) {
                removeAudioSource(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::audio::PlayEntityAudioCommand>(
            [this](const auto& cmd) {
                playEntityAudio(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::audio::StopEntityAudioCommand>(
            [this](const auto& cmd) {
                stopEntityAudio(cmd.entity);
            });

        // Queries
        dispatcher.registerQueryHandler<events::audio::GetMasterVolumeQuery>(
            [this](const auto&) {
                return getMasterVolume();
            });

        dispatcher.registerQueryHandler<events::audio::IsSoundPlayingQuery>(
            [this](const auto& query) {
                return isPlaying(query.handle);
            });

        dispatcher.registerQueryHandler<events::audio::HasAudioSourceQuery>(
            [this](const auto& query) {
                return hasAudioSource(query.entity);
            });
    }

    void AudioServiceImpl::setMasterVolume(float volume) {
        audioProvider->setMasterVolume(volume);
    }

    float AudioServiceImpl::getMasterVolume() const {
        return audioProvider->getMasterVolume();
    }

    void AudioServiceImpl::pauseAll() {
        audioProvider->pauseAll();
    }

    void AudioServiceImpl::resumeAll() {
        audioProvider->resumeAll();
    }

    void AudioServiceImpl::stopAll() {
        audioProvider->stopAll();
    }

    void AudioServiceImpl::setListenerPosition(const glm::vec3& position,
                                                const glm::vec3& forward,
                                                const glm::vec3& up) {
        audioProvider->setListenerPosition(position, forward, up);
    }

    void AudioServiceImpl::setListenerVelocity(const glm::vec3& velocity) {
        audioProvider->setListenerVelocity(velocity);
    }

    AudioHandle AudioServiceImpl::playSound(const std::string& path, const AudioParams& params) {
        AudioPlayParams playParams = convertParams(params);
        AudioHandleId handleId = audioProvider->playSound(path, playParams);
        return AudioHandle{handleId};
    }

    AudioHandle AudioServiceImpl::playSound3D(const std::string& path, const glm::vec3& position,
                                               const AudioParams& params) {
        AudioPlayParams playParams = convertParams(params);
        AudioHandleId handleId = audioProvider->playSound3D(path, position, playParams);
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

    void AudioServiceImpl::setPosition(AudioHandle handle, const glm::vec3& position) {
        audioProvider->setPosition(handle.id, position);
    }

    void AudioServiceImpl::addAudioSource(EntityHandle entity, const AudioSourceData& data) {
        if (!entity.isValid()) return;

        EntityAudioState state;
        state.data = data;
        state.currentHandle = AudioHandle{InvalidAudioHandleId};
        entityAudioSources[entity] = state;
    }

    void AudioServiceImpl::removeAudioSource(EntityHandle entity) {
        auto it = entityAudioSources.find(entity);
        if (it != entityAudioSources.end()) {
            if (it->second.currentHandle.isValid()) {
                audioProvider->stopSound(it->second.currentHandle.id);
            }
            entityAudioSources.erase(it);
        }
    }

    bool AudioServiceImpl::hasAudioSource(EntityHandle entity) const {
        return entityAudioSources.find(entity) != entityAudioSources.end();
    }

    void AudioServiceImpl::playEntityAudio(EntityHandle entity) {
        auto it = entityAudioSources.find(entity);
        if (it == entityAudioSources.end()) return;

        auto& state = it->second;

        if (state.currentHandle.isValid() && audioProvider->isPlaying(state.currentHandle.id)) {
            return;
        }

        AudioParams params;
        params.volume = state.data.volume;
        params.pitch = state.data.pitch;
        params.loop = state.data.loop;
        params.is3D = state.data.is3D;
        params.minDistance = state.data.minDistance;
        params.maxDistance = state.data.maxDistance;

        AudioPlayParams playParams = convertParams(params);
        AudioHandleId handleId = audioProvider->playSound(state.data.clipPath, playParams);
        state.currentHandle = AudioHandle{handleId};
    }

    void AudioServiceImpl::stopEntityAudio(EntityHandle entity) {
        auto it = entityAudioSources.find(entity);
        if (it == entityAudioSources.end()) return;

        if (it->second.currentHandle.isValid()) {
            audioProvider->stopSound(it->second.currentHandle.id);
            it->second.currentHandle = AudioHandle{InvalidAudioHandleId};
        }
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
        return playParams;
    }

}
