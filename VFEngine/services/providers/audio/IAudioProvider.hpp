#pragma once
#include <glm/glm.hpp>
#include <string>
#include <cstdint>
#include "types/AudioTypes.hpp"

namespace services {

    using AudioHandleId = uint64_t;
    constexpr AudioHandleId InvalidAudioHandleId = 0;

    struct AudioPlayParams {
        float volume = 1.0f;
        float pitch = 1.0f;
        bool loop = false;
        bool is3D = false;
        glm::vec3 position{0.0f};
        float minDistance = 1.0f;
        float maxDistance = 100.0f;
        float rolloffFactor = 1.0f;
        bool streaming = false;

        bool enableDistanceFilter = true;
        float filterStartDistance = 10.0f;
        float filterMaxDistance = 100.0f;
        float filterIntensity = 1.0f;
    };

    class IAudioProvider {
    public:
        virtual ~IAudioProvider() = default;

        virtual bool init() = 0;
        virtual void cleanUp() = 0;
        virtual void update() = 0;
        virtual bool isInitialized() const = 0;

        // === Sound Playback ===
        virtual AudioHandleId playSound3D(const std::string& path, const glm::vec3& position,
                                           const AudioPlayParams& params) = 0;
        virtual AudioHandleId playStreamingSound(const std::string& path,
                                                  const AudioPlayParams& params) = 0;

        // === Sound Control ===
        virtual void stopSound(AudioHandleId handle) = 0;
        virtual void pauseSound(AudioHandleId handle) = 0;
        virtual void resumeSound(AudioHandleId handle) = 0;
        virtual bool isPlaying(AudioHandleId handle) const = 0;
        virtual void setVolume(AudioHandleId handle, float volume) = 0;
        virtual void setPitch(AudioHandleId handle, float pitch) = 0;

        // === Listener ===
        virtual void setListenerPosition(const glm::vec3& position, const glm::vec3& forward,
                                          const glm::vec3& up) = 0;

        // === Playback Position ===
        virtual float getPlaybackPosition(AudioHandleId handle) const = 0;
        virtual bool setPlaybackPosition(AudioHandleId handle, float seconds) = 0;
        virtual float getDuration(AudioHandleId handle) const = 0;

        // === Audio Settings ===
        virtual void applySettings(const types::AudioSettings& settings) = 0;
        virtual types::AudioSettings getCurrentSettings() const = 0;
    };

}
