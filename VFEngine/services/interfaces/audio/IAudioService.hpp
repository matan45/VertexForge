#pragma once
#include "../../data/EntityHandle.hpp"
#include <glm/glm.hpp>
#include <string>

namespace services {

    struct AudioHandle {
        uint64_t id = 0;
        bool isValid() const { return id != 0; }
    };

    struct AudioParams {
        float volume = 1.0f;
        float pitch = 1.0f;
        bool loop = false;
        bool is3D = false;
        glm::vec3 position{ 0.0f };
        float minDistance = 1.0f;
        float maxDistance = 100.0f;
        float rolloffFactor = 1.0f;
        bool streaming = false;

        bool enableDistanceFilter = true;
        float filterStartDistance = 10.0f;
        float filterMaxDistance = 100.0f;
        float filterIntensity = 1.0f;
    };

    class IAudioService {
    public:
        virtual ~IAudioService() = default;

        virtual void registerEventHandlers() = 0;

        // === Listener (Camera/Player) ===

        virtual void setListenerPosition(const glm::vec3& position,
                                         const glm::vec3& forward,
                                         const glm::vec3& up = glm::vec3(0, 1, 0)) = 0;

        // === Sound Playback ===

        virtual AudioHandle playSound3D(const std::string& path, const glm::vec3& position,
                                        const AudioParams& params = {}) = 0;

        virtual AudioHandle playStreamingSound(const std::string& path, const AudioParams& params = {}) = 0;

        // === Sound Control ===

        virtual void stopSound(AudioHandle handle) = 0;

        virtual void pauseSound(AudioHandle handle) = 0;

        virtual void resumeSound(AudioHandle handle) = 0;

        virtual bool isPlaying(AudioHandle handle) const = 0;

        virtual void setVolume(AudioHandle handle, float volume) = 0;

        virtual void setPitch(AudioHandle handle, float pitch) = 0;

        // === Playback Position ===

        virtual float getPlaybackPosition(AudioHandle handle) const = 0;

        virtual bool setPlaybackPosition(AudioHandle handle, float seconds) = 0;

        virtual float getDuration(AudioHandle handle) const = 0;
    };

}
