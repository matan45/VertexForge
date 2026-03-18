#pragma once

#include "AudioSourceManager.hpp"
#include "AudioPlayParams.hpp"
#include "types/AudioTypes.hpp"
#include "types/AudioEffectTypes.hpp"
#include <glm/glm.hpp>
#include <string>
#include <variant>
#include <future>
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
        std::string path;
        PlaySoundParams params;
        std::promise<AudioHandle> result;
    };

    struct StopSoundCmd { AudioHandle handle; };
    struct PauseSoundCmd { AudioHandle handle; };
    struct ResumeSoundCmd { AudioHandle handle; };
    struct SetVolumeCmd { AudioHandle handle; float volume; };
    struct SetPitchCmd { AudioHandle handle; float pitch; };

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
        std::promise<bool> result;
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
        std::promise<bool> result;
    };

    struct RemoveBusEffectCmd
    {
        std::string busName;
        uint32_t effectId;
        std::promise<bool> result;
    };

    struct UpdateBusEffectCmd
    {
        std::string busName;
        uint32_t effectId;
        types::BusEffectConfig config;
        std::promise<bool> result;
    };

    struct SetBusEffectEnabledCmd
    {
        std::string busName;
        uint32_t effectId;
        bool enabled;
        std::promise<bool> result;
    };

    struct SetBusEffectWetDryCmd
    {
        std::string busName;
        uint32_t effectId;
        float wetDry;
        std::promise<bool> result;
    };

    struct LoadSnapshotCmd { std::string name; };
    struct SaveSnapshotCmd { std::string name; };
    struct DeleteSnapshotCmd { std::string name; };
    struct UnloadBufferCmd { std::string path; };
    struct ShutdownCmd {};

    using AudioCommand = std::variant<
        PlaySoundCmd, StopSoundCmd, PauseSoundCmd, ResumeSoundCmd,
        SetVolumeCmd, SetPitchCmd, SetListenerCmd, SetPlaybackPosCmd,
        ApplySettingsCmd, BusVolumeCmd, BusMuteCmd, BusSoloCmd, CreateBusCmd,
        AddBusEffectCmd, RemoveBusEffectCmd, UpdateBusEffectCmd,
        SetBusEffectEnabledCmd, SetBusEffectWetDryCmd,
        LoadSnapshotCmd, SaveSnapshotCmd, DeleteSnapshotCmd,
        UnloadBufferCmd, ShutdownCmd
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
