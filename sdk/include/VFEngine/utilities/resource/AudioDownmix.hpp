#pragma once
#include <cstdint>
#include <span>
#include <vector>

// Pure, CPU-only PCM downmix helper. Header-only inline so both the import
// pipeline (import/types/Audio.cpp) and the unit tests (tests/) can use it
// without linking OpenAL or the audio DLL. Kept next to VfAudioHeader.hpp /
// AudioResource.hpp (the audio-IO/PCM home).

namespace resource
{
    // Averages interleaved int16 PCM channels into a single mono channel.
    //
    //   interleaved : frame-interleaved samples, size == frames * channels
    //   channels    : source channel count
    //
    // Returns one sample per frame. channels == 1 is a passthrough copy;
    // channels == 0 returns empty (no divide-by-zero). A trailing partial frame
    // (size not a multiple of channels) is dropped. Averaging in int32 keeps the
    // result within int16 range; the clamp is defensive.
    inline std::vector<short> downmixToMono(std::span<const short> interleaved, uint32_t channels)
    {
        if (channels == 0)
        {
            return {};
        }
        if (channels == 1)
        {
            return std::vector<short>(interleaved.begin(), interleaved.end());
        }

        const std::size_t frames = interleaved.size() / channels;
        std::vector<short> mono;
        mono.reserve(frames);

        for (std::size_t frame = 0; frame < frames; ++frame)
        {
            const std::size_t base = frame * channels;
            std::int32_t sum = 0;
            for (uint32_t c = 0; c < channels; ++c)
            {
                sum += interleaved[base + c];
            }

            std::int32_t avg = sum / static_cast<std::int32_t>(channels);
            if (avg < -32768) avg = -32768;
            if (avg > 32767) avg = 32767;
            mono.push_back(static_cast<short>(avg));
        }

        return mono;
    }
}
