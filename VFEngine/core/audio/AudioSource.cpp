#include "AudioSource.hpp"
#include "AudioSystem.hpp"
#include <algorithm>

namespace core::audio
{
    AudioSource::AudioSource()
    {
        alGenSources(1, &sourceId);
        if (AudioSystem::checkError("alGenSources"))
        {
            sourceId = 0;
            return;
        }

        alSourcef(sourceId, AL_GAIN, 1.0f);
        alSourcef(sourceId, AL_PITCH, 1.0f);
        alSourcei(sourceId, AL_LOOPING, AL_FALSE);
        alSourcei(sourceId, AL_SOURCE_RELATIVE, AL_TRUE);
        alSource3f(sourceId, AL_POSITION, 0.0f, 0.0f, 0.0f);

        initFilter();
    }

    AudioSource::~AudioSource()
    {
        if (sourceId != 0)
        {
            alGetError();

            alSourceStop(sourceId);
            alSourcei(sourceId, AL_BUFFER, 0);
            cleanUpFilter();
            alDeleteSources(1, &sourceId);

            alGetError();
            sourceId = 0;
        }
    }

    AudioSource::AudioSource(AudioSource&& other) noexcept
        : sourceId(other.sourceId), spatialEnabled(other.spatialEnabled),
          filterId(other.filterId), currentGainHF(other.currentGainHF),
          distanceFilterEnabled(other.distanceFilterEnabled),
          filterStartDistance(other.filterStartDistance),
          filterMaxDistance(other.filterMaxDistance),
          filterIntensity(other.filterIntensity)
    {
        other.sourceId = 0;
        other.spatialEnabled = false;
        other.filterId = 0;
        other.currentGainHF = 1.0f;
        other.distanceFilterEnabled = false;
    }

    AudioSource& AudioSource::operator=(AudioSource&& other) noexcept
    {
        if (this != &other)
        {
            if (sourceId != 0)
            {
                alSourceStop(sourceId);
                alSourcei(sourceId, AL_BUFFER, 0);
                cleanUpFilter();
                alDeleteSources(1, &sourceId);
            }
            sourceId = other.sourceId;
            spatialEnabled = other.spatialEnabled;
            filterId = other.filterId;
            currentGainHF = other.currentGainHF;
            distanceFilterEnabled = other.distanceFilterEnabled;
            filterStartDistance = other.filterStartDistance;
            filterMaxDistance = other.filterMaxDistance;
            filterIntensity = other.filterIntensity;
            other.sourceId = 0;
            other.spatialEnabled = false;
            other.filterId = 0;
            other.currentGainHF = 1.0f;
            other.distanceFilterEnabled = false;
        }
        return *this;
    }

    void AudioSource::setBuffer(ALuint bufferId)
    {
        if (!isValid()) return;

        auto state = getState();
        if (state != AudioSourceState::Initial && state != AudioSourceState::Stopped)
        {
            stop();
        }

        alSourcei(sourceId, AL_BUFFER, static_cast<ALint>(bufferId));
        AudioSystem::checkError("setBuffer");
    }

    ALuint AudioSource::getBuffer() const
    {
        if (!isValid()) return 0;

        ALint bufferId;
        alGetSourcei(sourceId, AL_BUFFER, &bufferId);
        return static_cast<ALuint>(bufferId);
    }

    void AudioSource::play()
    {
        if (!isValid()) return;
        alSourcePlay(sourceId);
        AudioSystem::checkError("play");
    }

    void AudioSource::pause()
    {
        if (!isValid()) return;
        alSourcePause(sourceId);
        AudioSystem::checkError("pause");
    }

    void AudioSource::stop()
    {
        if (!isValid()) return;
        alSourceStop(sourceId);
        AudioSystem::checkError("stop");
    }

    void AudioSource::rewind()
    {
        if (!isValid()) return;
        alSourceRewind(sourceId);
        AudioSystem::checkError("rewind");
    }

    AudioSourceState AudioSource::getState() const
    {
        if (!isValid()) return AudioSourceState::Stopped;

        ALint state;
        alGetSourcei(sourceId, AL_SOURCE_STATE, &state);

        switch (state)
        {
        case AL_INITIAL: return AudioSourceState::Initial;
        case AL_PLAYING: return AudioSourceState::Playing;
        case AL_PAUSED: return AudioSourceState::Paused;
        case AL_STOPPED: return AudioSourceState::Stopped;
        default: return AudioSourceState::Stopped;
        }
    }

