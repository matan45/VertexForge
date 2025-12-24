#pragma once
#include "AudioSource.hpp"
#include "../../utilities/resource/AudioResource.hpp"
#include <AL/al.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>

namespace core::audio {

    // Configuration for streaming buffers
    struct StreamingConfig {
        static constexpr size_t DEFAULT_BUFFER_COUNT = 4;
        static constexpr float DEFAULT_BUFFER_DURATION_SECONDS = 0.25f;

        size_t bufferCount = DEFAULT_BUFFER_COUNT;
        float bufferDurationSeconds = DEFAULT_BUFFER_DURATION_SECONDS;
    };

    // State of streaming playback
    enum class StreamingState : uint8_t {
        Stopped,
        Playing,
        Paused,
        Finished  // Reached EOF (non-looping)
    };

    // OpenAL streaming audio source with buffer queue
    class StreamingAudioSource {
    public:
        StreamingAudioSource();
        ~StreamingAudioSource();

        // Non-copyable, movable
        StreamingAudioSource(const StreamingAudioSource&) = delete;
        StreamingAudioSource& operator=(const StreamingAudioSource&) = delete;
        StreamingAudioSource(StreamingAudioSource&& other) noexcept;
        StreamingAudioSource& operator=(StreamingAudioSource&& other) noexcept;

        // Open audio file for streaming
        bool open(const std::string& path, const StreamingConfig& config = {});
        void close();
        [[nodiscard]] bool isOpen() const { return streamHandle != nullptr && streamHandle->isOpen(); }

        // Playback control
        void play();
        void pause();
        void stop();

        // Must be called regularly from update loop to refill buffers
        void update();

        // State queries
        [[nodiscard]] StreamingState getState() const { return state; }
        [[nodiscard]] bool isPlaying() const { return state == StreamingState::Playing; }
        [[nodiscard]] bool isPaused() const { return state == StreamingState::Paused; }
        [[nodiscard]] bool isStopped() const { return state == StreamingState::Stopped; }
        [[nodiscard]] bool isFinished() const { return state == StreamingState::Finished; }

        // Volume control (0.0 - 1.0)
        void setVolume(float volume);
        [[nodiscard]] float getVolume() const;

        // Pitch control (0.5 - 2.0)
        void setPitch(float pitch);
        [[nodiscard]] float getPitch() const;

        // Looping control
        void setLooping(bool loop);
        [[nodiscard]] bool isLooping() const { return looping; }

        // 3D positioning
        void setPosition(const glm::vec3& position);
        [[nodiscard]] glm::vec3 getPosition() const;

        void setVelocity(const glm::vec3& velocity);
        [[nodiscard]] glm::vec3 getVelocity() const;

        void set3D(bool is3D);
        [[nodiscard]] bool is3D() const { return spatialEnabled; }

        // 3D audio configuration
        void setMinDistance(float distance);
        [[nodiscard]] float getMinDistance() const;

        void setMaxDistance(float distance);
        [[nodiscard]] float getMaxDistance() const;

        void setRolloffFactor(float factor);
        [[nodiscard]] float getRolloffFactor() const;

        // Apply full configuration
        void applyConfig(const AudioSourceConfig& config);

        // Playback position
        [[nodiscard]] float getPlaybackPosition() const;
        bool setPlaybackPosition(float seconds);

        // Duration info
        [[nodiscard]] float getDuration() const;

        // Get OpenAL source ID (for debugging)
        [[nodiscard]] ALuint getSourceId() const { return sourceId; }

    private:
        // Initialize OpenAL buffers for streaming
        bool initBuffers();

        // Clean up OpenAL resources
        void cleanupBuffers();

        // Fill a single buffer with audio data
        bool fillBuffer(ALuint bufferId);

        // Queue a buffer to the source
        bool queueBuffer(ALuint bufferId);

        // Process finished buffers and refill them
        void processFinishedBuffers();

        // Get OpenAL format for current audio
        [[nodiscard]] ALenum getFormat() const;

        // Stream handle for file reading
        std::unique_ptr<resource::AudioStreamHandle> streamHandle;
        StreamingConfig config;

        // OpenAL resources
        ALuint sourceId = 0;
        std::vector<ALuint> bufferIds;
        std::vector<short> readBuffer;  // Reusable buffer for reading chunks

        // State
        StreamingState state = StreamingState::Stopped;
        bool looping = false;
        bool spatialEnabled = false;

        // Buffer configuration
        size_t samplesPerBuffer = 0;

        // Track total samples played for accurate position reporting
        size_t totalSamplesPlayed = 0;

        // Track actual samples in each buffer (for partial buffers at EOF)
        std::unordered_map<ALuint, size_t> bufferSampleCounts;
    };

}
