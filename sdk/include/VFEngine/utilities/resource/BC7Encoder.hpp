#pragma once

#include <vector>
#include <cstdint>

namespace resource
{
    // Simple BC7 Mode 6 encoder for offline baking.
    // Mode 6: 1 subset, 7-bit RGBA endpoints, 1 p-bit each, 4-bit indices.
    // Not as fast as ISPC but doesn't require external dependencies.
    class BC7Encoder
    {
    public:
        BC7Encoder() = delete;

        // Compress RGBA8 data to BC7 (Mode 6 only).
        // Width/height must be multiples of 4.
        // Output: 16 bytes per 4x4 block.
        static std::vector<uint8_t> compress(
            const uint8_t* rgbaData,
            uint32_t width, uint32_t height);
    };
}
