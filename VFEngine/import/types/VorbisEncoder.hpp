#pragma once
#include "../ImportExport.hpp"
#include <vector>
#include <cstdint>

namespace types
{
    class VF_IMPORT_API VorbisEncoder
    {
    public:
        // Encode PCM samples to Ogg/Vorbis compressed data.
        // quality: 0.0-1.0 (maps to vorbis VBR quality)
        // Returns complete Ogg/Vorbis bytestream decodable by stb_vorbis.
        static std::vector<uint8_t> encode(const short* pcm, size_t sampleCount,
                                           uint32_t channels, uint32_t sampleRate,
                                           float quality);

        // Map AudioCompressionQuality enum to vorbis quality float.
        // Low=0.1 (~80kbps), Medium=0.4 (~128kbps), High=0.6 (~192kbps)
        static float qualityToFloat(int qualityEnum);
    };
}
