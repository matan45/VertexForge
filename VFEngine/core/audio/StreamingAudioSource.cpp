#include "StreamingAudioSource.hpp"
#include "AudioSystem.hpp"
#include "print/Log.hpp"
#include <algorithm>

namespace core::audio
{
    StreamingAudioSource::StreamingAudioSource() = default;

    StreamingAudioSource::~StreamingAudioSource()
    {
        close();
    }

    StreamingAudioSource::StreamingAudioSource(StreamingAudioSource&& other) noexcept
        : streamHandle(std::move(other.streamHandle))
          , config(other.config)
          , sourceId(other.sourceId)
          , bufferIds(std::move(other.bufferIds))
          , readBuffer(std::move(other.readBuffer))
          , state(other.state)
          , looping(other.looping)
          , spatialEnabled(other.spatialEnabled)
          , samplesPerBuffer(other.samplesPerBuffer)
          , totalSamplesPlayed(other.totalSamplesPlayed)
    {
        other.sourceId = 0;
        other.state = StreamingState::Stopped;
    }

    StreamingAudioSource& StreamingAudioSource::operator=(StreamingAudioSource&& other) noexcept
    {
        if (this != &other)
        {
            close();
            streamHandle = std::move(other.streamHandle);
            config = other.config;
            sourceId = other.sourceId;
            bufferIds = std::move(other.bufferIds);
            readBuffer = std::move(other.readBuffer);
            state = other.state;
            looping = other.looping;
            spatialEnabled = other.spatialEnabled;
            samplesPerBuffer = other.samplesPerBuffer;
            totalSamplesPlayed = other.totalSamplesPlayed;
            other.sourceId = 0;
            other.state = StreamingState::Stopped;
        }
        return *this;
    }

    bool StreamingAudioSource::open(const std::string& path, const StreamingConfig& streamConfig)
    {
        close();

        config = streamConfig;

        streamHandle = resource::AudioResource::openStream(path);
        if (!streamHandle || !streamHandle->isOpen())
        {
            vfLogError("Failed to open audio stream: {}", path);
            return false;
        }

        if (!initBuffers())
        {
            streamHandle.reset();
            return false;
        }

        state = StreamingState::Stopped;
        totalSamplesPlayed = 0;

        return true;
    }

    void StreamingAudioSource::close()
    {
        stop();
        cleanupBuffers();
        streamHandle.reset();
        state = StreamingState::Stopped;
        totalSamplesPlayed = 0;
    }

    bool StreamingAudioSource::initBuffers()
    {
        if (!streamHandle || !streamHandle->isOpen())
        {
            return false;
        }

        const auto& header = streamHandle->getHeader();

        samplesPerBuffer = static_cast<size_t>(
            static_cast<float>(header.sampleRate) * static_cast<float>(header.channels) * config.bufferDurationSeconds
        );

        if (header.channels == 2 && samplesPerBuffer % 2 != 0)
        {
            samplesPerBuffer++;
        }

        readBuffer.resize(samplesPerBuffer);

        alGenSources(1, &sourceId);
        if (AudioSystem::checkError("alGenSources"))
        {
            return false;
        }

        bufferIds.resize(config.bufferCount);
        alGenBuffers(static_cast<ALsizei>(config.bufferCount), bufferIds.data());
        if (AudioSystem::checkError("alGenBuffers"))
        {
            alDeleteSources(1, &sourceId);
            sourceId = 0;
            bufferIds.clear();
            return false;
        }

        for (ALuint bufferId : bufferIds)
        {
            if (!fillBuffer(bufferId))
            {
                break;
            }
            queueBuffer(bufferId);
        }

        return true;
    }

    void StreamingAudioSource::cleanupBuffers()
    {
        if (sourceId != 0)
        {
            alSourceStop(sourceId);

            ALint queuedCount = 0;
            alGetSourcei(sourceId, AL_BUFFERS_QUEUED, &queuedCount);
            if (queuedCount > 0)
            {
                std::vector<ALuint> unqueuedBuffers(queuedCount);
                alSourceUnqueueBuffers(sourceId, queuedCount, unqueuedBuffers.data());
            }

            alDeleteSources(1, &sourceId);
            sourceId = 0;
        }

        if (!bufferIds.empty())
        {
            alDeleteBuffers(static_cast<ALsizei>(bufferIds.size()), bufferIds.data());
            bufferIds.clear();
        }

        readBuffer.clear();
        bufferSampleCounts.clear();
        samplesPerBuffer = 0;
    }

