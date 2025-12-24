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
        entityAudio2DSources.clear();
        entityAudio3DSources.clear();
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

        // 2D Entity Audio Commands (streaming)
        dispatcher.registerCommandHandler<events::audio::AddAudioSource2DCommand>(
            [this](const auto& cmd) {
                addAudioSource2D(cmd.entity, cmd.data);
            });

        dispatcher.registerCommandHandler<events::audio::RemoveAudioSource2DCommand>(
            [this](const auto& cmd) {
                removeAudioSource2D(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::audio::PlayEntityAudio2DCommand>(
            [this](const auto& cmd) {
                playEntityAudio2D(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::audio::PauseEntityAudio2DCommand>(
            [this](const auto& cmd) {
                pauseEntityAudio2D(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::audio::StopEntityAudio2DCommand>(
            [this](const auto& cmd) {
                stopEntityAudio2D(cmd.entity);
            });

        // 3D Entity Audio Commands (cached/spatial)
        dispatcher.registerCommandHandler<events::audio::AddAudioSource3DCommand>(
            [this](const auto& cmd) {
                addAudioSource3D(cmd.entity, cmd.data);
            });

        dispatcher.registerCommandHandler<events::audio::RemoveAudioSource3DCommand>(
            [this](const auto& cmd) {
                removeAudioSource3D(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::audio::PlayEntityAudio3DCommand>(
            [this](const auto& cmd) {
                playEntityAudio3D(cmd.entity, cmd.position);
            });

        dispatcher.registerCommandHandler<events::audio::PauseEntityAudio3DCommand>(
            [this](const auto& cmd) {
                pauseEntityAudio3D(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::audio::StopEntityAudio3DCommand>(
            [this](const auto& cmd) {
                stopEntityAudio3D(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::audio::UpdateEntityAudio3DPositionCommand>(
            [this](const auto& cmd) {
                updateEntityAudio3DPosition(cmd.entity, cmd.position);
            });

        // Streaming Audio Commands
        dispatcher.registerCommandHandler<events::audio::PlayStreamingSoundCommand>(
            [this](const auto& cmd) {
                return playStreamingSound(cmd.path, cmd.params);
            });

        dispatcher.registerCommandHandler<events::audio::PlayStreamingSound3DCommand>(
            [this](const auto& cmd) {
                return playStreamingSound3D(cmd.path, cmd.position, cmd.params);
            });

        dispatcher.registerCommandHandler<events::audio::SetPlaybackPositionCommand>(
            [this](const auto& cmd) {
                return setPlaybackPosition(cmd.handle, cmd.seconds);
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

        dispatcher.registerQueryHandler<events::audio::HasAudioSource2DQuery>(
            [this](const auto& query) {
                return hasAudioSource2D(query.entity);
            });

        dispatcher.registerQueryHandler<events::audio::HasAudioSource3DQuery>(
            [this](const auto& query) {
                return hasAudioSource3D(query.entity);
            });

        // Streaming Audio Queries
        dispatcher.registerQueryHandler<events::audio::GetPlaybackPositionQuery>(
            [this](const auto& query) {
                return getPlaybackPosition(query.handle);
            });

        dispatcher.registerQueryHandler<events::audio::GetDurationQuery>(
            [this](const auto& query) {
                return getDuration(query.handle);
            });

        dispatcher.registerQueryHandler<events::audio::IsStreamingHandleQuery>(
            [this](const auto& query) {
                return isStreamingHandle(query.handle);
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

    // === 2D Audio Source (streaming) ===

    void AudioServiceImpl::addAudioSource2D(EntityHandle entity, const AudioSource2DData& data) {
        if (!entity.isValid()) return;

        EntityAudio2DState state;
        state.data = data;
        state.currentHandle = AudioHandle{InvalidAudioHandleId};
        entityAudio2DSources[entity] = state;
    }

    void AudioServiceImpl::removeAudioSource2D(EntityHandle entity) {
        auto it = entityAudio2DSources.find(entity);
        if (it != entityAudio2DSources.end()) {
            if (it->second.currentHandle.isValid()) {
                audioProvider->stopSound(it->second.currentHandle.id);
            }
            entityAudio2DSources.erase(it);
        }
    }

    bool AudioServiceImpl::hasAudioSource2D(EntityHandle entity) const {
        return entityAudio2DSources.find(entity) != entityAudio2DSources.end();
    }

    void AudioServiceImpl::playEntityAudio2D(EntityHandle entity) {
        auto it = entityAudio2DSources.find(entity);
        if (it == entityAudio2DSources.end()) return;

        auto& state = it->second;

        if (state.currentHandle.isValid() && audioProvider->isPlaying(state.currentHandle.id)) {
            return;
        }

        AudioPlayParams playParams;
        playParams.volume = state.data.volume;
        playParams.pitch = state.data.pitch;
        playParams.loop = state.data.loop;
        playParams.streaming = true;

        AudioHandleId handleId = audioProvider->playStreamingSound(state.data.audioFilePath, playParams);
        state.currentHandle = AudioHandle{handleId};
    }

    void AudioServiceImpl::pauseEntityAudio2D(EntityHandle entity) {
        auto it = entityAudio2DSources.find(entity);
        if (it == entityAudio2DSources.end()) return;

        if (it->second.currentHandle.isValid()) {
            audioProvider->pauseSound(it->second.currentHandle.id);
        }
    }

    void AudioServiceImpl::stopEntityAudio2D(EntityHandle entity) {
        auto it = entityAudio2DSources.find(entity);
        if (it == entityAudio2DSources.end()) return;

        if (it->second.currentHandle.isValid()) {
            audioProvider->stopSound(it->second.currentHandle.id);
            it->second.currentHandle = AudioHandle{InvalidAudioHandleId};
        }
    }

    // === 3D Audio Source (cached/spatial) ===

    void AudioServiceImpl::addAudioSource3D(EntityHandle entity, const AudioSource3DData& data) {
        if (!entity.isValid()) return;

        EntityAudio3DState state;
        state.data = data;
        state.currentHandle = AudioHandle{InvalidAudioHandleId};
        entityAudio3DSources[entity] = state;
    }

    void AudioServiceImpl::removeAudioSource3D(EntityHandle entity) {
        auto it = entityAudio3DSources.find(entity);
        if (it != entityAudio3DSources.end()) {
            if (it->second.currentHandle.isValid()) {
                audioProvider->stopSound(it->second.currentHandle.id);
            }
            entityAudio3DSources.erase(it);
        }
    }

    bool AudioServiceImpl::hasAudioSource3D(EntityHandle entity) const {
        return entityAudio3DSources.find(entity) != entityAudio3DSources.end();
    }

    void AudioServiceImpl::playEntityAudio3D(EntityHandle entity, const glm::vec3& position) {
        auto it = entityAudio3DSources.find(entity);
        if (it == entityAudio3DSources.end()) return;

        auto& state = it->second;
        state.position = position;

        if (state.currentHandle.isValid() && audioProvider->isPlaying(state.currentHandle.id)) {
            return;
        }

        AudioPlayParams playParams;
        playParams.volume = state.data.volume;
        playParams.pitch = state.data.pitch;
        playParams.loop = state.data.loop;
        playParams.is3D = true;
        playParams.position = position;
        playParams.minDistance = state.data.minDistance;
        playParams.maxDistance = state.data.maxDistance;

        AudioHandleId handleId = audioProvider->playSound3D(state.data.audioFilePath, position, playParams);
        state.currentHandle = AudioHandle{handleId};
    }

    void AudioServiceImpl::pauseEntityAudio3D(EntityHandle entity) {
        auto it = entityAudio3DSources.find(entity);
        if (it == entityAudio3DSources.end()) return;

        if (it->second.currentHandle.isValid()) {
            audioProvider->pauseSound(it->second.currentHandle.id);
        }
    }

    void AudioServiceImpl::stopEntityAudio3D(EntityHandle entity) {
        auto it = entityAudio3DSources.find(entity);
        if (it == entityAudio3DSources.end()) return;

        if (it->second.currentHandle.isValid()) {
            audioProvider->stopSound(it->second.currentHandle.id);
            it->second.currentHandle = AudioHandle{InvalidAudioHandleId};
        }
    }

    void AudioServiceImpl::updateEntityAudio3DPosition(EntityHandle entity, const glm::vec3& position) {
        auto it = entityAudio3DSources.find(entity);
        if (it == entityAudio3DSources.end()) return;

        it->second.position = position;

        // Also update position for currently playing 3D sound
        if (it->second.currentHandle.isValid()) {
            audioProvider->setPosition(it->second.currentHandle.id, position);
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
        playParams.streaming = params.streaming;
        return playParams;
    }

    // === Streaming Audio ===

    AudioHandle AudioServiceImpl::playStreamingSound(const std::string& path, const AudioParams& params) {
        AudioPlayParams playParams = convertParams(params);
        playParams.streaming = true;
        AudioHandleId handleId = audioProvider->playStreamingSound(path, playParams);
        return AudioHandle{handleId};
    }

    AudioHandle AudioServiceImpl::playStreamingSound3D(const std::string& path, const glm::vec3& position,
                                                        const AudioParams& params) {
        AudioPlayParams playParams = convertParams(params);
        playParams.streaming = true;
        AudioHandleId handleId = audioProvider->playStreamingSound3D(path, position, playParams);
        return AudioHandle{handleId};
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

    bool AudioServiceImpl::isStreamingHandle(AudioHandle handle) const {
        return audioProvider->isStreamingHandle(handle.id);
    }

}
