#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace procedural
{
    class VFImageWriter
    {
    public:
        // Write uncompressed .vfImage (single mip, BGRA format)
        // Input rgbaData must be RGBA8 format
        static bool write(const std::string& outputPath,
                         uint32_t width, uint32_t height,
                         const std::vector<uint8_t>& rgbaData);
    };
}