    ALenum StreamingAudioSource::getFormat() const
    {
        if (!streamHandle) return AL_FORMAT_MONO16;

        const auto& header = streamHandle->getHeader();
        return (header.channels == 2) ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16;
    }

    bool StreamingAudioSource::fillBuffer(ALuint bufferId)
    {
        if (!streamHandle || streamHandle->isEOF())
        {
            return false;
        }

        size_t samplesRead = streamHandle->readSamples(readBuffer, samplesPerBuffer);

        if (samplesRead == 0)
        {
            return false;
        }

        bufferSampleCounts[bufferId] = samplesRead;

        const auto& header = streamHandle->getHeader();
        ALenum format = getFormat();

        alBufferData(
            bufferId,
            format,
            readBuffer.data(),
            static_cast<ALsizei>(samplesRead * sizeof(short)),
            static_cast<ALsizei>(header.sampleRate)
        );

        if (AudioSystem::checkError("alBufferData"))
        {
            return false;
        }

        return true;
    }

    bool StreamingAudioSource::queueBuffer(ALuint bufferId)
    {
        alSourceQueueBuffers(sourceId, 1, &bufferId);
        return !AudioSystem::checkError("alSourceQueueBuffers");
    }

    void StreamingAudioSource::processFinishedBuffers()
    {
        if (sourceId == 0 || state != StreamingState::Playing)
        {
            return;
        }

        // Check how many buffers have been processed
        ALint processedCount = 0;
        alGetSourcei(sourceId, AL_BUFFERS_PROCESSED, &processedCount);

        while (processedCount-- > 0)
        {
            // Unqueue the processed buffer
            ALuint bufferId;
            alSourceUnqueueBuffers(sourceId, 1, &bufferId);

            if (AudioSystem::checkError("alSourceUnqueueBuffers"))
            {
                break;
            }

            auto it = bufferSampleCounts.find(bufferId);
            size_t samplesInBuffer = (it != bufferSampleCounts.end()) ? it->second : samplesPerBuffer;
            totalSamplesPlayed += samplesInBuffer;

            if (!streamHandle->isEOF())
            {
                if (fillBuffer(bufferId))
                {
                    queueBuffer(bufferId);
                }
            }
            else if (looping)
            {
                // Reset stream and refill
                streamHandle->reset();
                totalSamplesPlayed = 0;
                bufferSampleCounts.clear();
                if (fillBuffer(bufferId))
                {
                    queueBuffer(bufferId);
                }
            }
        }

        ALint sourceState;
        alGetSourcei(sourceId, AL_SOURCE_STATE, &sourceState);

        if (sourceState == AL_STOPPED)
        {
            ALint queuedCount = 0;
            alGetSourcei(sourceId, AL_BUFFERS_QUEUED, &queuedCount);

            if (queuedCount > 0)
            {
                alSourcePlay(sourceId);
            }
            else
            {
                state = StreamingState::Finished;
            }
        }
    }

    void StreamingAudioSource::play()
    {
        if (sourceId == 0)
        {
            return;
        }

        if (state == StreamingState::Finished)
        {
            streamHandle->reset();
            totalSamplesPlayed = 0;

            for (ALuint bufferId : bufferIds)
            {
                if (!fillBuffer(bufferId)) break;
                queueBuffer(bufferId);
            }
        }

        alSourcePlay(sourceId);

        if (!AudioSystem::checkError("alSourcePlay"))
        {
            state = StreamingState::Playing;
        }
    }

    void StreamingAudioSource::pause()
    {
        if (sourceId == 0 || state != StreamingState::Playing) return;

        alSourcePause(sourceId);
        if (!AudioSystem::checkError("alSourcePause"))
        {
            state = StreamingState::Paused;
        }
    }

