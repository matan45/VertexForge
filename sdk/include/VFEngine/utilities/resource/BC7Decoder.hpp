#pragma once

#include <vector>
#include <cstdint>

namespace resource
{
    // Lightweight BC7 block decoder for CPU-side texture preview.
    // Supports Mode 6 (most common from ISPC ultrafast/fast profiles).
    // Other modes decode as approximate mid-gray.
    class BC7Decoder
    {
    public:
        BC7Decoder() = delete;

        // Decompress BC7 data to RGBA8.
        // Input: BC7 compressed blocks (16 bytes per 4x4 block)
        // Output: RGBA8 pixel data (width * height * 4 bytes)
        static std::vector<unsigned char> decompress(
            const unsigned char* compressedData,
            uint32_t width, uint32_t height);
    };
}
