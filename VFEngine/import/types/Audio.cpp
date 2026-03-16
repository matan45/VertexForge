#include "print/Log.hpp"
#include "Audio.hpp"
#include "VorbisEncoder.hpp"
#include "resource/EndianUtils.hpp"
#include "resource/VorbisDecoder.hpp"

#include <vector>
#include <fstream>
#include <filesystem>

#define DR_MP3_IMPLEMENTATION
#include <dr_mp3.h>
#define DR_WAV_IMPLEMENTATION
#include <dr_wav.h>


namespace types
{
    void Audio::loadFromFileWithType(const importConfig::ImportFiles& file, std::string_view fileName,
                                     std::string_view location, std::string_view fileType,
                                     AudioProgressCallback progressCallback) const
    {
        if (progressCallback) progressCallback(0.0f);

        DecodedAudio decoded;

        if (fileType == "OGG")
            decoded = decodeOgg(file.path);
        else if (fileType == "WAV")
            decoded = decodeWav(file.path);
        else if (fileType == "MP3")
            decoded = decodeMp3(file.path);
        else
        {
            vfLogError("Unsupported audio file type: {}", fileType);
            return;
        }

        if (decoded.data.empty())
            return;

        if (progressCallback) progressCallback(0.5f);

        resource::AudioData audioData;
        audioData.headerFileType = resource::FileType::AUDIO;
        audioData.sampleRate = decoded.sampleRate;
        audioData.channels = decoded.channels;
        audioData.frames = decoded.frames;
        audioData.totalDurationInSeconds = decoded.totalDurationInSeconds;

        const auto& audioConfig = file.config.audioConfig;

        // Determine compression format
        if (audioConfig.quality == importConfig::AudioCompressionQuality::Lossless)
        {
            audioData.compressionFormat = resource::AudioCompressionFormat::PCM;
            audioData.data = std::move(decoded.data);
        }
        else
        {
            // Encode to Vorbis
            float vorbisQuality = VorbisEncoder::qualityToFloat(static_cast<int>(audioConfig.quality));
            auto compressed = VorbisEncoder::encode(decoded.data.data(),
                                                     decoded.data.size(),
                                                     decoded.channels,
                                                     decoded.sampleRate,
                                                     vorbisQuality);
            if (compressed.empty())
            {
                vfLogWarning("Vorbis encoding failed, falling back to PCM");
                audioData.compressionFormat = resource::AudioCompressionFormat::PCM;
                audioData.data = std::move(decoded.data);
            }
            else
            {
                audioData.compressionFormat = resource::AudioCompressionFormat::Vorbis;
                audioData.compressedData = std::move(compressed);
                vfLogInfo("Vorbis compression: {} -> {} bytes ({:.1f}x)",
                          decoded.data.size() * sizeof(short),
                          audioData.compressedData.size(),
                          static_cast<float>(decoded.data.size() * sizeof(short)) /
                          static_cast<float>(audioData.compressedData.size()));
            }
        }

        if (progressCallback) progressCallback(0.7f);

        // Determine load type
        if (audioConfig.loadType == importConfig::AudioLoadType::Auto)
        {
            audioData.loadType = (decoded.totalDurationInSeconds < 10)
                ? resource::AudioLoadType::DecompressOnLoad
                : resource::AudioLoadType::Streaming;
        }
        else if (audioConfig.loadType == importConfig::AudioLoadType::DecompressOnLoad)
        {
            audioData.loadType = resource::AudioLoadType::DecompressOnLoad;
        }
        else
        {
            audioData.loadType = resource::AudioLoadType::Streaming;
        }

        saveToFile(location, fileName, audioData);

        if (progressCallback) progressCallback(1.0f);
    }

    DecodedAudio Audio::decodeOgg(std::string_view path) const
    {
        DecodedAudio result;

        // Read the file into memory, then use VorbisDecoder
        std::ifstream inFile(path.data(), std::ios::binary | std::ios::ate);
        if (!inFile)
        {
            vfLogError("Failed to open Ogg Vorbis file: {}", path);
            return result;
        }

        auto fileSize = inFile.tellg();
        inFile.seekg(0, std::ios::beg);

        std::vector<uint8_t> fileData(static_cast<size_t>(fileSize));
        inFile.read(reinterpret_cast<char*>(fileData.data()), fileSize);
        inFile.close();

        uint32_t channels = 0, sampleRate = 0;
        if (!resource::VorbisDecoder::decode(fileData.data(), fileData.size(),
                                              result.data, channels, sampleRate))
        {
            vfLogError("Failed to decode Ogg Vorbis file: {}", path);
            return result;
        }

        result.sampleRate = sampleRate;
        result.channels = channels;
        result.frames = static_cast<uint32_t>(result.data.size() / channels);
        result.totalDurationInSeconds = (sampleRate > 0)
            ? static_cast<uint32_t>(result.frames / sampleRate)
            : 0;

        return result;
    }

