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
#include "types/AudioTypes.hpp" // types::AudioVoiceRow — the overlay's per-voice payload
#include <thread>
#include <atomic>
#include <shared_mutex>
#include <string>
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
        // VK-1515: voices on a simulated clock, holding no AL source. Not bounded by
        // maxRealVoices — that budget exists to ration AL sources, which these do not use.
        int getVirtualVoiceCount() const { return virtualVoiceCount.load(std::memory_order_relaxed); }

        // VK-1515: one row per live voice, for the editor's active-sounds overlay. Empty
        // unless capture has been switched on with SetVoiceDebugCmd. Gated because, off,
        // the audio thread pays one relaxed load per tick, whereas on it builds N rows of
        // strings every kVoiceDebugInterval — a diagnostic must not perturb the thread it
        // is diagnosing. The overlay drives the gate from its window's visibility.
        std::vector<types::AudioVoiceRow> getVoiceDebugRows() const;

    private:
        void threadLoop();
        void processCommand(AudioCommand& cmd);
        void publishSnapshot(float deltaTime);

        // VK-1513 voice limiting.
        // What the pooled path retains about a playing voice. OpenAL has no priority
        // concept and stores no asset/bus identity, so the arbitration inputs have to be
        // mirrored CPU-side. Streaming voices are deliberately absent: they never draw
        // from the source pool this budget governs (see the PlaySoundCmd branch).
        //
        // VK-1515 widened this from the bare arbitration inputs to the whole play request.
        // Virtualization has to be able to replay a voice exactly as it was first asked for
        // — pitch, cone, distance filter and all — and the overlay needs its asset and bus.
        // `params` is the single source of truth for the voice's mutable state: the command
        // branches write volume/pitch/position back into it, so a record can never disagree
        // with the AL source it mirrors (a second copy of `position` did exactly that).
        struct VoiceRecord
        {
            AudioHandle internal = InvalidAudioHandle;
            ALuint bufferId = 0;
            std::string path;
            PlaySoundParams params;
        };

        // VK-1515. A voice that lost the real-voice budget but is kept alive on a simulated
        // playback clock, so it can be revived at the right offset once it is worth hearing
        // again. Holds no AL source and no pool slot — it costs a record and a float add.
        struct VirtualVoice
        {
            std::string path;
            PlaySoundParams params;
            ALuint bufferId = 0;
            float clock = 0.0f;    // simulated playback position, seconds
            float duration = 0.0f; // 0 = unknown: never expires on its own
            bool paused = false;
        };

        // VK-1515. Streaming voices are exempt from the budget and get no VoiceRecord, but
        // the overlay still has to name them — and StreamingAudioSource retains no path of
        // its own, so this is the only place a stream's identity survives. Deliberately its
        // own map: realVoiceCount IS voiceRecords.size(), so putting streams in there would
        // inflate the count and silently shrink the real budget.
        struct StreamingVoiceRecord
        {
            std::string path;
            std::string busName;
        };

        std::vector<VoiceCandidate> gatherLiveVoices() const;
        std::vector<VoiceCandidate> gatherVirtualVoices() const;
        VoiceCandidate makeCandidate(const PlaySoundCmd& command) const;
        // Hard-stops a voice and drops every trace of it. Used for steal victims.
        void releaseVoice(AudioHandle externalHandle);
        void forgetVoice(AudioHandle externalHandle);

        // VK-1515 virtualization.
        void virtualizeVoice(AudioHandle externalHandle, std::string path,
                             const PlaySoundParams& params, ALuint bufferId,
                             float startClock, bool paused);
        void demoteVoice(AudioHandle externalHandle); // real -> virtual, keeping the clock
        bool reviveVoice(AudioHandle externalHandle); // virtual -> real, seeking to the clock
        void updateVirtualClocks(float deltaTime);
        void rebalanceRealVirtual(float deltaTime);
        // VK-1515 debug capture. Returns true on the ticks the overlay's rows are rebuilt.
        bool shouldPublishVoiceDebug(float deltaTime);
        void commitVoiceDebug();
        // The mutable state of a voice, whichever registry currently owns it. Returns null
        // for streaming voices (which keep no params) and unknown handles.
        PlaySoundParams* findVoiceParams(AudioHandle externalHandle);

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
        // VK-1515: the other two registries a live handle can live in. Every handle in
        // activeHandles is in exactly one of the three (or is a streaming voice, which is
        // also in externalToInternal). Keyed by external handle, like voiceRecords.
        std::unordered_map<AudioHandle, VirtualVoice> virtualVoices;
        std::unordered_map<AudioHandle, StreamingVoiceRecord> streamingRecords;
        std::vector<SourceMeterSample> sourceMeterSamples;
        std::atomic<int> maxRealVoices{kDefaultMaxRealVoices}; // <= 0 disables the cap
        std::atomic<int> realVoiceCount{0};
        std::atomic<int> virtualVoiceCount{0};

        // VK-1515: rebalance is rate-limited rather than run every tick — it sorts both
        // voice sets and does an alGetSourcef per live voice, and nothing it reacts to
        // (listener or emitter motion) moves at the 200Hz tick rate. 20Hz is well inside
        // the reaction time for "walked into earshot".
        float rebalanceAccum = 0.0f;
        static constexpr float kRebalanceInterval = 0.05f;

        // VK-1515 debug capture.
        //
        // A mutex, deliberately, rather than the snapshots[2]/readIndex pair above. That
        // scheme computes writeIdx = 1 - readIndex, so two ticks (10ms) after a reader
        // starts copying, the audio thread reclaims the buffer under it. That is tolerable
        // for a small POD map; a reader deep-copying N rows of std::string widens the window
        // materially. This mirrors VK-1514's meterEntries instead (AudioBusManager), which
        // is the same shape of data and which the audio thread already locks every tick.
        mutable std::shared_mutex voiceDebugMutex;
        std::vector<types::AudioVoiceRow> voiceDebugRows;    // guarded by voiceDebugMutex
        std::vector<types::AudioVoiceRow> voiceDebugScratch; // audio-thread only; keeps capacity
        std::atomic<bool> voiceDebugEnabled{false};
        float voiceDebugAccum = 0.0f;
        // 10Hz. The overlay polls at 2Hz, so the tick rate would be 100x waste.
        static constexpr float kVoiceDebugInterval = 0.1f;
        // Audio-thread-only; refreshed by ApplySettingsCmd. Matches AudioSettings' default.
        types::AudioDistanceModel distanceModel = types::AudioDistanceModel::InverseDistanceClamped;

        std::chrono::steady_clock::time_point lastUpdateTime;
    };
}