    bool AudioSource::isPlaying() const
    {
        return getState() == AudioSourceState::Playing;
    }

    bool AudioSource::isPaused() const
    {
        return getState() == AudioSourceState::Paused;
    }

    bool AudioSource::isStopped() const
    {
        auto state = getState();
        return state == AudioSourceState::Stopped || state == AudioSourceState::Initial;
    }

    void AudioSource::setVolume(float volume)
    {
        if (!isValid()) return;
        alSourcef(sourceId, AL_GAIN, volume);
        AudioSystem::checkError("setVolume");
    }

    float AudioSource::getVolume() const
    {
        if (!isValid()) return 0.0f;

        float volume;
        alGetSourcef(sourceId, AL_GAIN, &volume);
        return volume;
    }

    void AudioSource::setPitch(float pitch)
    {
        if (!isValid()) return;
        alSourcef(sourceId, AL_PITCH, pitch);
        AudioSystem::checkError("setPitch");
    }

    float AudioSource::getPitch() const
    {
        if (!isValid()) return 1.0f;

        float pitch;
        alGetSourcef(sourceId, AL_PITCH, &pitch);
        return pitch;
    }

    void AudioSource::setLooping(bool loop)
    {
        if (!isValid()) return;
        alSourcei(sourceId, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);
        AudioSystem::checkError("setLooping");
    }

    bool AudioSource::isLooping() const
    {
        if (!isValid()) return false;

        ALint looping;
        alGetSourcei(sourceId, AL_LOOPING, &looping);
        return looping == AL_TRUE;
    }

    void AudioSource::setPosition(const glm::vec3& position)
    {
        if (!isValid()) return;
        alSource3f(sourceId, AL_POSITION, position.x, position.y, position.z);
        AudioSystem::checkError("setPosition");
    }

    glm::vec3 AudioSource::getPosition() const
    {
        if (!isValid()) return glm::vec3(0.0f);

        glm::vec3 position;
        alGetSource3f(sourceId, AL_POSITION, &position.x, &position.y, &position.z);
        return position;
    }

    void AudioSource::set3D(bool is3D)
    {
        if (!isValid()) return;

        spatialEnabled = is3D;
        alSourcei(sourceId, AL_SOURCE_RELATIVE, is3D ? AL_FALSE : AL_TRUE);
        AudioSystem::checkError("set3D");
    }

    void AudioSource::setMinDistance(float distance)
    {
        if (!isValid()) return;
        alSourcef(sourceId, AL_REFERENCE_DISTANCE, distance);
        AudioSystem::checkError("setMinDistance");
    }

    float AudioSource::getMinDistance() const
    {
        if (!isValid()) return 1.0f;

        float distance;
        alGetSourcef(sourceId, AL_REFERENCE_DISTANCE, &distance);
        return distance;
    }

    void AudioSource::setMaxDistance(float distance)
    {
        if (!isValid()) return;
        alSourcef(sourceId, AL_MAX_DISTANCE, distance);
        AudioSystem::checkError("setMaxDistance");
    }

    float AudioSource::getMaxDistance() const
    {
        if (!isValid()) return 100.0f;

        float distance;
        alGetSourcef(sourceId, AL_MAX_DISTANCE, &distance);
        return distance;
    }

    void AudioSource::setRolloffFactor(float factor)
    {
        if (!isValid()) return;
        alSourcef(sourceId, AL_ROLLOFF_FACTOR, factor);
        AudioSystem::checkError("setRolloffFactor");
    }

    void AudioSource::applyConfig(const AudioSourceConfig& config)
    {
        setVolume(config.volume);
        setPitch(config.pitch);
        setLooping(config.loop);
        set3D(config.is3D);
        setPosition(config.position);
        setMinDistance(config.minDistance);
        setMaxDistance(config.maxDistance);
        setRolloffFactor(config.rolloffFactor);
        setDistanceFilterParams(config.enableDistanceFilter, config.filterStartDistance,
                                config.filterMaxDistance, config.filterIntensity);
        setDirection(config.direction);
        setConeInnerAngle(config.innerConeAngle);
        setConeOuterAngle(config.outerConeAngle);
        setConeOuterGain(config.outerConeGain);
    }

