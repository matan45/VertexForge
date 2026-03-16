#include "AudioResource.hpp"
#include "VorbisDecoder.hpp"
#include "../print/Log.hpp"
#include "EndianUtils.hpp"

#include <fstream>
#include <algorithm>

// Include stb_vorbis header-only for streaming API (implementation in VorbisDecoder.cpp)
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

namespace resource
{
    // ============================================
    // AudioStreamHandle Implementation
    // ============================================

    AudioStreamHandle::~AudioStreamHandle()
    {
        if (vorbisHandle)
        {
            stb_vorbis_close(vorbisHandle);
            vorbisHandle = nullptr;
        }
        if (file.is_open())
        {
            file.close();
        }
    }

    AudioStreamHandle::AudioStreamHandle(AudioStreamHandle&& other) noexcept
        : file(std::move(other.file))
        , header(other.header)
        , currentSamplePosition(other.currentSamplePosition)
        , vorbisBuffer(std::move(other.vorbisBuffer))
        , vorbisHandle(other.vorbisHandle)
    {
        other.vorbisHandle = nullptr;
        other.currentSamplePosition = 0;
    }

    AudioStreamHandle& AudioStreamHandle::operator=(AudioStreamHandle&& other) noexcept
    {
        if (this != &other)
        {
            if (vorbisHandle)
            {
                stb_vorbis_close(vorbisHandle);
            }
            if (file.is_open())
            {
                file.close();
            }
            file = std::move(other.file);
            header = other.header;
            currentSamplePosition = other.currentSamplePosition;
            vorbisBuffer = std::move(other.vorbisBuffer);
            vorbisHandle = other.vorbisHandle;
            other.vorbisHandle = nullptr;
            other.header = {};
            other.currentSamplePosition = 0;
        }
        return *this;
    }

    bool AudioStreamHandle::isOpen() const
    {
        if (header.compressionFormat == AudioCompressionFormat::Vorbis)
            return vorbisHandle != nullptr;
        return file.is_open();
    }

    size_t AudioStreamHandle::readSamples(std::vector<short>& buffer, size_t sampleCount)
    {
        if (!isOpen() || sampleCount == 0)
            return 0;

        size_t totalSamples = getTotalSamples();
        size_t remainingSamples = (currentSamplePosition < totalSamples)
                                      ? totalSamples - currentSamplePosition
                                      : 0;
        size_t samplesToRead = std::min(sampleCount, remainingSamples);

        if (samplesToRead == 0)
            return 0;

        if (buffer.size() < samplesToRead)
            buffer.resize(samplesToRead);

        if (header.compressionFormat == AudioCompressionFormat::Vorbis)
        {
            // Vorbis streaming: decode on-the-fly
            int samplesRead = stb_vorbis_get_samples_short_interleaved(
                vorbisHandle, static_cast<int>(header.channels),
                buffer.data(), static_cast<int>(samplesToRead));

            size_t actualSamples = static_cast<size_t>(samplesRead) * header.channels;
            currentSamplePosition += actualSamples;
            return actualSamples;
        }
        else
        {
            // PCM: read directly with endian conversion
            endian::readVectorLE<short>(file, buffer, samplesToRead);

            if (file.fail() && !file.eof())
            {
                vfLogError("Error reading audio stream data");
                return 0;
            }

            currentSamplePosition += samplesToRead;
            return samplesToRead;
        }
    }

    bool AudioStreamHandle::seekToSample(size_t sampleIndex)
    {
        sampleIndex = std::min(sampleIndex, getTotalSamples());

        if (header.compressionFormat == AudioCompressionFormat::Vorbis)
        {
            if (!vorbisHandle)
                return false;

            // stb_vorbis_seek takes sample offset (frames, not interleaved samples)
            unsigned int frameIndex = static_cast<unsigned int>(sampleIndex / header.channels);
            int result = stb_vorbis_seek(vorbisHandle, frameIndex);
            if (!result)
            {
                vfLogError("Failed to seek in Vorbis stream");
                return false;
            }
            currentSamplePosition = static_cast<size_t>(frameIndex) * header.channels;
            return true;
        }
        else
        {
            if (!file.is_open())
                return false;

            std::streamoff byteOffset = static_cast<std::streamoff>(sampleIndex * sizeof(short));
            std::streampos targetPos = header.dataStartOffset + byteOffset;

            file.clear();
            file.seekg(targetPos);

            if (file.fail())
            {
                vfLogError("Failed to seek in audio stream");
                return false;
            }

            currentSamplePosition = sampleIndex;
            return true;
        }
    }

    bool AudioStreamHandle::seekToTime(float seconds)
    {
        if (!isOpen() || header.sampleRate == 0 || header.channels == 0)
            return false;

        seconds = std::max(0.0f, std::min(seconds, getDuration()));
        size_t sampleIndex = static_cast<size_t>(seconds * header.sampleRate * header.channels);
        return seekToSample(sampleIndex);
    }

    void AudioStreamHandle::reset()
    {
        seekToSample(0);
    }

