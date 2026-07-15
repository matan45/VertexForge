#pragma once
#include "AudioSource.hpp"
#include "StreamingMetering.hpp"
#include "resource/AudioResource.hpp"
#include <AL/al.h>
#include <vector>
#include <memory>
#include <string>
#include <optional>

namespace core::audio {
    
    struct StreamingConfig {
        static constexpr size_t DEFAULT_BUFFER_COUNT = 4;
        static constexpr float DEFAULT_BUFFER_DURATION_SECONDS = 0.25f;

        size_t bufferCount = DEFAULT_BUFFER_COUNT;
        float bufferDurationSeconds = DEFAULT_BUFFER_DURATION_SECONDS;
    };
    
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
        std::vector<short> readBuffer;

        // State
        StreamingState state = StreamingState::Stopped;
        bool looping = false;
        bool spatialEnabled = false;
        
        size_t samplesPerBuffer = 0;
        
        // VK-1514: mirrors the actual AL queue. Asset-relative starts keep the playhead
        // and meter correct when loop-head buffers sit behind still-audible tail buffers.
        std::vector<QueuedEnvelopeChunk> queuedChunks;
        size_t nextDecodedSample = 0;
        
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
        StreamingPlaybackMetrics getPlaybackMetrics() const;
        bool setPlaybackPosition(float seconds);
        
        float getDuration() const;
        
        ALuint getSourceId() const { return sourceId; }

    private:
        bool initBuffers();
        
        void cleanupBuffers();
        
        std::optional<QueuedEnvelopeChunk> fillBuffer(ALuint bufferId);
        
        bool queueBuffer(QueuedEnvelopeChunk chunk);

        bool fillAndQueueBuffer(ALuint bufferId);

        void resetQueuedState(size_t nextSample = 0);
        // Refill the AL queue from `sample` and restore `prevState`. Both exits of
        // setPlaybackPosition go through this — a failed seek must leave a usable stream.
        void refillAndRestore(size_t sample, StreamingState prevState);
        
        void processFinishedBuffers();
        
        ALenum getFormat() const;
    };

}
