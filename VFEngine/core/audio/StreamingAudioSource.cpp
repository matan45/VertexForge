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
          , queuedChunks(std::move(other.queuedChunks))
          , nextDecodedSample(other.nextDecodedSample)
    {
        other.sourceId = 0;
        other.state = StreamingState::Stopped;
        other.nextDecodedSample = 0;
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
            queuedChunks = std::move(other.queuedChunks);
            nextDecodedSample = other.nextDecodedSample;
            other.sourceId = 0;
            other.state = StreamingState::Stopped;
            other.nextDecodedSample = 0;
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

        return true;
    }

    void StreamingAudioSource::close()
    {
        stop();
        cleanupBuffers();
        streamHandle.reset();
        state = StreamingState::Stopped;
        resetQueuedState();
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

        resetQueuedState();
        for (ALuint bufferId : bufferIds)
        {
            if (!fillAndQueueBuffer(bufferId))
            {
                break;
            }
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
        resetQueuedState();
        samplesPerBuffer = 0;
    }

    ALenum StreamingAudioSource::getFormat() const
    {
        if (!streamHandle) return AL_FORMAT_MONO16;

        const auto& header = streamHandle->getHeader();
        return (header.channels == 2) ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16;
    }

    std::optional<QueuedEnvelopeChunk> StreamingAudioSource::fillBuffer(ALuint bufferId)
    {
        if (!streamHandle || streamHandle->isEOF())
        {
            return std::nullopt;
        }

        size_t samplesRead = streamHandle->readSamples(readBuffer, samplesPerBuffer);

        if (samplesRead == 0)
        {
            return std::nullopt;
        }

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
            nextDecodedSample += samplesRead;
            return std::nullopt;
        }

        QueuedEnvelopeChunk chunk;
        chunk.bufferId = bufferId;
        chunk.startSample = nextDecodedSample;
        chunk.sampleCount = samplesRead;
        chunk.rms = resource::buildRmsEnvelope(
            std::span<const short>(readBuffer.data(), samplesRead),
            header.channels, header.sampleRate);
        nextDecodedSample += samplesRead;
        return chunk;
    }

    bool StreamingAudioSource::queueBuffer(QueuedEnvelopeChunk chunk)
    {
        const ALuint bufferId = static_cast<ALuint>(chunk.bufferId);
        alSourceQueueBuffers(sourceId, 1, &bufferId);
        if (AudioSystem::checkError("alSourceQueueBuffers"))
            return false;
        queuedChunks.push_back(std::move(chunk));
        return true;
    }

    bool StreamingAudioSource::fillAndQueueBuffer(ALuint bufferId)
    {
        auto chunk = fillBuffer(bufferId);
        return chunk.has_value() && queueBuffer(std::move(*chunk));
    }

    void StreamingAudioSource::resetQueuedState(size_t nextSample)
    {
        queuedChunks.clear();
        nextDecodedSample = nextSample;
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

            const auto chunkIt = std::find_if(queuedChunks.begin(), queuedChunks.end(),
                [bufferId](const QueuedEnvelopeChunk& chunk)
                {
                    return chunk.bufferId == static_cast<uint32_t>(bufferId);
                });
            if (chunkIt != queuedChunks.end())
                queuedChunks.erase(chunkIt);
            else
                vfLogWarning("StreamingAudioSource: unqueued buffer {} has no meter metadata", bufferId);

            if (!streamHandle->isEOF())
            {
                fillAndQueueBuffer(bufferId);
            }
            else if (looping)
            {
                // Queue loop-head data behind any tail chunks that OpenAL has not played.
                streamHandle->reset();
                nextDecodedSample = 0;
                fillAndQueueBuffer(bufferId);
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
            resetQueuedState();

            for (ALuint bufferId : bufferIds)
            {
                if (!fillAndQueueBuffer(bufferId)) break;
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
        if (sourceId == 0)
        {
            resetQueuedState();
            state = StreamingState::Stopped;
            return;
        }

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
        resetQueuedState();

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
        return getPlaybackMetrics().positionSeconds;
    }

    StreamingPlaybackMetrics StreamingAudioSource::getPlaybackMetrics() const
    {
        if (!streamHandle) return {};

        const auto& header = streamHandle->getHeader();
        if (header.sampleRate == 0 || header.channels == 0) return {};

        ALint byteOffset = 0;
        if (sourceId != 0)
        {
            alGetSourcei(sourceId, AL_BYTE_OFFSET, &byteOffset);
        }

        const std::size_t queueSampleOffset = byteOffset > 0
            ? static_cast<std::size_t>(byteOffset) / sizeof(short)
            : 0;
        return sampleQueuedPlayback(queuedChunks, queueSampleOffset,
                                    header.channels, header.sampleRate);
    }

    // Refill the AL queue from `sample` and put the source back into `prevState`. Shared by
    // both exits of setPlaybackPosition, because the failure path needs it just as much as
    // the success path: a stream left with an empty queue reports position 0, and a Playing
    // one with nothing to mix is walked to Finished by processFinishedBuffers and reaped a
    // tick later.
    //
    // Safe to call play() here: `state` is still prevState, so its `if (state == Finished)`
    // branch cannot fire and re-reset what we have just queued.
    void StreamingAudioSource::refillAndRestore(size_t sample, StreamingState prevState)
    {
        resetQueuedState(sample);

        for (ALuint bufferId : bufferIds)
        {
            if (!fillAndQueueBuffer(bufferId)) break;
        }

        if (prevState == StreamingState::Playing)
        {
            play();
        }
        else if (prevState == StreamingState::Paused)
        {
            // Buffers are queued at the new offset; stay paused so resume plays from there
            state = StreamingState::Paused;
        }
        else
        {
            state = StreamingState::Stopped;
        }
    }

    bool StreamingAudioSource::setPlaybackPosition(float seconds)
    {
        if (!streamHandle) return false;

        // FIRST, before anything is torn down: alSourceStop zeroes AL_BYTE_OFFSET and
        // resetQueuedState empties queuedChunks, and the reported position is derived from
        // both — so read it once, here, or a failed seek has nothing to roll back to.
        //
        // The PLAYHEAD, deliberately, not nextDecodedSample: that is the decoder's WRITE
        // cursor, a whole queue ahead of what is being heard, so restoring it would answer a
        // failed scrub by silently jumping forward.
        const float restoreSeconds = getPlaybackPosition();

        // Clamp to valid range (must match what seekToTime does internally)
        seconds = std::max(0.0f, std::min(seconds, streamHandle->getDuration()));

        const StreamingState prevState = state;

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

        const auto& header = streamHandle->getHeader();
        const auto sampleAt = [&header](float t) {
            return static_cast<size_t>(t * static_cast<float>(header.sampleRate)
                                       * static_cast<float>(header.channels));
        };

        if (!streamHandle->seekToTime(seconds))
        {
            // A failed seek must leave the stream where it was, not destroy it. Bailing out
            // here with an empty queue is what made a failed scrub snap the preview playhead
            // to 0 — and it also stranded a Playing source with nothing to mix, so the
            // stream itself died a tick later.
            if (streamHandle->seekToTime(restoreSeconds))
            {
                refillAndRestore(sampleAt(restoreSeconds), prevState);
            }
            else
            {
                // Even the rollback failed. The top of the file is the one offset the
                // decoder is guaranteed to reach, and a stream playing from 0 beats a
                // stream that no longer exists.
                streamHandle->reset();
                refillAndRestore(0, prevState);
            }
            return false;
        }

        refillAndRestore(sampleAt(seconds), prevState);
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
