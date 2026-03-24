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
#include <thread>
#include <atomic>
#include <unordered_map>
#include <unordered_set>
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

    private:
        void threadLoop();
        void processCommand(AudioCommand& cmd);
        void publishSnapshot();

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

        std::chrono::steady_clock::time_point lastUpdateTime;
    };
}
