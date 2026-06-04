#pragma once
#include <cstdint>
#include <vector>

namespace imageprocessing
{
    enum class DetectionMethod : uint8_t
    {
        Color,
        Luminance,
        EdgeAware
    };

    struct RemovalParams
    {
        DetectionMethod method = DetectionMethod::Color;
        float threshold = 0.3f;          // 0.0-1.0 sensitivity
        float feather = 0.02f;           // Edge softness
        float bgColor[3] = {1.0f, 1.0f, 1.0f}; // Background color (RGB 0-1)
        bool autoDetectColor = true;     // Auto-detect from image edges
        bool invertSelection = false;    // Invert alpha mask
    };

    struct RemovalResult
    {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> rgbaData;   // RGBA8 with computed alpha
        float detectedBgColor[3] = {0.0f, 0.0f, 0.0f};

        bool valid() const { return !rgbaData.empty(); }
    };
}
