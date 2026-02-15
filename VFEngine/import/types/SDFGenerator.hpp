#pragma once
#include <vector>
#include <cstdint>

namespace types
{
    struct SDFResult
    {
        std::vector<unsigned char> pixels;
        int width = 0;
        int height = 0;
    };

    class SDFGenerator
    {
    public:
        // Generate SDF from a high-resolution monochrome bitmap.
        // srcBitmap: source grayscale bitmap (values > threshold = inside glyph)
        // srcWidth, srcHeight: source bitmap dimensions (typically 4x the target)
        // targetWidth, targetHeight: desired SDF output dimensions
        // spread: SDF spread in pixels (in target resolution)
        // onEdgeValue: output value at the glyph edge (typically 128)
        // threshold: grayscale value that separates inside/outside (typically 128)
        static SDFResult generateFromBitmap(
            const unsigned char* srcBitmap,
            int srcWidth, int srcHeight,
            int targetWidth, int targetHeight,
            float spread,
            uint8_t onEdgeValue = 128,
            uint8_t threshold = 128);

    private:
        // Compute unsigned distance field using 8SSEDT
        // (Eight-point Signed Sequential Euclidean Distance Transform)
        static void computeDistanceField(
            const std::vector<bool>& binaryImage,
            int width, int height,
            std::vector<float>& distanceField);
    };
}
