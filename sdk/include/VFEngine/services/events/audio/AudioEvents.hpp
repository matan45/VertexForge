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

    // VK-1518: geometry-occlusion verdict for a playing 3D source (AudioSceneUpdater's
    // round-robin listener->emitter raycast). Separate from SetSoundTransformCommand because
    // that one is dirty-gated on movement and runs at a different cadence — a stationary
    // emitter behind a closing door still has to be told it is occluded.
    // occlusion: 0 = clear, 1 = blocked. The amounts are CUT amounts at full occlusion
    // (0 = inert), matching AudioSource3DComponent::occlusionLpf / occlusionVolume.
    struct SetSoundOcclusionCommand : ::events::ICommand<void> {
        services::AudioHandle handle;
        float occlusion = 0.0f;
        float lpfAmount = 0.0f;
        float volumeAmount = 0.0f;
        std::string_view getName() const override { return "SetSoundOcclusion"; }
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

    // VK-1513. Everything a caller needs to know about a handle it holds, in ONE query.
    //
    // Deliberately not a second query beside IsSoundPlayingQuery: the answer is read from a
    // snapshot that is returned BY VALUE (AudioThread::getSnapshot deep-copies its source
    // map), and the caller is AudioSceneUpdater, which asks once per 3D emitter per frame.
    // Two queries would be two deep copies per emitter per frame.
    //
    // `rejected` exists because "not playing" is ambiguous on its own: a handle the audio
    // thread has not reached yet and a handle it has thrown away look identical, and the
    // caller cannot wait forever to tell them apart.
    struct SoundStatusQuery : ::events::IQuery<types::SoundStatus> {
        services::AudioHandle handle;
        std::string_view getName() const override { return "SoundStatus"; }
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
