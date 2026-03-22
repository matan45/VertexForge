#include "VegetationDensityBrushApplicator.hpp"
#include <algorithm>
#include <cmath>

namespace vegetation
{
    bool VegetationDensityBrushApplicator::apply(VegetationDensityMap& densityMap, const ApplyParams& params)
    {
        if (!densityMap.isInitialized())
        {
            return false;
        }

        DensityBrushType effectiveType = params.brushType;
        if (params.invert)
        {
            if (effectiveType == DensityBrushType::Paint)
            {
                effectiveType = DensityBrushType::Erase;
            }
            else if (effectiveType == DensityBrushType::Erase)
            {
                effectiveType = DensityBrushType::Paint;
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
                    case DensityBrushType::Paint:
                        paintDensity(densityMap, x, z, influence);
                        break;
                    case DensityBrushType::Erase:
                        eraseDensity(densityMap, x, z, influence);
                        break;
                    case DensityBrushType::Smooth:
                        smoothDensity(densityMap, x, z, influence);
                        break;
                    case DensityBrushType::Fill:
                        fillDensity(densityMap, x, z, influence);
                        break;
                }

                modified = true;
            }
        }

        return modified;
    }

    float VegetationDensityBrushApplicator::computeNormalizedDistance(
        const glm::vec2& texelWorldPos,
        const glm::vec2& brushCenter,
        float brushRadius,
        terrain::BrushShape shape)
    {
        glm::vec2 delta = texelWorldPos - brushCenter;

        if (shape == terrain::BrushShape::Circle)
        {
            return glm::length(delta) / brushRadius;
        }
        else
        {
            return std::max(std::abs(delta.x), std::abs(delta.y)) / brushRadius;
        }
    }

    float VegetationDensityBrushApplicator::applyFalloff(float t, terrain::BrushFalloff falloff)
    {
        switch (falloff)
        {
            case terrain::BrushFalloff::Constant: return 1.0f;
            case terrain::BrushFalloff::Linear:   return 1.0f - t;
            case terrain::BrushFalloff::Smooth:   return 1.0f - t * t * (3.0f - 2.0f * t);
            case terrain::BrushFalloff::Sharp:    return 1.0f - t * t;
            default: return 0.0f;
        }
    }

    void VegetationDensityBrushApplicator::paintDensity(
        VegetationDensityMap& dm, uint32_t x, uint32_t z, float influence)
    {
        float current = dm.getDensity(x, z);
        float newValue = std::min(current + influence, 10.0f);
        dm.setDensity(x, z, newValue);
    }

    void VegetationDensityBrushApplicator::eraseDensity(
        VegetationDensityMap& dm, uint32_t x, uint32_t z, float influence)
    {
        float current = dm.getDensity(x, z);
        float newValue = std::max(current - influence, 0.0f);
        dm.setDensity(x, z, newValue);
    }

    void VegetationDensityBrushApplicator::smoothDensity(
        VegetationDensityMap& dm, uint32_t x, uint32_t z, float influence)
    {
        float sum = 0.0f;
        float count = 0.0f;

        for (int dz = -1; dz <= 1; ++dz)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                int nx = static_cast<int>(x) + dx;
                int nz = static_cast<int>(z) + dz;

                if (nx >= 0 && nx < static_cast<int>(dm.resolution) &&
                    nz >= 0 && nz < static_cast<int>(dm.resolution))
                {
                    sum += dm.getDensity(static_cast<uint32_t>(nx), static_cast<uint32_t>(nz));
                    count += 1.0f;
                }
            }
        }

        if (count > 0.0f)
        {
            float current = dm.getDensity(x, z);
            float avg = sum / count;
            float blendFactor = std::clamp(influence, 0.0f, 1.0f);
            float newValue = current + (avg - current) * blendFactor;
            dm.setDensity(x, z, std::clamp(newValue, 0.0f, 1.0f));
        }
    }

    void VegetationDensityBrushApplicator::fillDensity(
        VegetationDensityMap& dm, uint32_t x, uint32_t z, float influence)
    {
        float current = dm.getDensity(x, z);
        float blendFactor = std::clamp(influence, 0.0f, 1.0f);
        float newValue = current + (1.0f - current) * blendFactor;
        dm.setDensity(x, z, std::clamp(newValue, 0.0f, 1.0f));
    }
}
