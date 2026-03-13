#include "TerrainWeightMap.hpp"
#include "../print/Log.hpp"
#include <algorithm>

namespace terrain
{
    float TileWeightMapData::getWeight(uint32_t channel, uint32_t x, uint32_t z) const
    {
        if (channel >= layerWeights.size() || x >= resolution || z >= resolution)
            return 0.0f;

        size_t idx = static_cast<size_t>(z) * resolution + x;
        return layerWeights[channel][idx];
    }

    void TileWeightMapData::setWeight(uint32_t channel, uint32_t x, uint32_t z, float value)
    {
        if (channel >= layerWeights.size() || x >= resolution || z >= resolution)
            return;

        size_t idx = static_cast<size_t>(z) * resolution + x;
        layerWeights[channel][idx] = std::clamp(value, 0.0f, 1.0f);
    }

    void TileWeightMapData::normalizeAt(uint32_t x, uint32_t z)
    {
        if (x >= resolution || z >= resolution || layerWeights.empty())
            return;

        size_t idx = static_cast<size_t>(z) * resolution + x;

        float sum = 0.0f;
        for (const auto& channel : layerWeights)
        {
            sum += channel[idx];
        }

        if (sum > 0.001f)
        {
            float invSum = 1.0f / sum;
            for (auto& channel : layerWeights)
            {
                channel[idx] *= invSum;
            }
        }
        else
        {
            for (auto& channel : layerWeights)
            {
                channel[idx] = 0.0f;
            }
            layerWeights[0][idx] = 1.0f;
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

    void TileWeightMapData::initializeDefault(uint32_t vertexResolution)
    {
        resolution = vertexResolution;

        size_t texelCount = getTexelCount();
        layerWeights.clear();
        layerWeights.resize(WEIGHT_CHANNELS);

        layerWeights[0].assign(texelCount, 1.0f);
        for (uint8_t i = 1; i < WEIGHT_CHANNELS; ++i)
        {
            layerWeights[i].assign(texelCount, 0.0f);
        }

        layerIndices = {0, 1, 2, 3};
    }

    void TileWeightMapData::packRGBA(uint32_t x, uint32_t z,
                                      float& r, float& g, float& b, float& a) const
    {
        r = (0 < layerWeights.size()) ? getWeight(0, x, z) : 0.0f;
        g = (1 < layerWeights.size()) ? getWeight(1, x, z) : 0.0f;
        b = (2 < layerWeights.size()) ? getWeight(2, x, z) : 0.0f;
        a = (3 < layerWeights.size()) ? getWeight(3, x, z) : 0.0f;
    }

    uint8_t TileWeightMapData::findChannel(uint8_t paletteLayer) const
    {
        for (uint8_t ch = 0; ch < WEIGHT_CHANNELS; ++ch)
        {
            if (layerIndices[ch] == paletteLayer)
                return ch;
        }
        return 0xFF;
    }

    uint8_t TileWeightMapData::assignChannel(uint8_t paletteLayer)
    {
        // Already assigned?
        uint8_t existing = findChannel(paletteLayer);
        if (existing != 0xFF)
            return existing;

        if (!isInitialized())
            return 0;

        // Find channel with near-zero total weight
        constexpr float EPSILON = 0.01f;
        size_t texelCount = getTexelCount();

        float channelSums[WEIGHT_CHANNELS] = {};
        for (uint8_t ch = 0; ch < WEIGHT_CHANNELS; ++ch)
        {
            if (ch < layerWeights.size())
            {
                for (size_t i = 0; i < texelCount; ++i)
                    channelSums[ch] += layerWeights[ch][i];
            }
        }

        // Find a near-zero channel
        for (uint8_t ch = 0; ch < WEIGHT_CHANNELS; ++ch)
        {
            if (channelSums[ch] < EPSILON)
            {
                layerIndices[ch] = paletteLayer;
                return ch;
            }
        }

        // All occupied — find channel with lowest total weight, zero it, reassign
        uint8_t minCh = 0;
        float minSum = channelSums[0];
        for (uint8_t ch = 1; ch < WEIGHT_CHANNELS; ++ch)
        {
            if (channelSums[ch] < minSum)
            {
                minSum = channelSums[ch];
                minCh = ch;
            }
        }

        vfLogWarning("TerrainWeightMap: Tile has all 4 channels occupied. "
                     "Evicting palette layer {} (channel {}, weight sum {:.3f}) to assign palette layer {}",
                     layerIndices[minCh], minCh, minSum, paletteLayer);

        if (minCh < layerWeights.size())
        {
            std::fill(layerWeights[minCh].begin(), layerWeights[minCh].end(), 0.0f);
            normalizeAll();
        }

        layerIndices[minCh] = paletteLayer;
        return minCh;
    }
}
