#include "WeightBrushApplicator.hpp"
#include <algorithm>
#include <cmath>

namespace terrain
{
    bool WeightBrushApplicator::apply(TileWeightMapData& weightMap, const ApplyParams& params)
    {
        if (!weightMap.isInitialized())
        {
            return false;
        }

        if (params.activeLayer >= weightMap.layerWeights.size())
        {
            return false;
        }

        PaintBrushType effectiveType = params.brushType;
        if (params.invert)
        {
            if (effectiveType == PaintBrushType::PaintLayer)
            {
                effectiveType = PaintBrushType::EraseLayer;
            }
            else if (effectiveType == PaintBrushType::EraseLayer)
            {
                effectiveType = PaintBrushType::PaintLayer;
            }
        }

        bool modified = false;

        for (uint32_t z = 0; z < params.verticesPerSide; ++z)
        {
            for (uint32_t x = 0; x < params.verticesPerSide; ++x)
            {
                glm::vec2 texelWorldPos = params.tileWorldOrigin
                    + glm::vec2(static_cast<float>(x), static_cast<float>(z)) * params.vertexSpacing;

                float dist = computeNormalizedDistance(
                    texelWorldPos, params.brushCenter, params.brushRadius, params.shape);

                if (dist >= 1.0f)
                {
                    continue;
                }

                float falloffValue = applyFalloff(dist, params.falloff);
                float influence = falloffValue * params.brushStrength * params.brushOpacity * params.deltaTime;
                influence = std::clamp(influence, 0.0f, 1.0f);

                if (influence <= 0.0001f)
                {
                    continue;
                }

                switch (effectiveType)
                {
                    case PaintBrushType::PaintLayer:
                        paintLayer(weightMap, x, z, params.activeLayer, influence, params.overlayMask);
                        break;
                    case PaintBrushType::EraseLayer:
                        eraseLayer(weightMap, x, z, params.activeLayer, influence, params.overlayMask);
                        break;
                    case PaintBrushType::SmoothWeights:
                        smoothWeights(weightMap, x, z, influence, params.overlayMask);
                        break;
                    case PaintBrushType::FillLayer:
                        fillLayer(weightMap, x, z, params.activeLayer, influence, params.overlayMask);
                        break;
                }

                modified = true;
            }
        }

        return modified;
    }

    float WeightBrushApplicator::computeNormalizedDistance(
        const glm::vec2& texelWorldPos,
        const glm::vec2& brushCenter,
        float brushRadius,
        BrushShape shape)
    {
        glm::vec2 delta = texelWorldPos - brushCenter;

        if (shape == BrushShape::Circle)
        {
            return glm::length(delta) / brushRadius;
        }
        else
        {
            return std::max(std::abs(delta.x), std::abs(delta.y)) / brushRadius;
        }
    }

    // Must match GPU shader brush_influence.glsl:28-38
    float WeightBrushApplicator::applyFalloff(float t, BrushFalloff falloff)
    {
        switch (falloff)
        {
            case BrushFalloff::Constant: return 1.0f;
            case BrushFalloff::Linear:   return 1.0f - t;
            case BrushFalloff::Smooth:   return 1.0f - t * t * (3.0f - 2.0f * t);
            case BrushFalloff::Sharp:    return 1.0f - t * t;
            default: return 0.0f;
        }
    }

    void WeightBrushApplicator::paintLayer(
        TileWeightMapData& wm, uint32_t x, uint32_t z,
        uint32_t layer, float influence, uint16_t overlayMask)
    {
        float currentWeight = wm.getWeight(layer, x, z);
        float newWeight = std::min(currentWeight + influence, 1.0f);
        float delta = newWeight - currentWeight;

        if (delta <= 0.0f)
        {
            return;
        }

        wm.setWeight(layer, x, z, newWeight);

        // Overlay layers are independent - don't reduce other layers
        if (isOverlay(overlayMask, layer))
        {
            return;
        }

        // Decrease only other BASE layers proportionally to maintain base sum = 1.0
        float otherBaseSum = 0.0f;
        uint32_t layerCount = static_cast<uint32_t>(wm.layerWeights.size());
        for (uint32_t i = 0; i < layerCount; ++i)
        {
            if (i != layer && !isOverlay(overlayMask, i))
            {
                otherBaseSum += wm.getWeight(i, x, z);
            }
        }

        if (otherBaseSum > 0.001f)
        {
            float scale = (otherBaseSum - delta) / otherBaseSum;
            scale = std::max(scale, 0.0f);
            for (uint32_t i = 0; i < layerCount; ++i)
            {
                if (i != layer && !isOverlay(overlayMask, i))
                {
                    wm.setWeight(i, x, z, wm.getWeight(i, x, z) * scale);
                }
            }
        }

        wm.normalizeAt(x, z, overlayMask);
    }

