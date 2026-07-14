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

    // VK-1511: read model for the last listener pose set on the (main) dispatch
    // thread. Defaults mirror OpenAL's default listener (origin, -Z forward, +Y up),
    // so a query issued before ViewPort's first per-frame dispatch still yields a
    // valid (origin-anchored) placement. See GetListenerStateQuery in QUERIES.
    struct ListenerState {
        glm::vec3 position{0.0f};
        glm::vec3 forward{0.0f, 0.0f, -1.0f};
        glm::vec3 up{0.0f, 1.0f, 0.0f};
        bool valid = false;
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

    // VK-1511: read back the last listener pose from AudioServiceImpl's main-thread
    // cache (written by the SetListenerPositionCommand handler — never the
    // audio-thread snapshot, avoiding the VK-1354 don't-race-the-snapshot hazard).
    struct GetListenerStateQuery : ::events::IQuery<ListenerState> {
        std::string_view getName() const override { return "GetListenerState"; }
    };

}
