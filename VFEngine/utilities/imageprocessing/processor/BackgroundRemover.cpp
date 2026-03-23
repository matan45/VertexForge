#include "BackgroundRemover.hpp"
#include "../writer/ImageWriter.hpp"
#include <cmath>
#include <algorithm>

namespace imageprocessing
{
    namespace
    {
        float colorDistance(float r1, float g1, float b1, float r2, float g2, float b2)
        {
            float dr = r1 - r2;
            float dg = g1 - g2;
            float db = b1 - b2;
            return std::sqrt(dr * dr + dg * dg + db * db) / std::sqrt(3.0f);
        }

        float luminance(float r, float g, float b)
        {
            return 0.299f * r + 0.587f * g + 0.114f * b;
        }

        uint8_t computeAlpha(float dist, float threshold, float feather, bool invert)
        {
            float alpha;
            if (dist < threshold)
                alpha = 0.0f;
            else if (feather > 0.001f && dist < threshold + feather)
                alpha = (dist - threshold) / feather;
            else
                alpha = 1.0f;

            if (invert)
                alpha = 1.0f - alpha;

            return static_cast<uint8_t>(std::clamp(alpha * 255.0f, 0.0f, 255.0f));
        }

        float sobelMagnitude(const uint8_t* rgba, uint32_t width, uint32_t height,
                             uint32_t x, uint32_t y)
        {
            auto lum = [&](uint32_t px, uint32_t py) -> float {
                px = std::clamp(px, 0u, width - 1);
                py = std::clamp(py, 0u, height - 1);
                size_t idx = (static_cast<size_t>(py) * width + px) * 4;
                return luminance(rgba[idx] / 255.0f, rgba[idx + 1] / 255.0f, rgba[idx + 2] / 255.0f);
            };

            float gx = -lum(x - 1, y - 1) + lum(x + 1, y - 1)
                        - 2.0f * lum(x - 1, y) + 2.0f * lum(x + 1, y)
                        - lum(x - 1, y + 1) + lum(x + 1, y + 1);

            float gy = -lum(x - 1, y - 1) - 2.0f * lum(x, y - 1) - lum(x + 1, y - 1)
                        + lum(x - 1, y + 1) + 2.0f * lum(x, y + 1) + lum(x + 1, y + 1);

            return std::sqrt(gx * gx + gy * gy);
        }
    }

    void BackgroundRemover::autoDetectBackground(const uint8_t* rgbaData, uint32_t width, uint32_t height,
                                                  float outColor[3])
    {
        double sumR = 0, sumG = 0, sumB = 0;
        uint32_t count = 0;
        constexpr uint32_t border = 8;

        auto sample = [&](uint32_t x, uint32_t y) {
            size_t idx = (static_cast<size_t>(y) * width + x) * 4;
            sumR += rgbaData[idx + 0] / 255.0;
            sumG += rgbaData[idx + 1] / 255.0;
            sumB += rgbaData[idx + 2] / 255.0;
            count++;
        };

        uint32_t bx = std::min(border, width);
        uint32_t by = std::min(border, height);

        for (uint32_t x = 0; x < width; x += 2)
        {
            for (uint32_t y = 0; y < by; ++y) sample(x, y);
            for (uint32_t y = height - by; y < height; ++y) sample(x, y);
        }
        for (uint32_t y = by; y < height - by; y += 2)
        {
            for (uint32_t x = 0; x < bx; ++x) sample(x, y);
            for (uint32_t x = width - bx; x < width; ++x) sample(x, y);
        }

        if (count > 0)
        {
            outColor[0] = static_cast<float>(sumR / count);
            outColor[1] = static_cast<float>(sumG / count);
            outColor[2] = static_cast<float>(sumB / count);
        }
        else
        {
            outColor[0] = outColor[1] = outColor[2] = 1.0f;
        }
    }

