#pragma once
#include <AL/al.h>
#include <glm/glm.hpp>
#include <cstdint>

namespace core::audio {

    enum class AudioSourceState : uint8_t {
        Initial,
        Playing,
        Paused,
        Stopped
    };

    struct AudioSourceConfig {
        float volume = 1.0f;
        float pitch = 1.0f;
        bool loop = false;
        bool is3D = false;
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        float minDistance = 1.0f;
        float maxDistance = 100.0f;
        float rolloffFactor = 1.0f;
        float coneInnerAngle = 360.0f;
        float coneOuterAngle = 360.0f;
        float coneOuterGain = 0.0f;
        glm::vec3 direction{0.0f, 0.0f, -1.0f};
    };

    class AudioSource {
    public:
        AudioSource();
        ~AudioSource();

        AudioSource(const AudioSource&) = delete;
        AudioSource& operator=(const AudioSource&) = delete;
        AudioSource(AudioSource&& other) noexcept;
        AudioSource& operator=(AudioSource&& other) noexcept;

        bool isValid() const { return sourceId != 0; }
        ALuint getId() const { return sourceId; }

        void setBuffer(ALuint bufferId);
        ALuint getBuffer() const;

        void play();
        void pause();
        void stop();
        void rewind();

        AudioSourceState getState() const;
        bool isPlaying() const;
        bool isPaused() const;
        bool isStopped() const;

        void setVolume(float volume);
        float getVolume() const;

        void setPitch(float pitch);
        float getPitch() const;

        void setLooping(bool loop);
        bool isLooping() const;

        void setPosition(const glm::vec3& position);
        glm::vec3 getPosition() const;

        void setVelocity(const glm::vec3& velocity);
        glm::vec3 getVelocity() const;

        void setDirection(const glm::vec3& direction);
        glm::vec3 getDirection() const;

        void set3D(bool is3D);
        bool is3D() const;

        void setMinDistance(float distance);
        float getMinDistance() const;

        void setMaxDistance(float distance);
        float getMaxDistance() const;

        void setRolloffFactor(float factor);
        float getRolloffFactor() const;

        void setConeAngles(float innerAngle, float outerAngle);
        void setConeOuterGain(float gain);

        void applyConfig(const AudioSourceConfig& config);

        float getPlaybackPosition() const;
        void setPlaybackPosition(float seconds);

    private:
        ALuint sourceId = 0;
        bool spatialEnabled = false;
    };

}