    float AudioSource::getPlaybackPosition() const
    {
        if (!isValid()) return 0.0f;

        float seconds;
        alGetSourcef(sourceId, AL_SEC_OFFSET, &seconds);
        return seconds;
    }

    void AudioSource::setPlaybackPosition(float seconds)
    {
        if (!isValid()) return;
        alSourcef(sourceId, AL_SEC_OFFSET, seconds);
        AudioSystem::checkError("setPlaybackPosition");
    }

    void AudioSource::setDirection(const glm::vec3& dir)
    {
        if (!isValid()) return;
        alSource3f(sourceId, AL_DIRECTION, dir.x, dir.y, dir.z);
        AudioSystem::checkError("setDirection");
    }

    void AudioSource::setConeInnerAngle(float degrees)
    {
        if (!isValid()) return;
        alSourcef(sourceId, AL_CONE_INNER_ANGLE, degrees);
        AudioSystem::checkError("setConeInnerAngle");
    }

    void AudioSource::setConeOuterAngle(float degrees)
    {
        if (!isValid()) return;
        alSourcef(sourceId, AL_CONE_OUTER_ANGLE, degrees);
        AudioSystem::checkError("setConeOuterAngle");
    }

    void AudioSource::setConeOuterGain(float gain)
    {
        if (!isValid()) return;
        alSourcef(sourceId, AL_CONE_OUTER_GAIN, gain);
        AudioSystem::checkError("setConeOuterGain");
    }

    void AudioSource::initFilter()
    {
        if (!AudioSystem::isEfxAvailable()) return;

        AudioSystem::alGenFilters(1, &filterId);
        if (AudioSystem::checkError("alGenFilters"))
        {
            filterId = 0;
            return;
        }

        AudioSystem::alFilteri(filterId, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
        if (AudioSystem::checkError("alFilteri AL_FILTER_LOWPASS"))
        {
            AudioSystem::alDeleteFilters(1, &filterId);
            filterId = 0;
        }
    }

    void AudioSource::cleanUpFilter()
    {
        if (filterId != 0)
        {
            if (sourceId != 0)
            {
                alSourcei(sourceId, AL_DIRECT_FILTER, AL_FILTER_NULL);
            }
            AudioSystem::alDeleteFilters(1, &filterId);
            filterId = 0;
        }
        currentGainHF = 1.0f;
    }

    void AudioSource::setDistanceFilterParams(bool enabled, float startDist, float maxDist, float intensity)
    {
        distanceFilterEnabled = enabled;
        filterStartDistance = startDist;
        filterMaxDistance = maxDist;
        filterIntensity = std::clamp(intensity, 0.0f, 1.0f);
    }

    void AudioSource::updateDistanceFilter(float distance, float deltaTime)
    {
        if (!isValid() || filterId == 0) return;

        if (!distanceFilterEnabled)
        {
            if (currentGainHF < 1.0f)
            {
                detachFilter();
            }
            return;
        }

        float targetGainHF = 1.0f;
        if (distance >= filterMaxDistance)
        {
            targetGainHF = 1.0f - filterIntensity * 0.9f;
        }
        else if (distance > filterStartDistance)
        {
            float t = (distance - filterStartDistance) / (filterMaxDistance - filterStartDistance);
            targetGainHF = 1.0f - filterIntensity * 0.9f * t;
        }

        targetGainHF = std::clamp(targetGainHF, 0.1f, 1.0f);

        constexpr float smoothingRate = 10.0f;
        float lerpFactor = std::min(1.0f, deltaTime * smoothingRate);
        currentGainHF += (targetGainHF - currentGainHF) * lerpFactor;

        AudioSystem::alFilterf(filterId, AL_LOWPASS_GAINHF, currentGainHF);
        AudioSystem::alFilterf(filterId, AL_LOWPASS_GAIN, 1.0f);
        alSourcei(sourceId, AL_DIRECT_FILTER, static_cast<ALint>(filterId));
    }

    void AudioSource::detachFilter()
    {
        if (!isValid()) return;

        if (filterId != 0)
        {
            alSourcei(sourceId, AL_DIRECT_FILTER, AL_FILTER_NULL);
        }
        currentGainHF = 1.0f;
    }
}