    DecodedAudio Audio::decodeWav(std::string_view path) const
    {
        DecodedAudio result;

        drwav wav;
        if (!drwav_init_file(&wav, path.data(), nullptr))
        {
            vfLogError("Failed to load WAV file: {}", path);
            return result;
        }

        result.sampleRate = wav.sampleRate;
        result.channels = wav.channels;
        result.frames = static_cast<uint32_t>(wav.totalPCMFrameCount);
        result.totalDurationInSeconds = static_cast<uint32_t>(wav.totalPCMFrameCount / wav.sampleRate);

        result.data.resize(wav.totalPCMFrameCount * wav.channels);
        drwav_read_pcm_frames_s16(&wav, wav.totalPCMFrameCount, result.data.data());

        drwav_uninit(&wav);
        return result;
    }

    DecodedAudio Audio::decodeMp3(std::string_view path) const
    {
        DecodedAudio result;

        drmp3 mp3;
        if (!drmp3_init_file(&mp3, path.data(), nullptr))
        {
            vfLogError("Failed to load MP3 file: {}", path);
            return result;
        }

        result.sampleRate = mp3.sampleRate;
        result.channels = mp3.channels;

        drmp3_uint64 totalFrames = drmp3_get_pcm_frame_count(&mp3);
        result.frames = static_cast<uint32_t>(totalFrames);
        result.totalDurationInSeconds = static_cast<uint32_t>(totalFrames / mp3.sampleRate);

        result.data.resize(totalFrames * mp3.channels);
        drmp3_read_pcm_frames_s16(&mp3, totalFrames, result.data.data());

        drmp3_uninit(&mp3);
        return result;
    }

    void Audio::saveToFile(std::string_view location, std::string_view fileName,
                           const resource::AudioData& audioData) const
    {
        std::filesystem::path newFileLocation = std::filesystem::path(location) / (std::string(fileName) + "." +
            FileExtension::audio);
        std::ofstream outFile(newFileLocation, std::ios::binary);
        if (!outFile)
        {
            vfLogError("Failed to open file for writing: {}", newFileLocation.string());
            return;
        }

        resource::endian::writeLE<uint8_t>(outFile, static_cast<uint8_t>(audioData.headerFileType));
        resource::endian::writeLE<uint32_t>(outFile, Version::major);
        resource::endian::writeLE<uint32_t>(outFile, Version::minor);
        resource::endian::writeLE<uint32_t>(outFile, Version::patch);

        // New fields: compression format and load type
        resource::endian::writeLE<uint8_t>(outFile, static_cast<uint8_t>(audioData.compressionFormat));
        resource::endian::writeLE<uint8_t>(outFile, static_cast<uint8_t>(audioData.loadType));

        resource::endian::writeLE<uint32_t>(outFile, audioData.sampleRate);
        resource::endian::writeLE<uint32_t>(outFile, audioData.channels);
        resource::endian::writeLE<uint32_t>(outFile, audioData.frames);
        resource::endian::writeLE<uint32_t>(outFile, audioData.totalDurationInSeconds);

        if (audioData.compressionFormat == resource::AudioCompressionFormat::Vorbis)
        {
            // Write compressed Vorbis data as raw bytes (opaque blob)
            auto dataSize = static_cast<uint32_t>(audioData.compressedData.size());
            resource::endian::writeLE<uint32_t>(outFile, dataSize);
            outFile.write(reinterpret_cast<const char*>(audioData.compressedData.data()), dataSize);
        }
        else
        {
            // Write PCM data with endian conversion
            auto dataSize = static_cast<uint32_t>(audioData.data.size() * sizeof(short));
            resource::endian::writeLE<uint32_t>(outFile, dataSize);
            resource::endian::writeVectorLE<short>(outFile, audioData.data);
        }

        outFile.close();
    }
}
