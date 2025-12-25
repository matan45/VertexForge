#pragma once
#include <AL/al.h>
#include <string>
#include <unordered_map>
#include <optional>
#include <cstdint>

namespace core::audio
{
    struct AudioBufferInfo
    {
        ALuint bufferId = 0;
        uint32_t channels = 0;
        uint32_t sampleRate = 0;
        uint32_t frames = 0;
        float durationSeconds = 0.0f;
    };

    class AudioBufferManager
    {
    private:
        std::unordered_map<std::string, AudioBufferInfo> pathToBuffer;
        std::unordered_map<ALuint, std::string> bufferToPath;

    public:
        explicit AudioBufferManager() = default;
        ~AudioBufferManager();

        AudioBufferManager(const AudioBufferManager&) = delete;
        AudioBufferManager& operator=(const AudioBufferManager&) = delete;

        ALuint loadBuffer(const std::string& path);
        void unloadBuffer(const std::string& path);
        void unloadBuffer(ALuint bufferId);
        void unloadAll();

        bool isLoaded(const std::string& path) const;
        std::optional<ALuint> getBuffer(const std::string& path) const;
        std::optional<AudioBufferInfo> getBufferInfo(const std::string& path) const;

        size_t getLoadedCount() const { return pathToBuffer.size(); }

    private:
        ALuint createBufferFromData(const short* data, size_t dataSize,
                                    uint32_t channels, uint32_t sampleRate);
    };
}
