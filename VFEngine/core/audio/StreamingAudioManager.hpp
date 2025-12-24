#pragma once
#include "StreamingAudioSource.hpp"
#include "AudioSourceManager.hpp"  // For AudioHandle type
#include <unordered_map>
#include <memory>
#include <string>

namespace core::audio {

    // Handle range for streaming sources (high bit set to distinguish from regular handles)
    constexpr AudioHandle StreamingHandleBase = 0x8000000000000000ULL;

    class StreamingAudioManager {
    public:
        StreamingAudioManager() = default;
        ~StreamingAudioManager();

        StreamingAudioManager(const StreamingAudioManager&) = delete;
        StreamingAudioManager& operator=(const StreamingAudioManager&) = delete;

        // Play a streaming audio file, returns handle for control
        AudioHandle playStreaming(const std::string& path, const AudioSourceConfig& config,
                                   const StreamingConfig& streamConfig = {});

        // Control existing streaming sources
        void stop(AudioHandle handle);
        void pause(AudioHandle handle);
        void resume(AudioHandle handle);

        void setVolume(AudioHandle handle, float volume);
        void setPitch(AudioHandle handle, float pitch);
        void setPosition(AudioHandle handle, const glm::vec3& position);
        void setLooping(AudioHandle handle, bool loop);

        [[nodiscard]] bool isPlaying(AudioHandle handle) const;
        [[nodiscard]] float getPlaybackPosition(AudioHandle handle) const;
        bool setPlaybackPosition(AudioHandle handle, float seconds);
        [[nodiscard]] float getDuration(AudioHandle handle) const;

        // Get streaming source (for advanced control)
        StreamingAudioSource* getSource(AudioHandle handle);
        [[nodiscard]] const StreamingAudioSource* getSource(AudioHandle handle) const;

        // Check if handle is a streaming handle
        [[nodiscard]] static bool isStreamingHandle(AudioHandle handle) {
            return (handle & StreamingHandleBase) != 0;
        }

        // Stop all streaming sources
        void stopAll();
        void pauseAll();
        void resumeAll();

        // Must be called from main update loop
        void update();

        // Get count of active streaming sources
        [[nodiscard]] size_t getActiveCount() const { return activeSources.size(); }

    private:
        AudioHandle generateHandle();
        void cleanupFinishedSources();

        std::unordered_map<AudioHandle, std::unique_ptr<StreamingAudioSource>> activeSources;
        uint64_t nextHandleId = 1;
    };

}