    void WeightBrushApplicator::eraseLayer(
        TileWeightMapData& wm, uint32_t x, uint32_t z,
        uint32_t layer, float influence, uint16_t overlayMask)
    {
        float currentWeight = wm.getWeight(layer, x, z);
        float newWeight = std::max(currentWeight - influence, 0.0f);
        float delta = currentWeight - newWeight;

        if (delta <= 0.0f)
        {
            return;
        }

        wm.setWeight(layer, x, z, newWeight);

        // Overlay layers are independent - just reduce, no redistribution
        if (isOverlay(overlayMask, layer))
        {
            return;
        }

        // Redistribute removed weight to a base layer
        uint32_t fallbackLayer = (layer == 0) ? 1 : 0;
        // Ensure fallback is a base layer
        if (isOverlay(overlayMask, fallbackLayer))
        {
            fallbackLayer = 0;
        }
        if (fallbackLayer < wm.layerWeights.size() && fallbackLayer != layer)
        {
            float fallbackWeight = wm.getWeight(fallbackLayer, x, z);
            wm.setWeight(fallbackLayer, x, z, fallbackWeight + delta);
        }

        wm.normalizeAt(x, z, overlayMask);
    }

    void WeightBrushApplicator::smoothWeights(
        TileWeightMapData& wm, uint32_t x, uint32_t z,
        float influence, uint16_t overlayMask)
    {
        uint32_t layerCount = static_cast<uint32_t>(wm.layerWeights.size());
        std::vector<float> avgWeights(layerCount, 0.0f);
        float count = 0.0f;

        for (int dz = -1; dz <= 1; ++dz)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                int nx = static_cast<int>(x) + dx;
                int nz = static_cast<int>(z) + dz;

                if (nx >= 0 && nx < static_cast<int>(wm.resolution) &&
                    nz >= 0 && nz < static_cast<int>(wm.resolution))
                {
                    for (uint32_t i = 0; i < layerCount; ++i)
                    {
                        avgWeights[i] += wm.getWeight(i, static_cast<uint32_t>(nx),
                                                       static_cast<uint32_t>(nz));
                    }
                    count += 1.0f;
                }
            }
        }

        if (count > 0.0f)
        {
            float invCount = 1.0f / count;
            float blendFactor = std::clamp(influence, 0.0f, 1.0f);

            for (uint32_t i = 0; i < layerCount; ++i)
            {
                float current = wm.getWeight(i, x, z);
                float avg = avgWeights[i] * invCount;
                wm.setWeight(i, x, z, current + (avg - current) * blendFactor);
            }

            wm.normalizeAt(x, z, overlayMask);
        }
    }

    void WeightBrushApplicator::fillLayer(
        TileWeightMapData& wm, uint32_t x, uint32_t z,
        uint32_t layer, float influence, uint16_t overlayMask)
    {
        float blendFactor = std::clamp(influence, 0.0f, 1.0f);
        uint32_t layerCount = static_cast<uint32_t>(wm.layerWeights.size());
        bool activeIsOverlay = isOverlay(overlayMask, layer);

        for (uint32_t i = 0; i < layerCount; ++i)
        {
            // Only fill among layers of the same type (base<->base, overlay<->overlay)
            if (isOverlay(overlayMask, i) != activeIsOverlay)
                continue;

            float current = wm.getWeight(i, x, z);
            float target = (i == layer) ? 1.0f : 0.0f;
            wm.setWeight(i, x, z, current + (target - current) * blendFactor);
        }

        wm.normalizeAt(x, z, overlayMask);
    }
}
