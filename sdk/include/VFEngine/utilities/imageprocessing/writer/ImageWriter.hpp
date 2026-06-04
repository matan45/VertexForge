#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace imageprocessing
{
    class ImageWriter
    {
    public:
        // Write uncompressed .vfImage (single mip, BGRA format)
        static bool write(const std::string& outputPath,
                         uint32_t width, uint32_t height,
                         const std::vector<uint8_t>& rgbaData);
    };
}
