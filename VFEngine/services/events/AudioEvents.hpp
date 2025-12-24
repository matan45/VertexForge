#pragma once
#include "EventTypes.hpp"
#include "../interfaces/IAudioService.hpp"
#include <glm/glm.hpp>
#include <string>

namespace services::events::audio {

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

    struct PlaySoundCommand : ::events::ICommand<AudioHandle> {
        std::string path;
        AudioParams params;
        std::string_view getName() const override { return "PlaySound"; }
    };

    struct PlaySound3DCommand : ::events::ICommand<AudioHandle> {
        std::string path;
        glm::vec3 position;
        AudioParams params;
        std::string_view getName() const override { return "PlaySound3D"; }
    };

    struct StopSoundCommand : ::events::ICommand<void> {
        AudioHandle handle;
        std::string_view getName() const override { return "StopSound"; }
    };

    struct PauseSoundCommand : ::events::ICommand<void> {
        AudioHandle handle;
        std::string_view getName() const override { return "PauseSound"; }
    };

    struct ResumeSoundCommand : ::events::ICommand<void> {
        AudioHandle handle;
        std::string_view getName() const override { return "ResumeSound"; }
    };

    struct SetSoundVolumeCommand : ::events::ICommand<void> {
        AudioHandle handle;
        float volume;
        std::string_view getName() const override { return "SetSoundVolume"; }
    };

    struct SetSoundPitchCommand : ::events::ICommand<void> {
        AudioHandle handle;
        float pitch;
        std::string_view getName() const override { return "SetSoundPitch"; }
    };

    struct SetSoundPositionCommand : ::events::ICommand<void> {
        AudioHandle handle;
        glm::vec3 position;
        std::string_view getName() const override { return "SetSoundPosition"; }
    };

    // ============================================================
    // AUDIO SOURCE COMPONENT COMMANDS
    // ============================================================

    struct AddAudioSourceCommand : ::events::ICommand<void> {
        EntityHandle entity;
        AudioSourceData data;
        std::string_view getName() const override { return "AddAudioSource"; }
    };

    struct RemoveAudioSourceCommand : ::events::ICommand<void> {
        EntityHandle entity;
        std::string_view getName() const override { return "RemoveAudioSource"; }
    };

    struct PlayEntityAudioCommand : ::events::ICommand<void> {
        EntityHandle entity;
        std::string_view getName() const override { return "PlayEntityAudio"; }
    };

    struct PauseEntityAudioCommand : ::events::ICommand<void> {
        EntityHandle entity;
        std::string_view getName() const override { return "PauseEntityAudio"; }
    };

    struct StopEntityAudioCommand : ::events::ICommand<void> {
        EntityHandle entity;
        std::string_view getName() const override { return "StopEntityAudio"; }
    };

    // ============================================================
    // QUERIES
    // ============================================================

    struct GetMasterVolumeQuery : ::events::IQuery<float> {
        std::string_view getName() const override { return "GetMasterVolume"; }
    };

    struct IsSoundPlayingQuery : ::events::IQuery<bool> {
        AudioHandle handle;
        std::string_view getName() const override { return "IsSoundPlaying"; }
    };

    struct HasAudioSourceQuery : ::events::IQuery<bool> {
        EntityHandle entity;
        std::string_view getName() const override { return "HasAudioSource"; }
    };

    // ============================================================
    // NOTIFICATIONS
    // ============================================================

    struct AudioSystemInitializedNotification : ::events::INotification {
        std::string_view getName() const override { return "AudioSystemInitialized"; }
    };

    struct SoundStartedNotification : ::events::INotification {
        AudioHandle handle;
        std::string path;
        std::string_view getName() const override { return "SoundStarted"; }
    };

    struct SoundStoppedNotification : ::events::INotification {
        AudioHandle handle;
        std::string_view getName() const override { return "SoundStopped"; }
    };

    struct SoundFinishedNotification : ::events::INotification {
        AudioHandle handle;
        std::string_view getName() const override { return "SoundFinished"; }
    };

}
