#pragma once
#pragma warning(disable: 4251)

#include "../ImageProcessingExport.hpp"
#include "BackgroundRemoverTypes.hpp"
#include <cstdint>
#include <string>

namespace imageprocessing
{
    class VF_IMAGEPROCESSING_API BackgroundRemover
    {
    public:
        // Process RGBA8 input -> RGBA8 output with computed alpha mask
        static RemovalResult process(const uint8_t* rgbaData, uint32_t width, uint32_t height,
                                     const RemovalParams& params);

        // Write result as uncompressed .vfImage
        static bool saveAsVFImage(const RemovalResult& result, const std::string& outputPath);

    private:
        static void autoDetectBackground(const uint8_t* rgbaData, uint32_t width, uint32_t height,
                                          float outColor[3]);
        static void processColor(const uint8_t* input, uint8_t* output,
                                 uint32_t width, uint32_t height, const RemovalParams& params);
        static void processLuminance(const uint8_t* input, uint8_t* output,
                                     uint32_t width, uint32_t height, const RemovalParams& params);
        static void processEdgeAware(const uint8_t* input, uint8_t* output,
                                     uint32_t width, uint32_t height, const RemovalParams& params);
    };
}
