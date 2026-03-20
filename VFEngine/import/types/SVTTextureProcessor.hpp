#pragma once

#include <string>
#include <cstdint>
#include <functional>
#include "../ImportExport.hpp"

namespace types
{
    using SVTProgressCallback = std::function<void(float progress)>;

    // Converts a large texture into a tiled .vfSVT file for Sparse Virtual Texturing.
    // Only textures above the size threshold are tiled; smaller ones should use
    // the standard .vfImage path.
    class VF_IMPORT_API SVTTextureProcessor
    {
    public:
        struct Config
        {
            uint32_t tileSizeLog2 = 7;       // 128x128 tiles
            uint32_t borderSize = 4;         // Border padding for filtering
            uint32_t minSizeForSVT = 4096;   // Only tile textures >= this size
            bool srgb = true;                // sRGB compression for albedo
        };

        // Convert a source image file to .vfSVT format.
        // Input: path to source image (PNG, JPG, TGA, HDR, EXR)
        // Output: .vfSVT file at outputPath
        // Returns true on success.
        static bool convertToSVT(const std::string& inputPath,
                                  const std::string& outputPath,
                                  const Config& config = {},
                                  SVTProgressCallback progressCallback = nullptr);

        // Convert an already-loaded RGBA8 texture to .vfSVT format.
        static bool convertRGBA8ToSVT(const uint8_t* rgbaData,
                                       uint32_t width, uint32_t height,
                                       const std::string& outputPath,
                                       const Config& config = {},
                                       SVTProgressCallback progressCallback = nullptr);

        // Check if a texture is large enough to benefit from SVT
        static bool shouldUseSVT(uint32_t width, uint32_t height,
                                  uint32_t minSize = 4096);

        // Compute the virtual texture size log2 for a given image size
        static uint32_t computeVirtualSizeLog2(uint32_t width, uint32_t height,
                                                 uint32_t tileSizeLog2 = 7);
    };
}
