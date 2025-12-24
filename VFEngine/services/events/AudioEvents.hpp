#pragma once
#include "EventTypes.hpp"
#include "../interfaces/IAudioService.hpp"
#include <glm/glm.hpp>
#include <string>

namespace events::audio {

    // ============================================================
    // GLOBAL AUDIO COMMANDS
    // ============================================================

    struct SetMasterVolumeCommand : ::events::ICommand<void> {
        float volume;
        std::string_view getName() const override { return "SetMasterVolume"; }
    };

    struct PauseAllCommand : ::events::ICommand<void> {
        std::string_view getName() const override { return "PauseAll"; }
    };

    struct ResumeAllCommand : ::events::ICommand<void> {
        std::string_view getName() const override { return "ResumeAll"; }
    };

    struct StopAllCommand : ::events::ICommand<void> {
        std::string_view getName() const override { return "StopAll"; }
    };

    // ============================================================
    // LISTENER COMMANDS
    // ============================================================

    struct SetListenerPositionCommand : ::events::ICommand<void> {
        glm::vec3 position;
        glm::vec3 forward;
        glm::vec3 up{0.0f, 1.0f, 0.0f};
        std::string_view getName() const override { return "SetListenerPosition"; }
    };

    struct SetListenerVelocityCommand : ::events::ICommand<void> {
        glm::vec3 velocity;
        std::string_view getName() const override { return "SetListenerVelocity"; }
    };

    // ============================================================
    // SOUND PLAYBACK COMMANDS
    // ============================================================

    struct PlaySoundCommand : ::events::ICommand<services::AudioHandle> {
        std::string path;
        services::AudioParams params;
        std::string_view getName() const override { return "PlaySound"; }
    };

    struct PlaySound3DCommand : ::events::ICommand<services::AudioHandle> {
        std::string path;
        glm::vec3 position;
        services::AudioParams params;
        std::string_view getName() const override { return "PlaySound3D"; }
    };

    struct StopSoundCommand : ::events::ICommand<void> {
        services::AudioHandle handle;
        std::string_view getName() const override { return "StopSound"; }
    };

    struct PauseSoundCommand : ::events::ICommand<void> {
        services::AudioHandle handle;
        std::string_view getName() const override { return "PauseSound"; }
    };

    struct ResumeSoundCommand : ::events::ICommand<void> {
        services::AudioHandle handle;
        std::string_view getName() const override { return "ResumeSound"; }
    };

    struct SetSoundVolumeCommand : ::events::ICommand<void> {
        services::AudioHandle handle;
        float volume;
        std::string_view getName() const override { return "SetSoundVolume"; }
    };

    struct SetSoundPitchCommand : ::events::ICommand<void> {
        services::AudioHandle handle;
        float pitch;
        std::string_view getName() const override { return "SetSoundPitch"; }
    };

    struct SetSoundPositionCommand : ::events::ICommand<void> {
        services::AudioHandle handle;
        glm::vec3 position;
        std::string_view getName() const override { return "SetSoundPosition"; }
    };

    // ============================================================
    // STREAMING AUDIO COMMANDS
    // ============================================================

    struct PlayStreamingSoundCommand : ::events::ICommand<services::AudioHandle> {
        std::string path;
        services::AudioParams params;
        std::string_view getName() const override { return "PlayStreamingSound"; }
    };

    struct PlayStreamingSound3DCommand : ::events::ICommand<services::AudioHandle> {
        std::string path;
        glm::vec3 position;
        services::AudioParams params;
        std::string_view getName() const override { return "PlayStreamingSound3D"; }
    };

    struct SetPlaybackPositionCommand : ::events::ICommand<bool> {
        services::AudioHandle handle;
        float seconds;
        std::string_view getName() const override { return "SetPlaybackPosition"; }
    };

    // ============================================================
    // 2D AUDIO SOURCE COMPONENT COMMANDS (streaming)
    // ============================================================

    struct AddAudioSource2DCommand : ::events::ICommand<void> {
        services::EntityHandle entity;
        services::AudioSource2DData data;
        std::string_view getName() const override { return "AddAudioSource2D"; }
    };

