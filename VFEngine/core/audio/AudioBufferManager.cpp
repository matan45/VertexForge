#include "AudioBufferManager.hpp"
#include "AudioSystem.hpp"
#include "resource/ResourceManager.hpp"
#include "asset/AssetRef.hpp"
#include "print/Log.hpp"
#include "resource/AudioAnalysis.hpp"

namespace core::audio
{
    AudioBufferManager::~AudioBufferManager()
    {
        pathToBuffer.clear();
        bufferToPath.clear();
        bufferEnvelopes.clear();
    }

    ALuint AudioBufferManager::loadBuffer(const std::string& path)
    {
        auto existingBuffer = getBuffer(path);
        if (existingBuffer.has_value())
        {
            return existingBuffer.value();
        }

        auto audioFuture = resource::ResourceManager::loadAudioAsync(asset::AssetRef::fromPath(std::string(path)));
        auto audioData = audioFuture.get();

        if (!audioData || audioData->data.empty())
        {
            vfLogError("Failed to load audio data from: {}", path);
            return 0;
        }

        ALuint bufferId = createBufferFromData(
            audioData->data.data(),
            audioData->data.size() * sizeof(short),
            audioData->channels,
            audioData->sampleRate
        );

        if (bufferId == 0)
        {
            vfLogError("Failed to create OpenAL buffer for: {}", path);
            return 0;
        }

        std::vector<uint8_t> envelope = resource::buildRmsEnvelope(
            audioData->data, audioData->channels, audioData->sampleRate);

        AudioBufferInfo info;
        info.bufferId = bufferId;
        info.channels = audioData->channels;
        info.sampleRate = audioData->sampleRate;
        info.frames = audioData->frames;
        info.durationSeconds = static_cast<float>(audioData->totalDurationInSeconds);

        pathToBuffer[path] = info;
        bufferToPath[bufferId] = path;
        bufferEnvelopes[bufferId] = std::move(envelope);

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
        bufferEnvelopes.erase(bufferId);
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
        bufferEnvelopes.clear();
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

    float AudioBufferManager::sampleEnvelope(ALuint bufferId, float seconds) const
    {
        const auto it = bufferEnvelopes.find(bufferId);
        return it != bufferEnvelopes.end()
            ? resource::sampleEnvelope(it->second, seconds)
            : 0.0f;
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
            vfLogError("Unsupported audio channel count: {}", channels);
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
