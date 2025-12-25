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
        Finished 
    };

    
    class StreamingAudioSource {
    private:
        std::unique_ptr<resource::AudioStreamHandle> streamHandle;
        StreamingConfig config;
        
        ALuint sourceId = 0;
        std::vector<ALuint> bufferIds;
        std::vector<short> readBuffer;  // Reusable buffer for reading chunks

        // State
        StreamingState state = StreamingState::Stopped;
        bool looping = false;
        bool spatialEnabled = false;
        
        size_t samplesPerBuffer = 0;
        
        size_t totalSamplesPlayed = 0;
        
        std::unordered_map<ALuint, size_t> bufferSampleCounts;
        
    public:
        explicit StreamingAudioSource();
        ~StreamingAudioSource();

        // Non-copyable, movable
        StreamingAudioSource(const StreamingAudioSource&) = delete;
        StreamingAudioSource& operator=(const StreamingAudioSource&) = delete;
        StreamingAudioSource(StreamingAudioSource&& other) noexcept;
        StreamingAudioSource& operator=(StreamingAudioSource&& other) noexcept;
        
        bool open(const std::string& path, const StreamingConfig& config = {});
        void close();
        bool isOpen() const { return streamHandle != nullptr && streamHandle->isOpen(); }
        
        void play();
        void pause();
        void stop();

        // Must be called regularly from update loop to refill buffers
        void update();
        
        bool isPlaying() const { return state == StreamingState::Playing; }
        bool isFinished() const { return state == StreamingState::Finished; }
        
        void setVolume(float volume);
        float getVolume() const;
        
        void setPitch(float pitch);
        float getPitch() const;
        
        void setLooping(bool loop);
        bool isLooping() const { return looping; }

        // Apply full configuration
        void applyConfig(const AudioSourceConfig& config);
        
        float getPlaybackPosition() const;
        bool setPlaybackPosition(float seconds);
        
        float getDuration() const;
        
        ALuint getSourceId() const { return sourceId; }

    private:
        bool initBuffers();
        
        void cleanupBuffers();
        
        bool fillBuffer(ALuint bufferId);
        
        bool queueBuffer(ALuint bufferId);
        
        void processFinishedBuffers();
        
        ALenum getFormat() const;
    };

}
