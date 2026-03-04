#include "Audio.hpp"
#include "resource/EndianUtils.hpp"

#include <vector>
#include <fstream>
#include <filesystem>

#define DR_MP3_IMPLEMENTATION
#include <dr_mp3.h>
#define DR_WAV_IMPLEMENTATION
#include <dr_wav.h>
#include <stb_vorbis.c>


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

        if (progressCallback) progressCallback(0.7f);

        resource::AudioData audioData;
        audioData.headerFileType = resource::FileType::AUDIO;
        audioData.sampleRate = decoded.sampleRate;
        audioData.channels = decoded.channels;
        audioData.frames = decoded.frames;
        audioData.totalDurationInSeconds = decoded.totalDurationInSeconds;
        audioData.data = std::move(decoded.data);

        saveToFile(location, fileName, audioData);

        if (progressCallback) progressCallback(1.0f);
    }

    DecodedAudio Audio::decodeOgg(std::string_view path) const
    {
        DecodedAudio result;

        int error;
        stb_vorbis* vorbis = stb_vorbis_open_filename(path.data(), &error, nullptr);
        if (!vorbis)
        {
            vfLogError("Failed to load Ogg Vorbis file: {}", path);
            return result;
        }

        stb_vorbis_info info = stb_vorbis_get_info(vorbis);
        result.sampleRate = info.sample_rate;
        result.channels = info.channels;

        int frames = stb_vorbis_stream_length_in_samples(vorbis);
        int totalSamples = frames * info.channels;
        result.frames = frames;
        result.totalDurationInSeconds = static_cast<uint32_t>(stb_vorbis_stream_length_in_seconds(vorbis));

        result.data.resize(totalSamples);
        stb_vorbis_get_samples_short_interleaved(vorbis, info.channels, result.data.data(), totalSamples);

        stb_vorbis_close(vorbis);
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

        resource::endian::writeLE<uint32_t>(outFile, audioData.sampleRate);
        resource::endian::writeLE<uint32_t>(outFile, audioData.channels);
        resource::endian::writeLE<uint32_t>(outFile, audioData.frames);
        resource::endian::writeLE<uint32_t>(outFile, audioData.totalDurationInSeconds);

        auto dataSize = static_cast<uint32_t>(audioData.data.size() * sizeof(short));
        resource::endian::writeLE<uint32_t>(outFile, dataSize);
        resource::endian::writeVectorLE<short>(outFile, audioData.data);

        outFile.close();
    }
}
