#pragma once

#include "AudioSourceManager.hpp"
#include "AudioPlayParams.hpp"
#include "types/AudioTypes.hpp"
#include "types/AudioEffectTypes.hpp"
#include <glm/glm.hpp>
#include <string>
#include <variant>
#include <deque>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <chrono>

namespace core::audio
{
    // --- Command types ---

    struct PlaySoundCmd
    {
        AudioHandle preAssignedHandle = InvalidAudioHandle;
        std::string path;
        PlaySoundParams params;
    };

    struct StopSoundCmd { AudioHandle handle; };
    struct PauseSoundCmd { AudioHandle handle; };
    struct ResumeSoundCmd { AudioHandle handle; };
    struct SetVolumeCmd { AudioHandle handle; float volume; };
    struct SetPitchCmd { AudioHandle handle; float pitch; };

    // Per-frame re-sync of a playing 3D source's transform (VK-1505: sounds follow
    // moving entities). velocity is plumbed for VK-1506 doppler but dispatched as 0 today.
    struct SetSourceTransformCmd
    {
        AudioHandle handle;
        glm::vec3 position;
        glm::vec3 direction;
        glm::vec3 velocity;
    };

    // VK-1518: latest geometry-occlusion verdict for a playing 3D source, plus the authored
    // cut amounts. Deliberately NOT folded into SetSourceTransformCmd: that command is
    // dirty-gated on movement, so a stationary emitter behind a closing door would never
    // re-dispatch. The two cadences are also different (transform: per-frame when moving;
    // occlusion: ~10 Hz round-robin). occlusion is 0 (clear) .. 1 (blocked); the amounts are
    // cut amounts at full occlusion (0 = inert), matching AudioSource3DComponent.
    struct SetSourceOcclusionCmd
    {
        AudioHandle handle;
        float occlusion;
        float lpfAmount;
        float volumeAmount;
    };

    struct SetListenerCmd
    {
        glm::vec3 position;
        glm::vec3 forward;
        glm::vec3 up;
        glm::vec3 velocity;  // VK-1506 doppler (0 on the editor path)
    };

    struct SetPlaybackPosCmd
    {
        AudioHandle handle;
        float seconds;
    };

    struct ApplySettingsCmd { types::AudioSettings settings; };

    struct BusVolumeCmd { std::string busName; float volume; };
    struct BusMuteCmd { std::string busName; bool muted; };
    struct BusSoloCmd { std::string busName; bool soloed; };
    struct CreateBusCmd { std::string name; std::string parentName; };

    struct AddBusEffectCmd
    {
        std::string busName;
        types::BusEffectConfig config;
    };

    struct RemoveBusEffectCmd
    {
        std::string busName;
        uint32_t effectId;
    };

    struct UpdateBusEffectCmd
    {
        std::string busName;
        uint32_t effectId;
        types::BusEffectConfig config;
    };

    struct SetBusEffectEnabledCmd
    {
        std::string busName;
        uint32_t effectId;
        bool enabled;
    };

    struct SetBusEffectWetDryCmd
    {
        std::string busName;
        uint32_t effectId;
        float wetDry;
    };

    struct LoadSnapshotCmd { std::string name; };
    struct SaveSnapshotCmd { std::string name; };
    struct DeleteSnapshotCmd { std::string name; };
    struct UnloadBufferCmd { std::string path; };
    struct FadeOutAndReleaseCmd { AudioHandle handle; float fadeDurationMs = 300.0f; };
    struct StopAllCmd {};
    struct ShutdownCmd {};
    // VK-1515: gates the active-sounds overlay's per-voice capture. A command rather than a
    // direct atomic store so the audio thread owns the accumulator and row reset, like every
    // other mutation of its state.
    struct SetVoiceDebugCmd { bool enabled; };

    using AudioCommand = std::variant<
        PlaySoundCmd, StopSoundCmd, PauseSoundCmd, ResumeSoundCmd,
        SetVolumeCmd, SetPitchCmd, SetSourceTransformCmd, SetSourceOcclusionCmd,
        SetListenerCmd, SetPlaybackPosCmd,
        ApplySettingsCmd, BusVolumeCmd, BusMuteCmd, BusSoloCmd, CreateBusCmd,
        AddBusEffectCmd, RemoveBusEffectCmd, UpdateBusEffectCmd,
        SetBusEffectEnabledCmd, SetBusEffectWetDryCmd,
        LoadSnapshotCmd, SaveSnapshotCmd, DeleteSnapshotCmd,
        UnloadBufferCmd, FadeOutAndReleaseCmd, StopAllCmd, ShutdownCmd,
        SetVoiceDebugCmd
    >;

    class AudioCommandQueue
    {
    public:
        void enqueue(AudioCommand cmd);
        bool tryDequeueAll(std::vector<AudioCommand>& out);
        void waitForCommands(std::chrono::milliseconds timeout);
        void notify();

    private:
        std::deque<AudioCommand> queue;
        std::mutex mutex;
        std::condition_variable cv;
    };
}
