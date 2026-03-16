#pragma once
#include <vector>
#include <cstdint>

namespace resource
{
    class VorbisDecoder
    {
    public:
        // Decode Ogg/Vorbis data to interleaved PCM shorts.
        // Returns true on success. Populates pcmOut, channels, and sampleRate.
        static bool decode(const uint8_t* data, size_t dataSize,
                           std::vector<short>& pcmOut,
                           uint32_t& channels, uint32_t& sampleRate);
    };
}