    void BackgroundRemover::processColor(const uint8_t* input, uint8_t* output,
                                          uint32_t width, uint32_t height, const RemovalParams& params)
    {
        float bgR = params.bgColor[0], bgG = params.bgColor[1], bgB = params.bgColor[2];

        for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i)
        {
            size_t idx = i * 4;
            float r = input[idx + 0] / 255.0f;
            float g = input[idx + 1] / 255.0f;
            float b = input[idx + 2] / 255.0f;

            float dist = colorDistance(r, g, b, bgR, bgG, bgB);

            output[idx + 0] = input[idx + 0];
            output[idx + 1] = input[idx + 1];
            output[idx + 2] = input[idx + 2];
            output[idx + 3] = computeAlpha(dist, params.threshold, params.feather, params.invertSelection);
        }
    }

    void BackgroundRemover::processLuminance(const uint8_t* input, uint8_t* output,
                                              uint32_t width, uint32_t height, const RemovalParams& params)
    {
        float bgLum = luminance(params.bgColor[0], params.bgColor[1], params.bgColor[2]);

        for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i)
        {
            size_t idx = i * 4;
            float r = input[idx + 0] / 255.0f;
            float g = input[idx + 1] / 255.0f;
            float b = input[idx + 2] / 255.0f;
            float lum = luminance(r, g, b);

            float dist = std::abs(lum - bgLum);

            output[idx + 0] = input[idx + 0];
            output[idx + 1] = input[idx + 1];
            output[idx + 2] = input[idx + 2];
            output[idx + 3] = computeAlpha(dist, params.threshold, params.feather, params.invertSelection);
        }
    }

    void BackgroundRemover::processEdgeAware(const uint8_t* input, uint8_t* output,
                                              uint32_t width, uint32_t height, const RemovalParams& params)
    {
        // Step 1: color-based removal for initial mask
        processColor(input, output, width, height, params);

        // Step 2: Sobel edge map
        std::vector<float> edgeMap(static_cast<size_t>(width) * height, 0.0f);
        float maxEdge = 0.0f;

        for (uint32_t y = 1; y < height - 1; ++y)
        {
            for (uint32_t x = 1; x < width - 1; ++x)
            {
                float mag = sobelMagnitude(input, width, height, x, y);
                size_t idx = static_cast<size_t>(y) * width + x;
                edgeMap[idx] = mag;
                maxEdge = std::max(maxEdge, mag);
            }
        }

        if (maxEdge < 0.001f) return;

        // Step 3: preserve edge detail
        constexpr float edgeThreshold = 0.15f;

        for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i)
        {
            float edgeStrength = edgeMap[i] / maxEdge;
            if (edgeStrength > edgeThreshold)
            {
                size_t idx = i * 4;
                float currentAlpha = output[idx + 3] / 255.0f;
                float edgeFactor = std::clamp((edgeStrength - edgeThreshold) / (1.0f - edgeThreshold), 0.0f, 1.0f);
                float newAlpha = std::max(currentAlpha, edgeFactor);

                if (params.invertSelection)
                    newAlpha = std::min(currentAlpha, 1.0f - edgeFactor);

                output[idx + 3] = static_cast<uint8_t>(std::clamp(newAlpha * 255.0f, 0.0f, 255.0f));
            }
        }
    }

    RemovalResult BackgroundRemover::process(const uint8_t* rgbaData, uint32_t width, uint32_t height,
                                              const RemovalParams& params)
    {
        RemovalResult result;
        result.width = width;
        result.height = height;
        result.rgbaData.resize(static_cast<size_t>(width) * height * 4);

        RemovalParams effectiveParams = params;

        if (params.autoDetectColor)
        {
            autoDetectBackground(rgbaData, width, height, effectiveParams.bgColor);
        }

        result.detectedBgColor[0] = effectiveParams.bgColor[0];
        result.detectedBgColor[1] = effectiveParams.bgColor[1];
        result.detectedBgColor[2] = effectiveParams.bgColor[2];

        switch (effectiveParams.method)
        {
        case DetectionMethod::Color:
            processColor(rgbaData, result.rgbaData.data(), width, height, effectiveParams);
            break;
        case DetectionMethod::Luminance:
            processLuminance(rgbaData, result.rgbaData.data(), width, height, effectiveParams);
            break;
        case DetectionMethod::EdgeAware:
            processEdgeAware(rgbaData, result.rgbaData.data(), width, height, effectiveParams);
            break;
        }

        return result;
    }

    bool BackgroundRemover::saveAsVFImage(const RemovalResult& result, const std::string& outputPath)
    {
        if (!result.valid())
            return false;
        return ImageWriter::write(outputPath, result.width, result.height, result.rgbaData);
    }
}
