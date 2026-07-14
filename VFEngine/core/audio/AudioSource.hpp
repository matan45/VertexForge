#pragma once
#include <AL/al.h>
#include <glm/glm.hpp>
#include <cstdint>

namespace core::audio
{
    enum class AudioSourceState : uint8_t
    {
        Initial,
        Playing,
        Paused,
        Stopped
    };

    struct AudioSourceConfig
    {
        float volume = 1.0f;
        float pitch = 1.0f;
        bool loop = false;
        bool is3D = false;
        glm::vec3 position{0.0f};
        float minDistance = 1.0f;
        float maxDistance = 100.0f;
        float rolloffFactor = 1.0f;

        bool enableDistanceFilter = false;
        float filterStartDistance = 10.0f;
        float filterMaxDistance = 100.0f;
        float filterIntensity = 1.0f;

        float innerConeAngle = 360.0f;
        float outerConeAngle = 360.0f;
        float outerConeGain = 0.0f;
        glm::vec3 direction{0.0f, 0.0f, -1.0f};
    };

    class AudioSource
    {
    private:
        ALuint sourceId = 0;
        bool spatialEnabled = false;

        ALuint filterId = 0;
        float currentGainHF = 1.0f;
        bool distanceFilterEnabled = false;
        float filterStartDistance = 10.0f;
        float filterMaxDistance = 100.0f;
        float filterIntensity = 1.0f;

    public:
        explicit AudioSource();
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

        // AL_VELOCITY for doppler (VK-1506). Set to 0 by VK-1505's follow loop.
        void setVelocity(const glm::vec3& velocity);

        void set3D(bool is3D);

        void setMinDistance(float distance);
        float getMinDistance() const;

        void setMaxDistance(float distance);
        float getMaxDistance() const;

        void setRolloffFactor(float factor);

        void applyConfig(const AudioSourceConfig& config);

        float getPlaybackPosition() const;
        void setPlaybackPosition(float seconds);

        void setDirection(const glm::vec3& dir);
        void setConeInnerAngle(float degrees);
        void setConeOuterAngle(float degrees);
        void setConeOuterGain(float gain);

        void initFilter();
        void cleanUpFilter();
        void setDistanceFilterParams(bool enabled, float startDist, float maxDist, float intensity);
        void updateDistanceFilter(float distance, float deltaTime);
        void detachFilter();
    };
}
