#pragma once

#include <string>
#include <cstdint>
#include <functional>
#include "../ImportExport.hpp"

namespace types
{
    using SVTProgressCallback = std::function<void(float progress)>;

    struct ImageData
    {
        const uint8_t* data = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    struct ORMPackInput
    {
        std::string aoPath;
        std::string roughnessPath;
        std::string metallicPath;
        float defaultAO = 1.0f;
        float defaultRoughness = 0.5f;
        float defaultMetallic = 0.0f;
    };

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
        static bool convertToSVT(const std::string& inputPath,
                                  const std::string& outputPath,
                                  const Config& config = {},
                                  SVTProgressCallback progressCallback = nullptr);

        // Convert an already-loaded RGBA8 texture to .vfSVT format.
        static bool convertRGBA8ToSVT(const ImageData& image,
                                       const std::string& outputPath,
                                       const Config& config = {},
                                       SVTProgressCallback progressCallback = nullptr);

        // Pack separate AO, Roughness, Metallic images into a single ORM .vfSVT.
        static bool packORMToSVT(const ORMPackInput& input,
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
