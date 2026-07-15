#pragma once

#include "AudioCommandQueue.hpp"
#include "AudioSourceManager.hpp"
#include "StreamingAudioManager.hpp"
#include "AudioBusManager.hpp"
#include "AudioEffectManager.hpp"
#include "AudioBufferManager.hpp"
#include "AudioListener.hpp"
#include "AudioSystem.hpp"
#include "ReverbZoneManager.hpp"
#include "VoicePolicy.hpp"
#include <thread>
#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <chrono>

namespace core::audio
{
    struct AudioStateSnapshot
    {
        struct SourceState
        {
            bool playing = false;
            float playbackPosition = 0.0f;
            float duration = 0.0f;
        };
        std::unordered_map<AudioHandle, SourceState> sources;
    };

    class AudioThread
    {
    public:
        struct Dependencies
        {
            AudioSystem* audioSystem = nullptr;
            AudioSourceManager* sourceManager = nullptr;
            StreamingAudioManager* streamingManager = nullptr;
            AudioBusManager* busManager = nullptr;
            AudioEffectManager* effectManager = nullptr;
            AudioBufferManager* bufferManager = nullptr;
            AudioListener* listener = nullptr;
            ReverbZoneManager* reverbZoneManager = nullptr;
        };

        explicit AudioThread(AudioCommandQueue& commandQueue, Dependencies deps);
        ~AudioThread();

        AudioThread(const AudioThread&) = delete;
        AudioThread& operator=(const AudioThread&) = delete;

        void start();
        void stop();

        AudioStateSnapshot getSnapshot() const;

        // VK-1513: live count of pooled (non-streaming) voices, for the editor's
        // "Real voices: N / M" readout. Written on the audio thread, read on the main
        // thread — an atomic rather than a snapshot field because getSnapshot() returns
        // by value and would deep-copy a map on every poll. Mirrors AudioSystem's
        // hrtfStatus (VK-1508).
        int getRealVoiceCount() const { return realVoiceCount.load(std::memory_order_relaxed); }
        int getMaxRealVoices() const { return maxRealVoices.load(std::memory_order_relaxed); }

    private:
        void threadLoop();
        void processCommand(AudioCommand& cmd);
        void publishSnapshot(float deltaTime);

        // VK-1513 voice limiting.
        // What the pooled path retains about a playing voice. OpenAL has no priority
        // concept and stores no asset/bus identity, so the arbitration inputs have to be
        // mirrored CPU-side. Streaming voices are deliberately absent: they never draw
        // from the source pool this budget governs (see the PlaySoundCmd branch).
        struct VoiceRecord
        {
            AudioHandle internal = InvalidAudioHandle;
            ALuint bufferId = 0;
            uint8_t priority = 128;
            bool is3D = false;
            glm::vec3 position{0.0f};
            AttenuationParams atten{};
        };

        std::vector<VoiceCandidate> gatherLiveVoices() const;
        VoiceCandidate makeCandidate(const PlaySoundCmd& command) const;
        // Hard-stops a voice and drops every trace of it. Used for steal victims.
        void releaseVoice(AudioHandle externalHandle);
        void forgetVoice(AudioHandle externalHandle);

        AudioCommandQueue& commandQueue;
        Dependencies deps;

        std::thread thread;
        std::atomic<bool> running{false};

        // Double-buffered snapshot for lock-free main-thread reads
        AudioStateSnapshot snapshots[2];
        std::atomic<int> readIndex{0};

        // Listener position cache (updated via commands)
        glm::vec3 listenerPosition{0.0f};

        // Track active handles for snapshot publishing
        std::unordered_set<AudioHandle> activeHandles;

        // Map pre-assigned (external) handles to internal handles from source/streaming managers
        std::unordered_map<AudioHandle, AudioHandle> externalToInternal;
        AudioHandle resolveHandle(AudioHandle externalHandle) const;

        // VK-1513: arbitration state for pooled voices, keyed by external handle.
        std::unordered_map<AudioHandle, VoiceRecord> voiceRecords;
        std::vector<SourceMeterSample> sourceMeterSamples;
        std::atomic<int> maxRealVoices{kDefaultMaxRealVoices}; // <= 0 disables the cap
        std::atomic<int> realVoiceCount{0};
        // Audio-thread-only; refreshed by ApplySettingsCmd. Matches AudioSettings' default.
        types::AudioDistanceModel distanceModel = types::AudioDistanceModel::InverseDistanceClamped;

        std::chrono::steady_clock::time_point lastUpdateTime;
    };
}
