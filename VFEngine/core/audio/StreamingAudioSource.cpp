#include "StreamingAudioSource.hpp"
#include "AudioSystem.hpp"
#include "print/Logger.hpp"
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

        // Open file for streaming
        streamHandle = resource::AudioResource::openStream(path);
        if (!streamHandle || !streamHandle->isOpen())
        {
            loggerError("Failed to open audio stream: {}", path);
            return false;
        }

        // Initialize OpenAL resources
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
            loggerError("StreamingAudioSource::initBuffers - stream handle invalid or not open");
            return false;
        }

        const auto& header = streamHandle->getHeader();

        loggerInfo("StreamingAudioSource::initBuffers - Header info:");
        loggerInfo("  sampleRate: {}", header.sampleRate);
        loggerInfo("  channels: {}", header.channels);
        loggerInfo("  frames: {}", header.frames);
        loggerInfo("  dataSize: {}", header.dataSize);
        loggerInfo("  bufferDurationSeconds: {}", config.bufferDurationSeconds);

        // Calculate samples per buffer based on duration
        // samples = sampleRate * channels * duration
        samplesPerBuffer = static_cast<size_t>(
            header.sampleRate * header.channels * config.bufferDurationSeconds
        );

        // Ensure even number for stereo alignment
        if (header.channels == 2 && samplesPerBuffer % 2 != 0)
        {
            samplesPerBuffer++;
        }

        loggerInfo("  samplesPerBuffer: {}", samplesPerBuffer);

        // Pre-allocate read buffer to avoid repeated allocations
        readBuffer.resize(samplesPerBuffer);

        loggerInfo("  readBuffer allocated, size: {}", readBuffer.size());

        // Create OpenAL source
        alGenSources(1, &sourceId);
        if (AudioSystem::checkError("alGenSources"))
        {
            return false;
        }

        loggerInfo("  OpenAL source created: {}", sourceId);

        // Create buffers
        bufferIds.resize(config.bufferCount);
        alGenBuffers(static_cast<ALsizei>(config.bufferCount), bufferIds.data());
        if (AudioSystem::checkError("alGenBuffers"))
        {
            alDeleteSources(1, &sourceId);
            sourceId = 0;
            bufferIds.clear();
            return false;
        }

        loggerInfo("  OpenAL buffers created: {} buffers", config.bufferCount);

        // Fill initial buffers with audio data
        int filledCount = 0;
        for (ALuint bufferId : bufferIds)
        {
            loggerInfo("  Filling buffer {} ...", bufferId);
            if (!fillBuffer(bufferId))
            {
                loggerInfo("  fillBuffer returned false for buffer {}, breaking", bufferId);
                // Not enough data to fill all buffers - that's OK for short files
                break;
            }
            loggerInfo("  Queueing buffer {} ...", bufferId);
            queueBuffer(bufferId);
            filledCount++;
        }

        loggerInfo("  Total buffers filled and queued: {}", filledCount);

        return true;
    }

    void StreamingAudioSource::cleanupBuffers()
    {
        if (sourceId != 0)
        {
            // Stop the source first
            alSourceStop(sourceId);

            // Unqueue all buffers
            ALint queuedCount = 0;
            alGetSourcei(sourceId, AL_BUFFERS_QUEUED, &queuedCount);
            if (queuedCount > 0)
            {
                std::vector<ALuint> unqueuedBuffers(queuedCount);
                alSourceUnqueueBuffers(sourceId, queuedCount, unqueuedBuffers.data());
            }

            // Delete source
            alDeleteSources(1, &sourceId);
            sourceId = 0;
        }

        // Delete buffers
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
        loggerInfo("    fillBuffer({}) - start", bufferId);

        if (!streamHandle)
        {
            loggerError("    fillBuffer - streamHandle is null");
            return false;
        }

        if (streamHandle->isEOF())
        {
            loggerInfo("    fillBuffer - stream is at EOF");
            return false;
        }

        loggerInfo("    fillBuffer - calling readSamples with samplesPerBuffer={}", samplesPerBuffer);
        loggerInfo("    fillBuffer - readBuffer.size() before read: {}", readBuffer.size());

        size_t samplesRead = streamHandle->readSamples(readBuffer, samplesPerBuffer);

        loggerInfo("    fillBuffer - samplesRead: {}", samplesRead);
        loggerInfo("    fillBuffer - readBuffer.size() after read: {}", readBuffer.size());

        if (samplesRead == 0)
        {
            loggerInfo("    fillBuffer - samplesRead is 0, returning false");
            return false;
        }

        // Track actual samples in this buffer for accurate position reporting
        bufferSampleCounts[bufferId] = samplesRead;

        const auto& header = streamHandle->getHeader();
        ALenum format = getFormat();

        loggerInfo("    fillBuffer - calling alBufferData:");
        loggerInfo("      bufferId: {}", bufferId);
        loggerInfo("      format: {} (MONO16={}, STEREO16={})", format, AL_FORMAT_MONO16, AL_FORMAT_STEREO16);
        loggerInfo("      data ptr: {}", (void*)readBuffer.data());
        loggerInfo("      size (bytes): {}", samplesRead * sizeof(short));
        loggerInfo("      sampleRate: {}", header.sampleRate);

        alBufferData(
            bufferId,
            format,
            readBuffer.data(),
            static_cast<ALsizei>(samplesRead * sizeof(short)),
            static_cast<ALsizei>(header.sampleRate)
        );

        if (AudioSystem::checkError("alBufferData"))
        {
            loggerError("    fillBuffer - alBufferData failed");
            return false;
        }

        loggerInfo("    fillBuffer({}) - success", bufferId);
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

        if (processedCount > 0)
        {
            loggerInfo("processFinishedBuffers - {} buffers processed", processedCount);
        }

        while (processedCount-- > 0)
        {
            // Unqueue the processed buffer
            ALuint bufferId;
            loggerInfo("  Unqueueing buffer...");
            alSourceUnqueueBuffers(sourceId, 1, &bufferId);

            if (AudioSystem::checkError("alSourceUnqueueBuffers"))
            {
                loggerError("  alSourceUnqueueBuffers failed");
                break;
            }

            loggerInfo("  Unqueued buffer {}", bufferId);

            // Update samples played count using actual samples in this buffer
            auto it = bufferSampleCounts.find(bufferId);
            size_t samplesInBuffer = (it != bufferSampleCounts.end()) ? it->second : samplesPerBuffer;
            totalSamplesPlayed += samplesInBuffer;

            // Try to refill the buffer
            if (!streamHandle->isEOF())
            {
                loggerInfo("  Refilling buffer {}...", bufferId);
                if (fillBuffer(bufferId))
                {
                    loggerInfo("  Re-queueing buffer {}...", bufferId);
                    queueBuffer(bufferId);
                }
                else
                {
                    loggerInfo("  fillBuffer returned false");
                }
            }
            else if (looping)
            {
                loggerInfo("  EOF reached, looping - resetting stream");
                // Reset stream and refill
                streamHandle->reset();
                totalSamplesPlayed = 0;
                bufferSampleCounts.clear();
                if (fillBuffer(bufferId))
                {
                    queueBuffer(bufferId);
                }
            }
            else
            {
                loggerInfo("  EOF reached, not looping");
            }
        }

        // Check if playback has stopped due to buffer underrun or end of stream
        ALint sourceState;
        alGetSourcei(sourceId, AL_SOURCE_STATE, &sourceState);

        if (sourceState == AL_STOPPED)
        {
            loggerInfo("processFinishedBuffers - source stopped");
            // Check if there are still buffers queued
            ALint queuedCount = 0;
            alGetSourcei(sourceId, AL_BUFFERS_QUEUED, &queuedCount);

            if (queuedCount > 0)
            {
                loggerInfo("  {} buffers still queued, restarting playback", queuedCount);
                // Buffer underrun - restart playback
                alSourcePlay(sourceId);
            }
            else
            {
                loggerInfo("  No buffers queued, playback finished");
                // No more buffers - playback finished
                state = StreamingState::Finished;
            }
        }
    }

    void StreamingAudioSource::play()
    {
        loggerInfo("StreamingAudioSource::play() - start, sourceId={}", sourceId);

        if (sourceId == 0)
        {
            loggerError("StreamingAudioSource::play() - sourceId is 0, returning");
            return;
        }

        loggerInfo("  current state: {}", static_cast<int>(state));

        if (state == StreamingState::Finished)
        {
            loggerInfo("  state is Finished, resetting for replay");
            // Reset for replay
            streamHandle->reset();
            totalSamplesPlayed = 0;

            // Refill all buffers
            for (ALuint bufferId : bufferIds)
            {
                if (!fillBuffer(bufferId)) break;
                queueBuffer(bufferId);
            }
        }

        // Check queued buffer count before playing
        ALint queuedCount = 0;
        alGetSourcei(sourceId, AL_BUFFERS_QUEUED, &queuedCount);
        loggerInfo("  buffers queued before play: {}", queuedCount);

        loggerInfo("  calling alSourcePlay({})", sourceId);
        alSourcePlay(sourceId);

        if (!AudioSystem::checkError("alSourcePlay"))
        {
            state = StreamingState::Playing;
            loggerInfo("  alSourcePlay succeeded, state set to Playing");
        }
        else
        {
            loggerError("  alSourcePlay failed");
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

        // Unqueue all buffers
        ALint queuedCount = 0;
        alGetSourcei(sourceId, AL_BUFFERS_QUEUED, &queuedCount);
        if (queuedCount > 0)
        {
            std::vector<ALuint> unqueuedBuffers(queuedCount);
            alSourceUnqueueBuffers(sourceId, queuedCount, unqueuedBuffers.data());
        }

        // Reset stream position
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

        // Get OpenAL's byte offset within current buffer
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

        // Stop current playback
        if (sourceId != 0)
        {
            alSourceStop(sourceId);

            // Unqueue all buffers
            ALint queuedCount = 0;
            alGetSourcei(sourceId, AL_BUFFERS_QUEUED, &queuedCount);
            if (queuedCount > 0)
            {
                std::vector<ALuint> unqueuedBuffers(queuedCount);
                alSourceUnqueueBuffers(sourceId, queuedCount, unqueuedBuffers.data());
            }
        }

        // Seek in stream
        if (!streamHandle->seekToTime(seconds))
        {
            return false;
        }

        // Update position tracking
        const auto& header = streamHandle->getHeader();
        totalSamplesPlayed = static_cast<size_t>(seconds * header.sampleRate * header.channels);
        bufferSampleCounts.clear();

        // Refill buffers
        for (ALuint bufferId : bufferIds)
        {
            if (!fillBuffer(bufferId)) break;
            queueBuffer(bufferId);
        }

        // Resume playback if was playing
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
        loggerInfo("StreamingAudioSource::getDuration() called");
        if (!streamHandle)
        {
            loggerInfo("  streamHandle is null, returning 0");
            return 0.0f;
        }
        float dur = streamHandle->getDuration();
        loggerInfo("  returning duration: {}", dur);
        return dur;
    }
}
