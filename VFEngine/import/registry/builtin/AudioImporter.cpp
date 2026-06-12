#include "AudioImporter.hpp"
#include <algorithm>

namespace import::builtin
{
    namespace
    {
        constexpr unsigned char oggSig[] = {0x4F, 0x67, 0x67, 0x53};

        bool matchesSignature(std::span<const unsigned char> header, std::span<const unsigned char> signature)
        {
            return header.size() >= signature.size() &&
                   std::equal(signature.begin(), signature.end(), header.begin());
        }

        bool isMP3(std::span<const unsigned char> header)
        {
            return header.size() >= 3 &&
                ((header[0] == 0x49 && header[1] == 0x44 && header[2] == 0x33) || // 'ID3'
                 (header[0] == 0xFF && (header[1] & 0xE0) == 0xE0)); // Frame sync bytes
        }

        bool isWAV(std::span<const unsigned char> header)
        {
            return header.size() >= 12 &&
                header[0] == 0x52 && header[1] == 0x49 && header[2] == 0x46 && header[3] == 0x46 && // 'RIFF'
                header[8] == 0x57 && header[9] == 0x41 && header[10] == 0x56 && header[11] == 0x45; // 'WAVE'
        }
    }

    std::vector<FormatInfo> AudioImporter::formats() const
    {
        return {
            {"OGG", "Audio Files", {"ogg"}, FileExtension::audio, resource::AssetType::Audio, 100},
            {"MP3", "Audio Files", {"mp3"}, FileExtension::audio, resource::AssetType::Audio, 70},
            {"WAV", "Audio Files", {"wav"}, FileExtension::audio, resource::AssetType::Audio, 60},
        };
    }

    bool AudioImporter::matches(const std::string& fileType, const DetectionInput& input) const
    {
        if (fileType == "OGG") return matchesSignature(input.header, oggSig);
        if (fileType == "MP3") return isMP3(input.header);
        if (fileType == "WAV") return isWAV(input.header);
        return false;
    }

    void AudioImporter::process(pipeline::ImportContext& context)
    {
        audioProcessor.loadFromFileWithType(context.file, context.fileName, context.location,
                                            context.fileType, wrapFileProgress<types::AudioProgressCallback>(context));
    }
}