    bool AudioStreamHandle::isEOF() const
    {
        if (!isOpen())
            return true;
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

        handle->file.open(path.data(), std::ios::binary);
        if (!handle->file)
        {
            vfLogError("Failed to open audio file for streaming: {}", path);
            return nullptr;
        }

        // Read header
        uint8_t headerFileType = endian::readLE<uint8_t>(handle->file);
        if (static_cast<FileType>(headerFileType) != FileType::AUDIO)
        {
            vfLogError("Invalid audio file type for streaming: {}", path);
            return nullptr;
        }

        uint32_t majorVersion = endian::readLE<uint32_t>(handle->file);
        uint32_t minorVersion = endian::readLE<uint32_t>(handle->file);
        uint32_t patchVersion = endian::readLE<uint32_t>(handle->file);

        if (majorVersion != Version::major || minorVersion != Version::minor || patchVersion != Version::patch)
        {
            vfLogError("Incompatible file version for streaming: {}.{}.{}", majorVersion, minorVersion, patchVersion);
            return nullptr;
        }

        // Read new compression fields
        handle->header.compressionFormat = static_cast<AudioCompressionFormat>(endian::readLE<uint8_t>(handle->file));
        handle->header.loadType = static_cast<AudioLoadType>(endian::readLE<uint8_t>(handle->file));

        handle->header.sampleRate = endian::readLE<uint32_t>(handle->file);
        handle->header.channels = endian::readLE<uint32_t>(handle->file);
        handle->header.frames = endian::readLE<uint32_t>(handle->file);
        handle->header.totalDurationSeconds = endian::readLE<uint32_t>(handle->file);
        handle->header.dataSize = endian::readLE<uint32_t>(handle->file);

        if (handle->header.dataSize == 0 || handle->header.sampleRate == 0 || handle->header.channels == 0)
        {
            vfLogError("Invalid audio parameters for streaming: {}", path);
            return nullptr;
        }

        if (handle->header.compressionFormat == AudioCompressionFormat::Vorbis)
        {
            // Read entire compressed blob into memory, open stb_vorbis handle
            handle->vorbisBuffer.resize(handle->header.dataSize);
            handle->file.read(reinterpret_cast<char*>(handle->vorbisBuffer.data()), handle->header.dataSize);
            handle->file.close();

            int error = 0;
            handle->vorbisHandle = stb_vorbis_open_memory(
                handle->vorbisBuffer.data(),
                static_cast<int>(handle->vorbisBuffer.size()),
                &error, nullptr);

            if (!handle->vorbisHandle)
            {
                vfLogError("Failed to open Vorbis stream from memory (error {}): {}", error, path);
                return nullptr;
            }
        }
        else
        {
            // PCM: record data start offset for seeking
            handle->header.dataStartOffset = handle->file.tellg();
        }

        handle->currentSamplePosition = 0;
        return handle;
    }

    // ============================================
    // AudioResource::loadAudio Implementation
    // ============================================

    AudioData AudioResource::loadAudio(std::string_view path)
    {
        resource::AudioData audioData;

        if (path.empty())
        {
            vfLogError("Empty path provided for audio loading");
            return {};
        }

        std::ifstream inFile(path.data(), std::ios::binary);
        if (!inFile)
        {
            vfLogError("Failed to open audio file for reading: {}", path);
            return {};
        }

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
            vfLogError("Audio file {} is too large: {} bytes", path, static_cast<long long>(fileSize));
            return {};
        }

        // Read header
        uint8_t headerFileType = endian::readLE<uint8_t>(inFile);
        audioData.headerFileType = static_cast<resource::FileType>(headerFileType);

        uint32_t majorVersion = endian::readLE<uint32_t>(inFile);
        uint32_t minorVersion = endian::readLE<uint32_t>(inFile);
        uint32_t patchVersion = endian::readLE<uint32_t>(inFile);

        if (majorVersion != Version::major || minorVersion != Version::minor || patchVersion != Version::patch)
        {
            vfLogError("Incompatible file version: {}.{}.{}", majorVersion, minorVersion, patchVersion);
            return {};
        }

        // Read compression fields
        audioData.compressionFormat = static_cast<AudioCompressionFormat>(endian::readLE<uint8_t>(inFile));
        audioData.loadType = static_cast<AudioLoadType>(endian::readLE<uint8_t>(inFile));

        audioData.sampleRate = endian::readLE<uint32_t>(inFile);
        audioData.channels = endian::readLE<uint32_t>(inFile);
        audioData.frames = endian::readLE<uint32_t>(inFile);
        audioData.totalDurationInSeconds = endian::readLE<uint32_t>(inFile);

        uint32_t dataSize = endian::readLE<uint32_t>(inFile);

        if (dataSize == 0)
        {
            vfLogError("Audio file has zero data size: {}", path);
            return {};
        }

        if (audioData.compressionFormat == AudioCompressionFormat::Vorbis)
        {
            // Read compressed Vorbis data, then decode to PCM
            std::vector<uint8_t> compressedData(dataSize);
            inFile.read(reinterpret_cast<char*>(compressedData.data()), dataSize);

            uint32_t decodedChannels = 0, decodedSampleRate = 0;
            if (!VorbisDecoder::decode(compressedData.data(), compressedData.size(),
                                       audioData.data, decodedChannels, decodedSampleRate))
            {
                vfLogError("Failed to decode Vorbis audio: {}", path);
                return {};
            }
            // Decoded PCM is now in audioData.data — callers see PCM as before
        }
        else
        {
            // PCM path (unchanged)
            if (audioData.sampleRate > 0 && audioData.channels > 0)
            {
                size_t expectedMaxSize = audioData.sampleRate * audioData.channels * sizeof(short) * 3600;
                if (dataSize > expectedMaxSize)
                {
                    vfLogError("Audio data size {} seems unreasonable for given parameters", dataSize);
                    return {};
                }
            }

            size_t totalSamples = dataSize / sizeof(short);
            endian::readVectorLE<short>(inFile, audioData.data, totalSamples);
        }

        return audioData;
    }
}