    void StreamingAudioSource::stop()
    {
        if (sourceId == 0) return;

        alSourceStop(sourceId);

        ALint queuedCount = 0;
        alGetSourcei(sourceId, AL_BUFFERS_QUEUED, &queuedCount);
        if (queuedCount > 0)
        {
            std::vector<ALuint> unqueuedBuffers(queuedCount);
            alSourceUnqueueBuffers(sourceId, queuedCount, unqueuedBuffers.data());
        }

        if (streamHandle)
        {
            streamHandle->reset();
        }
        totalSamplesPlayed = 0;
        bufferSampleCounts.clear();

        state = StreamingState::Stopped;
    }

    void StreamingAudioSource::update()
    {
        if (state == StreamingState::Playing)
        {
            processFinishedBuffers();
        }
    }

    void StreamingAudioSource::setVolume(float volume)
    {
        if (sourceId == 0) return;
        volume = std::clamp(volume, 0.0f, 1.0f);
        alSourcef(sourceId, AL_GAIN, volume);
    }

    float StreamingAudioSource::getVolume() const
    {
        if (sourceId == 0) return 1.0f;
        float volume;
        alGetSourcef(sourceId, AL_GAIN, &volume);
        return volume;
    }

    void StreamingAudioSource::setPitch(float pitch)
    {
        if (sourceId == 0) return;
        pitch = std::clamp(pitch, 0.5f, 2.0f);
        alSourcef(sourceId, AL_PITCH, pitch);
    }

    float StreamingAudioSource::getPitch() const
    {
        if (sourceId == 0) return 1.0f;
        float pitch;
        alGetSourcef(sourceId, AL_PITCH, &pitch);
        return pitch;
    }

    void StreamingAudioSource::setLooping(bool loop)
    {
        looping = loop;
    }

    void StreamingAudioSource::applyConfig(const AudioSourceConfig& sourceConfig)
    {
        setVolume(sourceConfig.volume);
        setPitch(sourceConfig.pitch);
        setLooping(sourceConfig.loop);
    }

    float StreamingAudioSource::getPlaybackPosition() const
    {
        if (!streamHandle) return 0.0f;

        const auto& header = streamHandle->getHeader();
        if (header.sampleRate == 0 || header.channels == 0) return 0.0f;

        ALint byteOffset = 0;
        if (sourceId != 0)
        {
            alGetSourcei(sourceId, AL_BYTE_OFFSET, &byteOffset);
        }

        size_t currentSamples = totalSamplesPlayed + (byteOffset / sizeof(short));
        return static_cast<float>(currentSamples) /
            static_cast<float>(header.sampleRate * header.channels);
    }

    bool StreamingAudioSource::setPlaybackPosition(float seconds)
    {
        if (!streamHandle) return false;

        // Clamp to valid range (must match what seekToTime does internally)
        seconds = std::max(0.0f, std::min(seconds, streamHandle->getDuration()));

        bool wasPlaying = (state == StreamingState::Playing);

        if (sourceId != 0)
        {
            alSourceStop(sourceId);

            ALint queuedCount = 0;
            alGetSourcei(sourceId, AL_BUFFERS_QUEUED, &queuedCount);
            if (queuedCount > 0)
            {
                std::vector<ALuint> unqueuedBuffers(queuedCount);
                alSourceUnqueueBuffers(sourceId, queuedCount, unqueuedBuffers.data());
            }
        }

        if (!streamHandle->seekToTime(seconds))
        {
            return false;
        }

        const auto& header = streamHandle->getHeader();
        totalSamplesPlayed = static_cast<size_t>(seconds * static_cast<float>(header.sampleRate) * static_cast<float>(header.channels));
        bufferSampleCounts.clear();

        for (ALuint bufferId : bufferIds)
        {
            if (!fillBuffer(bufferId)) break;
            queueBuffer(bufferId);
        }

        if (wasPlaying)
        {
            play();
        }
        else
        {
            state = StreamingState::Stopped;
        }

        return true;
    }

    float StreamingAudioSource::getDuration() const
    {
        if (!streamHandle)
        {
            return 0.0f;
        }
        return streamHandle->getDuration();
    }
}
