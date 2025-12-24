#include "AudioSource.hpp"
#include "AudioSystem.hpp"
#include <spdlog/spdlog.h>
#include <utility>

namespace core::audio {

    AudioSource::AudioSource() {
        alGenSources(1, &sourceId);
        if (AudioSystem::checkError("alGenSources")) {
            sourceId = 0;
            return;
        }

        alSourcef(sourceId, AL_GAIN, 1.0f);
        alSourcef(sourceId, AL_PITCH, 1.0f);
        alSourcei(sourceId, AL_LOOPING, AL_FALSE);
        alSourcei(sourceId, AL_SOURCE_RELATIVE, AL_TRUE);
        alSource3f(sourceId, AL_POSITION, 0.0f, 0.0f, 0.0f);
        alSource3f(sourceId, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
    }

    AudioSource::~AudioSource() {
        if (sourceId != 0) {
            // Clear any pending errors first
            alGetError();

            alSourceStop(sourceId);
            alSourcei(sourceId, AL_BUFFER, 0);
            alDeleteSources(1, &sourceId);

            // Don't report errors during destruction - context may be invalid
            alGetError();
            sourceId = 0;
        }
    }

    AudioSource::AudioSource(AudioSource&& other) noexcept
        : sourceId(other.sourceId), spatialEnabled(other.spatialEnabled) {
        other.sourceId = 0;
        other.spatialEnabled = false;
    }

    AudioSource& AudioSource::operator=(AudioSource&& other) noexcept {
        if (this != &other) {
            if (sourceId != 0) {
                alSourceStop(sourceId);
                alSourcei(sourceId, AL_BUFFER, 0);
                alDeleteSources(1, &sourceId);
            }
            sourceId = other.sourceId;
            spatialEnabled = other.spatialEnabled;
            other.sourceId = 0;
            other.spatialEnabled = false;
        }
        return *this;
    }

    void AudioSource::setBuffer(ALuint bufferId) {
        if (!isValid()) return;

        auto state = getState();
        if (state != AudioSourceState::Initial && state != AudioSourceState::Stopped) {
            stop();
        }

        alSourcei(sourceId, AL_BUFFER, static_cast<ALint>(bufferId));
        AudioSystem::checkError("setBuffer");
    }

    ALuint AudioSource::getBuffer() const {
        if (!isValid()) return 0;

        ALint bufferId;
        alGetSourcei(sourceId, AL_BUFFER, &bufferId);
        return static_cast<ALuint>(bufferId);
    }

    void AudioSource::play() {
        if (!isValid()) return;
        alSourcePlay(sourceId);
        AudioSystem::checkError("play");
    }

    void AudioSource::pause() {
        if (!isValid()) return;
        alSourcePause(sourceId);
        AudioSystem::checkError("pause");
    }

    void AudioSource::stop() {
        if (!isValid()) return;
        alSourceStop(sourceId);
        AudioSystem::checkError("stop");
    }

    void AudioSource::rewind() {
        if (!isValid()) return;
        alSourceRewind(sourceId);
        AudioSystem::checkError("rewind");
    }

    AudioSourceState AudioSource::getState() const {
        if (!isValid()) return AudioSourceState::Stopped;

        ALint state;
        alGetSourcei(sourceId, AL_SOURCE_STATE, &state);

        switch (state) {
            case AL_INITIAL: return AudioSourceState::Initial;
            case AL_PLAYING: return AudioSourceState::Playing;
            case AL_PAUSED: return AudioSourceState::Paused;
            case AL_STOPPED: return AudioSourceState::Stopped;
            default: return AudioSourceState::Stopped;
        }
    }

    bool AudioSource::isPlaying() const {
        return getState() == AudioSourceState::Playing;
    }

    bool AudioSource::isPaused() const {
        return getState() == AudioSourceState::Paused;
    }

    bool AudioSource::isStopped() const {
        auto state = getState();
        return state == AudioSourceState::Stopped || state == AudioSourceState::Initial;
    }

    void AudioSource::setVolume(float volume) {
        if (!isValid()) return;
        alSourcef(sourceId, AL_GAIN, volume);
        AudioSystem::checkError("setVolume");
    }

    float AudioSource::getVolume() const {
        if (!isValid()) return 0.0f;

        float volume;
        alGetSourcef(sourceId, AL_GAIN, &volume);
        return volume;
    }

    void AudioSource::setPitch(float pitch) {
        if (!isValid()) return;
        alSourcef(sourceId, AL_PITCH, pitch);
        AudioSystem::checkError("setPitch");
    }

    float AudioSource::getPitch() const {
        if (!isValid()) return 1.0f;

        float pitch;
        alGetSourcef(sourceId, AL_PITCH, &pitch);
        return pitch;
    }

    void AudioSource::setLooping(bool loop) {
        if (!isValid()) return;
        alSourcei(sourceId, AL_LOOPING, loop ? AL_TRUE : AL_FALSE);
        AudioSystem::checkError("setLooping");
    }

    bool AudioSource::isLooping() const {
        if (!isValid()) return false;

        ALint looping;
        alGetSourcei(sourceId, AL_LOOPING, &looping);
        return looping == AL_TRUE;
    }

    void AudioSource::setPosition(const glm::vec3& position) {
        if (!isValid()) return;
        alSource3f(sourceId, AL_POSITION, position.x, position.y, position.z);
        AudioSystem::checkError("setPosition");
    }

    glm::vec3 AudioSource::getPosition() const {
        if (!isValid()) return glm::vec3(0.0f);

        glm::vec3 position;
        alGetSource3f(sourceId, AL_POSITION, &position.x, &position.y, &position.z);
        return position;
    }

    void AudioSource::setVelocity(const glm::vec3& velocity) {
        if (!isValid()) return;
        alSource3f(sourceId, AL_VELOCITY, velocity.x, velocity.y, velocity.z);
        AudioSystem::checkError("setVelocity");
    }

    glm::vec3 AudioSource::getVelocity() const {
        if (!isValid()) return glm::vec3(0.0f);

        glm::vec3 velocity;
        alGetSource3f(sourceId, AL_VELOCITY, &velocity.x, &velocity.y, &velocity.z);
        return velocity;
    }

    void AudioSource::setDirection(const glm::vec3& direction) {
        if (!isValid()) return;
        alSource3f(sourceId, AL_DIRECTION, direction.x, direction.y, direction.z);
        AudioSystem::checkError("setDirection");
    }

    glm::vec3 AudioSource::getDirection() const {
        if (!isValid()) return glm::vec3(0.0f, 0.0f, -1.0f);

        glm::vec3 direction;
        alGetSource3f(sourceId, AL_DIRECTION, &direction.x, &direction.y, &direction.z);
        return direction;
    }

    void AudioSource::set3D(bool is3D) {
        if (!isValid()) return;

        spatialEnabled = is3D;
        alSourcei(sourceId, AL_SOURCE_RELATIVE, is3D ? AL_FALSE : AL_TRUE);
        AudioSystem::checkError("set3D");
    }

    bool AudioSource::is3D() const {
        return spatialEnabled;
    }

    void AudioSource::setMinDistance(float distance) {
        if (!isValid()) return;
        alSourcef(sourceId, AL_REFERENCE_DISTANCE, distance);
        AudioSystem::checkError("setMinDistance");
    }

    float AudioSource::getMinDistance() const {
        if (!isValid()) return 1.0f;

        float distance;
        alGetSourcef(sourceId, AL_REFERENCE_DISTANCE, &distance);
        return distance;
    }

    void AudioSource::setMaxDistance(float distance) {
        if (!isValid()) return;
        alSourcef(sourceId, AL_MAX_DISTANCE, distance);
        AudioSystem::checkError("setMaxDistance");
    }

    float AudioSource::getMaxDistance() const {
        if (!isValid()) return 100.0f;

        float distance;
        alGetSourcef(sourceId, AL_MAX_DISTANCE, &distance);
        return distance;
    }

    void AudioSource::setRolloffFactor(float factor) {
        if (!isValid()) return;
        alSourcef(sourceId, AL_ROLLOFF_FACTOR, factor);
        AudioSystem::checkError("setRolloffFactor");
    }

    float AudioSource::getRolloffFactor() const {
        if (!isValid()) return 1.0f;

        float factor;
        alGetSourcef(sourceId, AL_ROLLOFF_FACTOR, &factor);
        return factor;
    }

    void AudioSource::setConeAngles(float innerAngle, float outerAngle) {
        if (!isValid()) return;
        alSourcef(sourceId, AL_CONE_INNER_ANGLE, innerAngle);
        alSourcef(sourceId, AL_CONE_OUTER_ANGLE, outerAngle);
        AudioSystem::checkError("setConeAngles");
    }

    void AudioSource::setConeOuterGain(float gain) {
        if (!isValid()) return;
        alSourcef(sourceId, AL_CONE_OUTER_GAIN, gain);
        AudioSystem::checkError("setConeOuterGain");
    }

    void AudioSource::applyConfig(const AudioSourceConfig& config) {
        setVolume(config.volume);
        setPitch(config.pitch);
        setLooping(config.loop);
        set3D(config.is3D);
        setPosition(config.position);
        setVelocity(config.velocity);
        setMinDistance(config.minDistance);
        setMaxDistance(config.maxDistance);
        setRolloffFactor(config.rolloffFactor);
        setConeAngles(config.coneInnerAngle, config.coneOuterAngle);
        setConeOuterGain(config.coneOuterGain);
        setDirection(config.direction);
    }

    float AudioSource::getPlaybackPosition() const {
        if (!isValid()) return 0.0f;

        float seconds;
        alGetSourcef(sourceId, AL_SEC_OFFSET, &seconds);
        return seconds;
    }

    void AudioSource::setPlaybackPosition(float seconds) {
        if (!isValid()) return;
        alSourcef(sourceId, AL_SEC_OFFSET, seconds);
        AudioSystem::checkError("setPlaybackPosition");
    }

}
