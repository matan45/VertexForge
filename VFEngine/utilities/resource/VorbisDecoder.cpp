#include "VorbisDecoder.hpp"
#include "../print/Log.hpp"

#define STB_VORBIS_IMPLEMENTATION
#include <stb_vorbis.c>

namespace resource
{
    bool VorbisDecoder::decode(const uint8_t* data, size_t dataSize,
                               std::vector<short>& pcmOut,
                               uint32_t& channels, uint32_t& sampleRate)
    {
        if (!data || dataSize == 0)
        {
            vfLogError("VorbisDecoder: null or empty input data");
            return false;
        }

        int channelsOut = 0;
        int sampleRateOut = 0;
        short* output = nullptr;

        int sampleCount = stb_vorbis_decode_memory(
            data, static_cast<int>(dataSize),
            &channelsOut, &sampleRateOut, &output);

        if (sampleCount <= 0 || !output)
        {
            vfLogError("VorbisDecoder: stb_vorbis_decode_memory failed");
            if (output) free(output);
            return false;
        }

        channels = static_cast<uint32_t>(channelsOut);
        sampleRate = static_cast<uint32_t>(sampleRateOut);

        // stb_vorbis returns interleaved samples, total = sampleCount * channels
        size_t totalSamples = static_cast<size_t>(sampleCount) * channels;
        pcmOut.assign(output, output + totalSamples);

        free(output);
        return true;
    }
}
