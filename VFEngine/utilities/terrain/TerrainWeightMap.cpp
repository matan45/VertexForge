#include "TerrainWeightMap.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>

namespace terrain
{
    float TileWeightMapData::getWeight(uint32_t layerIndex, uint32_t x, uint32_t z) const
    {
        if (layerIndex >= layerWeights.size() || x >= resolution || z >= resolution)
            return 0.0f;

        size_t idx = static_cast<size_t>(z) * resolution + x;
        return layerWeights[layerIndex][idx];
    }

    void TileWeightMapData::setWeight(uint32_t layerIndex, uint32_t x, uint32_t z, float value)
    {
        if (layerIndex >= layerWeights.size() || x >= resolution || z >= resolution)
            return;

        size_t idx = static_cast<size_t>(z) * resolution + x;
        layerWeights[layerIndex][idx] = std::clamp(value, 0.0f, 1.0f);
    }

    void TileWeightMapData::normalizeAt(uint32_t x, uint32_t z)
    {
        if (x >= resolution || z >= resolution || layerWeights.empty())
            return;

        size_t idx = static_cast<size_t>(z) * resolution + x;

        float sum = 0.0f;
        for (const auto& layer : layerWeights)
        {
            sum += layer[idx];
        }

        if (sum > 0.001f)
        {
            float invSum = 1.0f / sum;
            for (auto& layer : layerWeights)
            {
                layer[idx] *= invSum;
            }
        }
        else
        {
                for (auto& layer : layerWeights)
            {
                layer[idx] = 0.0f;
            }
            layerWeights[0][idx] = 1.0f;
        }
    }

    void TileWeightMapData::normalizeAt(uint32_t x, uint32_t z, uint16_t overlayMask)
    {
        if (overlayMask == 0)
        {
            normalizeAt(x, z);
            return;
        }

        if (x >= resolution || z >= resolution || layerWeights.empty())
            return;

        size_t idx = static_cast<size_t>(z) * resolution + x;
        uint32_t layerCount = static_cast<uint32_t>(layerWeights.size());

        // Clamp overlay layers independently to [0,1]
        for (uint32_t i = 0; i < layerCount; ++i)
        {
            if (i < 16 && (overlayMask & (1u << i)))
            {
                layerWeights[i][idx] = std::clamp(layerWeights[i][idx], 0.0f, 1.0f);
            }
        }

        // Normalize only base (non-overlay) layers to sum=1.0
        float baseSum = 0.0f;
        for (uint32_t i = 0; i < layerCount; ++i)
        {
            if (i >= 16 || !(overlayMask & (1u << i)))
            {
                baseSum += layerWeights[i][idx];
            }
        }

        if (baseSum > 0.001f)
        {
            float invSum = 1.0f / baseSum;
            for (uint32_t i = 0; i < layerCount; ++i)
            {
                if (i >= 16 || !(overlayMask & (1u << i)))
                {
                    layerWeights[i][idx] *= invSum;
                }
            }
        }
        else
        {
            // Fallback: reset base layer 0 to 1.0
            for (uint32_t i = 0; i < layerCount; ++i)
            {
                if (i >= 16 || !(overlayMask & (1u << i)))
                {
                    layerWeights[i][idx] = 0.0f;
                }
            }
            if (!layerWeights.empty() && !(overlayMask & 1u))
            {
                layerWeights[0][idx] = 1.0f;
            }
        }
    }

    void TileWeightMapData::normalizeAll()
    {
        for (uint32_t z = 0; z < resolution; ++z)
        {
            for (uint32_t x = 0; x < resolution; ++x)
            {
                normalizeAt(x, z);
            }
        }
    }

    void TileWeightMapData::normalizeAll(uint16_t overlayMask)
    {
        for (uint32_t z = 0; z < resolution; ++z)
        {
            for (uint32_t x = 0; x < resolution; ++x)
            {
                normalizeAt(x, z, overlayMask);
            }
        }
    }

    void TileWeightMapData::initializeDefault(uint32_t vertexResolution, uint8_t layerCount)
    {
        resolution = vertexResolution;
        activeLayerCount = std::max(layerCount, static_cast<uint8_t>(1));

        size_t texelCount = getTexelCount();
        layerWeights.clear();
        layerWeights.resize(activeLayerCount);

        layerWeights[0].assign(texelCount, 1.0f);
        for (uint8_t i = 1; i < activeLayerCount; ++i)
        {
            layerWeights[i].assign(texelCount, 0.0f);
        }
    }

    void TileWeightMapData::setLayerCount(uint8_t newCount)
    {
        if (newCount == 0)
            newCount = 1;

        if (!isInitialized())
        {
            activeLayerCount = newCount;
            return;
        }

        uint8_t oldCount = activeLayerCount;
        activeLayerCount = newCount;
        size_t texelCount = getTexelCount();

        if (newCount > oldCount)
        {
            layerWeights.resize(newCount);
            for (uint8_t i = oldCount; i < newCount; ++i)
            {
                layerWeights[i].assign(texelCount, 0.0f);
            }
        }
        else if (newCount < oldCount)
        {
            layerWeights.resize(newCount);
            normalizeAll();
        }
    }

    void TileWeightMapData::packRGBA(uint32_t textureIndex, uint32_t x, uint32_t z,
                                      float& r, float& g, float& b, float& a) const
    {
        uint32_t baseLayer = textureIndex * 4;
        r = (baseLayer + 0 < layerWeights.size()) ? getWeight(baseLayer + 0, x, z) : 0.0f;
        g = (baseLayer + 1 < layerWeights.size()) ? getWeight(baseLayer + 1, x, z) : 0.0f;
        b = (baseLayer + 2 < layerWeights.size()) ? getWeight(baseLayer + 2, x, z) : 0.0f;
        a = (baseLayer + 3 < layerWeights.size()) ? getWeight(baseLayer + 3, x, z) : 0.0f;
    }

}