    struct RemoveAudioSource2DCommand : ::events::ICommand<void> {
        services::EntityHandle entity;
        std::string_view getName() const override { return "RemoveAudioSource2D"; }
    };

    struct PlayEntityAudio2DCommand : ::events::ICommand<void> {
        services::EntityHandle entity;
        std::string_view getName() const override { return "PlayEntityAudio2D"; }
    };

    struct PauseEntityAudio2DCommand : ::events::ICommand<void> {
        services::EntityHandle entity;
        std::string_view getName() const override { return "PauseEntityAudio2D"; }
    };

    struct StopEntityAudio2DCommand : ::events::ICommand<void> {
        services::EntityHandle entity;
        std::string_view getName() const override { return "StopEntityAudio2D"; }
    };

    // ============================================================
    // 3D AUDIO SOURCE COMPONENT COMMANDS (cached/spatial)
    // ============================================================

    struct AddAudioSource3DCommand : ::events::ICommand<void> {
        services::EntityHandle entity;
        services::AudioSource3DData data;
        std::string_view getName() const override { return "AddAudioSource3D"; }
    };

    struct RemoveAudioSource3DCommand : ::events::ICommand<void> {
        services::EntityHandle entity;
        std::string_view getName() const override { return "RemoveAudioSource3D"; }
    };

    struct PlayEntityAudio3DCommand : ::events::ICommand<void> {
        services::EntityHandle entity;
        glm::vec3 position;
        std::string_view getName() const override { return "PlayEntityAudio3D"; }
    };

    struct PauseEntityAudio3DCommand : ::events::ICommand<void> {
        services::EntityHandle entity;
        std::string_view getName() const override { return "PauseEntityAudio3D"; }
    };

    struct StopEntityAudio3DCommand : ::events::ICommand<void> {
        services::EntityHandle entity;
        std::string_view getName() const override { return "StopEntityAudio3D"; }
    };

    struct UpdateEntityAudio3DPositionCommand : ::events::ICommand<void> {
        services::EntityHandle entity;
        glm::vec3 position;
        std::string_view getName() const override { return "UpdateEntityAudio3DPosition"; }
    };

    // ============================================================
    // QUERIES
    // ============================================================

    struct GetMasterVolumeQuery : ::events::IQuery<float> {
        std::string_view getName() const override { return "GetMasterVolume"; }
    };

    struct IsSoundPlayingQuery : ::events::IQuery<bool> {
        services::AudioHandle handle;
        std::string_view getName() const override { return "IsSoundPlaying"; }
    };

    struct HasAudioSource2DQuery : ::events::IQuery<bool> {
        services::EntityHandle entity;
        std::string_view getName() const override { return "HasAudioSource2D"; }
    };

    struct HasAudioSource3DQuery : ::events::IQuery<bool> {
        services::EntityHandle entity;
        std::string_view getName() const override { return "HasAudioSource3D"; }
    };

    // ============================================================
    // STREAMING AUDIO QUERIES
    // ============================================================

    struct GetPlaybackPositionQuery : ::events::IQuery<float> {
        services::AudioHandle handle;
        std::string_view getName() const override { return "GetPlaybackPosition"; }
    };

    struct GetDurationQuery : ::events::IQuery<float> {
        services::AudioHandle handle;
        std::string_view getName() const override { return "GetDuration"; }
    };

    struct IsStreamingHandleQuery : ::events::IQuery<bool> {
        services::AudioHandle handle;
        std::string_view getName() const override { return "IsStreamingHandle"; }
    };

    // ============================================================
    // NOTIFICATIONS
    // ============================================================

    struct AudioSystemInitializedNotification : ::events::INotification {
        std::string_view getName() const override { return "AudioSystemInitialized"; }
    };

    struct SoundStartedNotification : ::events::INotification {
        services::AudioHandle handle;
        std::string path;
        std::string_view getName() const override { return "SoundStarted"; }
    };

    struct SoundStoppedNotification : ::events::INotification {
        services::AudioHandle handle;
        std::string_view getName() const override { return "SoundStopped"; }
    };

    struct SoundFinishedNotification : ::events::INotification {
        services::AudioHandle handle;
        std::string_view getName() const override { return "SoundFinished"; }
    };

}
