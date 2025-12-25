#include "AudioBufferManager.hpp"
#include "AudioSystem.hpp"
#include "resource/AudioResource.hpp"
#include "print/Logger.hpp"

namespace core::audio
{
    AudioBufferManager::~AudioBufferManager()
    {
        pathToBuffer.clear();
        bufferToPath.clear();
    }

    ALuint AudioBufferManager::loadBuffer(const std::string& path)
    {
        auto existingBuffer = getBuffer(path);
        if (existingBuffer.has_value())
        {
            return existingBuffer.value();
        }

        resource::AudioData audioData = resource::AudioResource::loadAudio(path);

        if (audioData.data.empty())
        {
            loggerError("Failed to load audio data from: {}", path);
            return 0;
        }

        ALuint bufferId = createBufferFromData(
            audioData.data.data(),
            audioData.data.size() * sizeof(short),
            audioData.channels,
            audioData.sampleRate
        );

        if (bufferId == 0)
        {
            loggerError("Failed to create OpenAL buffer for: {}", path);
            return 0;
        }

        AudioBufferInfo info;
        info.bufferId = bufferId;
        info.channels = audioData.channels;
        info.sampleRate = audioData.sampleRate;
        info.frames = audioData.frames;
        info.durationSeconds = static_cast<float>(audioData.totalDurationInSeconds);

        pathToBuffer[path] = info;
        bufferToPath[bufferId] = path;

        return bufferId;
    }

    void AudioBufferManager::unloadBuffer(const std::string& path)
    {
        auto it = pathToBuffer.find(path);
        if (it == pathToBuffer.end())
        {
            return;
        }

        ALuint bufferId = it->second.bufferId;
        alDeleteBuffers(1, &bufferId);
        AudioSystem::checkError("unloadBuffer");

        bufferToPath.erase(bufferId);
        pathToBuffer.erase(it);
    }

    void AudioBufferManager::unloadBuffer(ALuint bufferId)
    {
        auto it = bufferToPath.find(bufferId);
        if (it == bufferToPath.end())
        {
            return;
        }

        std::string path = it->second;
        unloadBuffer(path);
    }

    void AudioBufferManager::unloadAll()
    {
        for (auto& [path, info] : pathToBuffer)
        {
            alDeleteBuffers(1, &info.bufferId);
        }
        AudioSystem::checkError("unloadAll");

        pathToBuffer.clear();
        bufferToPath.clear();
    }

    bool AudioBufferManager::isLoaded(const std::string& path) const
    {
        return pathToBuffer.find(path) != pathToBuffer.end();
    }

    std::optional<ALuint> AudioBufferManager::getBuffer(const std::string& path) const
    {
        auto it = pathToBuffer.find(path);
        if (it != pathToBuffer.end())
        {
            return it->second.bufferId;
        }
        return std::nullopt;
    }

    std::optional<AudioBufferInfo> AudioBufferManager::getBufferInfo(const std::string& path) const
    {
        auto it = pathToBuffer.find(path);
        if (it != pathToBuffer.end())
        {
            return it->second;
        }
        return std::nullopt;
    }

    ALuint AudioBufferManager::createBufferFromData(const short* data, size_t dataSize,
                                                    uint32_t channels, uint32_t sampleRate)
    {
        ALuint bufferId;
        alGenBuffers(1, &bufferId);

        if (AudioSystem::checkError("alGenBuffers"))
        {
            return 0;
        }

        ALenum format;
        if (channels == 1)
        {
            format = AL_FORMAT_MONO16;
        }
        else if (channels == 2)
        {
            format = AL_FORMAT_STEREO16;
        }
        else
        {
            loggerError("Unsupported audio channel count: {}", channels);
            alDeleteBuffers(1, &bufferId);
            return 0;
        }

        alBufferData(bufferId, format, data, static_cast<ALsizei>(dataSize),
                     static_cast<ALsizei>(sampleRate));

        if (AudioSystem::checkError("alBufferData"))
        {
            alDeleteBuffers(1, &bufferId);
            return 0;
        }

        return bufferId;
    }
}
