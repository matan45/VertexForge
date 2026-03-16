#pragma once
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace terrain::compression
{
    struct HeightQuantizationParams
    {
        float minH = 0.0f;
        float maxH = 0.0f;
    };

    inline HeightQuantizationParams computeHeightRange(const std::vector<float>& heights)
    {
        HeightQuantizationParams params;
        if (heights.empty()) return params;

        params.minH = heights[0];
        params.maxH = heights[0];
        for (float h : heights)
        {
            params.minH = std::min(params.minH, h);
            params.maxH = std::max(params.maxH, h);
        }
        return params;
    }

    inline std::vector<uint16_t> quantizeHeights(const std::vector<float>& heights,
                                                   const HeightQuantizationParams& params)
    {
        std::vector<uint16_t> result(heights.size());
        float range = params.maxH - params.minH;

        if (range < 1e-7f)
        {
            // Flat tile — all zeros, will dequantize back to minH
            std::fill(result.begin(), result.end(), static_cast<uint16_t>(0));
            return result;
        }

        float invRange = 65535.0f / range;
        for (size_t i = 0; i < heights.size(); ++i)
        {
            float normalized = (heights[i] - params.minH) * invRange;
            result[i] = static_cast<uint16_t>(std::clamp(normalized + 0.5f, 0.0f, 65535.0f));
        }
        return result;
    }

    inline std::vector<float> dequantizeHeights(const std::vector<uint16_t>& quantized,
                                                  const HeightQuantizationParams& params)
    {
        std::vector<float> result(quantized.size());
        float range = params.maxH - params.minH;

        if (range < 1e-7f)
        {
            std::fill(result.begin(), result.end(), params.minH);
            return result;
        }

        float scale = range / 65535.0f;
        for (size_t i = 0; i < quantized.size(); ++i)
        {
            result[i] = params.minH + quantized[i] * scale;
        }
        return result;
    }

    inline std::vector<uint8_t> quantizeWeights(const std::vector<float>& weights)
    {
        std::vector<uint8_t> result(weights.size());
        for (size_t i = 0; i < weights.size(); ++i)
        {
            result[i] = static_cast<uint8_t>(std::clamp(weights[i] * 255.0f + 0.5f, 0.0f, 255.0f));
        }
        return result;
    }

    inline std::vector<float> dequantizeWeights(const std::vector<uint8_t>& quantized)
    {
        std::vector<float> result(quantized.size());
        for (size_t i = 0; i < quantized.size(); ++i)
        {
            result[i] = quantized[i] / 255.0f;
        }
        return result;
    }
}
