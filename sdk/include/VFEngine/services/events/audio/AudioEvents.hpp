#pragma once
#include "../EventTypes.hpp"
#include "../../interfaces/audio/IAudioService.hpp"
#include <glm/glm.hpp>
#include <string>

namespace events::audio {

    // ============================================================
    // LISTENER COMMANDS
    // ============================================================

    struct SetListenerPositionCommand : ::events::ICommand<void> {
        glm::vec3 position;
        glm::vec3 forward;
        glm::vec3 up{0.0f, 1.0f, 0.0f};
        glm::vec3 velocity{0.0f};  // VK-1506 doppler; zero on the editor path
        std::string_view getName() const override { return "SetListenerPosition"; }
    };

    // ============================================================
    // SOUND PLAYBACK COMMANDS
    // ============================================================

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

    struct FadeOutAndReleaseSoundCommand : ::events::ICommand<void> {
        services::AudioHandle handle;
        float fadeDurationMs = 300.0f;
        std::string_view getName() const override { return "FadeOutAndReleaseSound"; }
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

    // Per-frame re-sync of a playing 3D source's transform (VK-1505: sounds follow
    // moving entities). velocity is reserved for VK-1506 doppler (dispatched as 0 today).
    struct SetSoundTransformCommand : ::events::ICommand<void> {
        services::AudioHandle handle;
        glm::vec3 position;
        glm::vec3 direction{0.0f, 0.0f, -1.0f};
        glm::vec3 velocity{0.0f};
        std::string_view getName() const override { return "SetSoundTransform"; }
    };

    // ============================================================
    // STREAMING AUDIO COMMANDS
    // ============================================================

    struct PlayStreamingSoundCommand : ::events::ICommand<services::AudioHandle> {
        std::string path;
        services::AudioParams params;
        std::string_view getName() const override { return "PlayStreamingSound"; }
    };

    struct SetPlaybackPositionCommand : ::events::ICommand<bool> {
        services::AudioHandle handle;
        float seconds;
        std::string_view getName() const override { return "SetPlaybackPosition"; }
    };

    // ============================================================
    // QUERIES
    // ============================================================

    struct IsSoundPlayingQuery : ::events::IQuery<bool> {
        services::AudioHandle handle;
        std::string_view getName() const override { return "IsSoundPlaying"; }
    };

    struct GetPlaybackPositionQuery : ::events::IQuery<float> {
        services::AudioHandle handle;
        std::string_view getName() const override { return "GetPlaybackPosition"; }
    };

    struct GetDurationQuery : ::events::IQuery<float> {
        services::AudioHandle handle;
        std::string_view getName() const override { return "GetDuration"; }
    };

}
