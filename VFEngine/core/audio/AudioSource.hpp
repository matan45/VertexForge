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

        // VK-1506 / VK-1518. These two joined the config late, and the reason they belong
        // here rather than on their own setters is the invariant applyConfig now carries:
        // it fully determines every AL property and CPU mirror on the source, so a pool
        // slot cannot inherit anything from its previous tenant. Velocity was the hole —
        // nothing reset AL_VELOCITY on recycle, so a fresh one-shot doppler-shifted at the
        // dead emitter's speed. Occlusion had resetOcclusionState() at every choke point,
        // which is the same idea spelled twice; the defaults below make it one.
        glm::vec3 velocity{0.0f};
        float occlusion = 0.0f;             // cut amounts, not gains: 0 == no cut
        float occlusionLpfAmount = 0.0f;
        float occlusionVolumeAmount = 0.0f;
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

        // VK-1518 geometry occlusion. occlusionTarget is the last verdict pushed by
        // SetSourceOcclusionCmd; currentOcclusion is its attack/release glide. The two
        // amounts are CUT amounts at full occlusion (0 = inert), like filterIntensity —
        // so 0 is the neutral value a pooled source resets to (see detachFilter).
        float occlusionTarget = 0.0f;
        float currentOcclusion = 0.0f;
        float occlusionLpfAmount = 0.0f;
        float occlusionVolumeAmount = 0.0f;

        void resetOcclusionState();

        // setOcclusion + land the glide on its target in one step, for a source whose
        // occlusion is being RESTORED rather than newly observed. A revived voice was
        // already muffled when it lost its slot, so gliding in from 0 over the attack time
        // would swell it audibly through the wall it is supposed to be behind.
        void settleOcclusion(float occlusion, float lpfAmount, float volumeAmount);

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

        // VK-1518: latest geometry-occlusion verdict for this voice (0 = clear, 1 = blocked)
        // plus the authored cut amounts. Pushed per-emitter from AudioSceneUpdater's raycast;
        // the glide and the AL writes happen in updateDistanceFilter.
        void setOcclusion(float occlusion, float lpfAmount, float volumeAmount);

        void updateDistanceFilter(float distance, float deltaTime);
        void detachFilter();
    };
}
