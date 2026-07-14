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

    struct SetListenerCmd
    {
        glm::vec3 position;
        glm::vec3 forward;
        glm::vec3 up;
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

    using AudioCommand = std::variant<
        PlaySoundCmd, StopSoundCmd, PauseSoundCmd, ResumeSoundCmd,
        SetVolumeCmd, SetPitchCmd, SetSourceTransformCmd, SetListenerCmd, SetPlaybackPosCmd,
        ApplySettingsCmd, BusVolumeCmd, BusMuteCmd, BusSoloCmd, CreateBusCmd,
        AddBusEffectCmd, RemoveBusEffectCmd, UpdateBusEffectCmd,
        SetBusEffectEnabledCmd, SetBusEffectWetDryCmd,
        LoadSnapshotCmd, SaveSnapshotCmd, DeleteSnapshotCmd,
        UnloadBufferCmd, FadeOutAndReleaseCmd, StopAllCmd, ShutdownCmd
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
