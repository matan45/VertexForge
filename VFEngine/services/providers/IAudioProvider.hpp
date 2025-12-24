#pragma once
#include <glm/glm.hpp>
#include <string>
#include <cstdint>

namespace services {

    using AudioHandleId = uint64_t;
    constexpr AudioHandleId InvalidAudioHandleId = 0;

    enum class AudioDistanceModel : uint8_t {
        InverseDistance,
        InverseDistanceClamped,
        LinearDistance,
        LinearDistanceClamped,
        ExponentDistance,
        ExponentDistanceClamped,
        None
    };

    struct AudioPlayParams {
        float volume = 1.0f;
        float pitch = 1.0f;
        bool loop = false;
        bool is3D = false;
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        float minDistance = 1.0f;
        float maxDistance = 100.0f;
        float rolloffFactor = 1.0f;
        bool streaming = false;  // Use streaming playback for long audio
    };

    class IAudioProvider {
    public:
        virtual ~IAudioProvider() = default;

        virtual bool init() = 0;
        virtual void cleanUp() = 0;
        virtual void update() = 0;
        virtual bool isInitialized() const = 0;

        virtual void setMasterVolume(float volume) = 0;
        virtual float getMasterVolume() const = 0;

        virtual AudioHandleId playSound(const std::string& path, const AudioPlayParams& params) = 0;
        virtual AudioHandleId playSound3D(const std::string& path, const glm::vec3& position,
                                           const AudioPlayParams& params) = 0;

        virtual void stopSound(AudioHandleId handle) = 0;
        virtual void pauseSound(AudioHandleId handle) = 0;
        virtual void resumeSound(AudioHandleId handle) = 0;
        virtual bool isPlaying(AudioHandleId handle) const = 0;

        virtual void setVolume(AudioHandleId handle, float volume) = 0;
        virtual void setPitch(AudioHandleId handle, float pitch) = 0;
        virtual void setPosition(AudioHandleId handle, const glm::vec3& position) = 0;
        virtual void setLooping(AudioHandleId handle, bool loop) = 0;

        virtual void stopAll() = 0;
        virtual void pauseAll() = 0;
        virtual void resumeAll() = 0;

        virtual void setListenerPosition(const glm::vec3& position, const glm::vec3& forward,
                                          const glm::vec3& up) = 0;
        virtual void setListenerVelocity(const glm::vec3& velocity) = 0;

        virtual void setDistanceModel(AudioDistanceModel model) = 0;
        virtual void setDopplerFactor(float factor) = 0;
        virtual void setSpeedOfSound(float speed) = 0;

        virtual bool loadBuffer(const std::string& path) = 0;
        virtual void unloadBuffer(const std::string& path) = 0;
        virtual bool isBufferLoaded(const std::string& path) const = 0;

        virtual size_t getActiveSourceCount() const = 0;
        virtual size_t getLoadedBufferCount() const = 0;

        // Streaming-specific methods
        virtual AudioHandleId playStreamingSound(const std::string& path,
                                                  const AudioPlayParams& params) = 0;
        virtual AudioHandleId playStreamingSound3D(const std::string& path,
                                                    const glm::vec3& position,
                                                    const AudioPlayParams& params) = 0;
        virtual float getPlaybackPosition(AudioHandleId handle) const = 0;
        virtual bool setPlaybackPosition(AudioHandleId handle, float seconds) = 0;
        virtual float getDuration(AudioHandleId handle) const = 0;
        virtual bool isStreamingHandle(AudioHandleId handle) const = 0;
        virtual size_t getActiveStreamingCount() const = 0;
    };

}
