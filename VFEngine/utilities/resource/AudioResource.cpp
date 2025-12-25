#include "AudioResource.hpp"
#include "../print/EditorLogger.hpp"
#include "EndianUtils.hpp"

#include <fstream>
#include <algorithm>

namespace resource
{
    // ============================================
    // AudioStreamHandle Implementation
    // ============================================

    AudioStreamHandle::~AudioStreamHandle()
    {
        if (file.is_open())
        {
            file.close();
        }
    }

    AudioStreamHandle::AudioStreamHandle(AudioStreamHandle&& other) noexcept
        : file(std::move(other.file))
          , currentSamplePosition(other.currentSamplePosition)
    {
        other.currentSamplePosition = 0;
    }

    AudioStreamHandle& AudioStreamHandle::operator=(AudioStreamHandle&& other) noexcept
    {
        if (this != &other)
        {
            if (file.is_open())
            {
                file.close();
            }
            file = std::move(other.file);
            header = other.header;
            currentSamplePosition = other.currentSamplePosition;
            other.header = {};
            other.currentSamplePosition = 0;
        }
        return *this;
    }

    size_t AudioStreamHandle::readSamples(std::vector<short>& buffer, size_t sampleCount)
    {
        if (!file.is_open() || sampleCount == 0)
        {
            return 0;
        }

        // Calculate how many samples are left
        size_t totalSamples = getTotalSamples();
        size_t remainingSamples = (currentSamplePosition < totalSamples)
                                      ? totalSamples - currentSamplePosition
                                      : 0;
        size_t samplesToRead = std::min(sampleCount, remainingSamples);

        if (samplesToRead == 0)
        {
            return 0;
        }

        // Only grow buffer if needed (avoid shrinking to prevent repeated allocations)
        if (buffer.size() < samplesToRead)
        {
            buffer.resize(samplesToRead);
        }

        // Read samples using endian-safe method
        endian::readVectorLE<short>(file, buffer, samplesToRead);

        // Check for read errors
        if (file.fail() && !file.eof())
        {
            vfLogError("Error reading audio stream data");
            return 0;
        }

        // Update position
        currentSamplePosition += samplesToRead;

        return samplesToRead;
    }

    bool AudioStreamHandle::seekToSample(size_t sampleIndex)
    {
        if (!file.is_open())
        {
            return false;
        }

        size_t totalSamples = getTotalSamples();
        if (sampleIndex > totalSamples)
        {
            sampleIndex = totalSamples;
        }

        // Calculate byte offset from data start
        std::streamoff byteOffset = static_cast<std::streamoff>(sampleIndex * sizeof(short));
        std::streampos targetPos = header.dataStartOffset + byteOffset;

        file.clear(); // Clear any error flags
        file.seekg(targetPos);

        if (file.fail())
        {
            vfLogError("Failed to seek in audio stream");
            return false;
        }

        currentSamplePosition = sampleIndex;
        return true;
    }

    bool AudioStreamHandle::seekToTime(float seconds)
    {
        if (!file.is_open() || header.sampleRate == 0 || header.channels == 0)
        {
            return false;
        }

        // Clamp to valid range
        seconds = std::max(0.0f, std::min(seconds, getDuration()));

        // Calculate sample position (samples = seconds * sampleRate * channels)
        size_t sampleIndex = static_cast<size_t>(seconds * header.sampleRate * header.channels);

        return seekToSample(sampleIndex);
    }

    void AudioStreamHandle::reset()
    {
        seekToSample(0);
    }

    bool AudioStreamHandle::isEOF() const
    {
        if (!file.is_open())
        {
            return true;
        }
        return currentSamplePosition >= getTotalSamples();
    }

    // ============================================
    // AudioResource::openStream Implementation
    // ============================================

    std::unique_ptr<AudioStreamHandle> AudioResource::openStream(std::string_view path)
    {
        if (path.empty())
        {
            vfLogError("Empty path provided for audio streaming");
            return nullptr;
        }

        auto handle = std::make_unique<AudioStreamHandle>();

        // Open the file in binary mode
        handle->file.open(path.data(), std::ios::binary);
        if (!handle->file)
        {
            vfLogError("Failed to open audio file for streaming: {}", path);
            return nullptr;
        }

        // Read header file type (endian-safe)
        uint8_t headerFileType = endian::readLE<uint8_t>(handle->file);
        if (static_cast<FileType>(headerFileType) != FileType::AUDIO)
        {
            vfLogError("Invalid audio file type for streaming: {}", path);
            return nullptr;
        }

        // Read version information (endian-safe)
        uint32_t majorVersion = endian::readLE<uint32_t>(handle->file);
        uint32_t minorVersion = endian::readLE<uint32_t>(handle->file);
        uint32_t patchVersion = endian::readLE<uint32_t>(handle->file);

        // Validate version compatibility
        if (majorVersion != Version::major || minorVersion != Version::minor || patchVersion != Version::patch)
        {
            vfLogError("Incompatible file version for streaming: {}.{}.{}", majorVersion, minorVersion, patchVersion);
            return nullptr;
        }

        // Read audio metadata (endian-safe)
        handle->header.sampleRate = endian::readLE<uint32_t>(handle->file);
        handle->header.channels = endian::readLE<uint32_t>(handle->file);
        handle->header.frames = endian::readLE<uint32_t>(handle->file);
        handle->header.totalDurationSeconds = endian::readLE<uint32_t>(handle->file);
        handle->header.dataSize = endian::readLE<uint32_t>(handle->file);

        // Validate data
        if (handle->header.dataSize == 0)
        {
            vfLogError("Audio file has zero data size for streaming: {}", path);
            return nullptr;
        }

        if (handle->header.sampleRate == 0 || handle->header.channels == 0)
        {
            vfLogError("Invalid audio parameters for streaming: {}", path);
            return nullptr;
        }

        // Record the position where PCM data starts
        handle->header.dataStartOffset = handle->file.tellg();
        handle->currentSamplePosition = 0;

        vfLogInfo("Opened audio stream: {} ({}Hz, {} channels, {} seconds)",
                  path, handle->header.sampleRate, handle->header.channels,
                  handle->header.totalDurationSeconds);

        return handle;
    }

    // ============================================
    // AudioResource::loadAudio Implementation
    // ============================================

    AudioData AudioResource::loadAudio(std::string_view path)
    {
        resource::AudioData audioData;

        // Validate input path
        if (path.empty())
        {
            vfLogError("Empty path provided for audio loading");
            return {};
        }

        // Open the file in binary mode
        std::ifstream inFile(path.data(), std::ios::binary);
        if (!inFile)
        {
            vfLogError("Failed to open audio file for reading: {}", path);
            return {};
        }

        // Check file size to prevent loading extremely large files
        inFile.seekg(0, std::ios::end);
        auto filePos = inFile.tellg();
        inFile.seekg(0, std::ios::beg);

        if (filePos == std::ifstream::pos_type(-1))
        {
            vfLogError("Failed to determine file size for: {}", path);
            return {};
        }

        std::streamsize fileSize = static_cast<std::streamsize>(filePos);
        if (fileSize > 500 * 1024 * 1024)
        {
            // 500MB limit
            vfLogError("Audio file {} is too large: {} bytes", path, static_cast<long long>(fileSize));
            return {};
        }

        // Read header file type (endian-safe)
        uint8_t headerFileType = endian::readLE<uint8_t>(inFile);
        audioData.headerFileType = static_cast<resource::FileType>(headerFileType);

        // Read version information (endian-safe)
        uint32_t majorVersion = endian::readLE<uint32_t>(inFile);
        uint32_t minorVersion = endian::readLE<uint32_t>(inFile);
        uint32_t patchVersion = endian::readLE<uint32_t>(inFile);

        // Validate version compatibility
        if (majorVersion != Version::major || minorVersion != Version::minor || patchVersion != Version::patch)
        {
            vfLogError("Incompatible file version: {}.{}.{}", majorVersion, minorVersion, patchVersion);
            return {};
        }

        // Read audio metadata (endian-safe)
        audioData.sampleRate = endian::readLE<uint32_t>(inFile);
        audioData.channels = endian::readLE<uint32_t>(inFile);
        audioData.frames = endian::readLE<uint32_t>(inFile);
        audioData.totalDurationInSeconds = endian::readLE<uint32_t>(inFile);

        uint32_t dataSize = endian::readLE<uint32_t>(inFile);

        // Validate data size
        if (dataSize == 0)
        {
            vfLogError("Audio file has zero data size: {}", path);
            return {};
        }

        // Validate that dataSize is reasonable given sample rate and channels
        if (audioData.sampleRate > 0 && audioData.channels > 0)
        {
            size_t expectedMaxSize = audioData.sampleRate * audioData.channels * sizeof(short) * 3600; // 1 hour max
            if (dataSize > expectedMaxSize)
            {
                vfLogError("Audio data size {} seems unreasonable for given parameters", dataSize);
                return {};
            }
        }

        // Read audio data (endian-safe)
        size_t totalSamples = dataSize / sizeof(short);
        endian::readVectorLE<short>(inFile, audioData.data, totalSamples);

        return audioData;
    }
}
