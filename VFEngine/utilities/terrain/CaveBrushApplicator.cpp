#include "CaveBrushApplicator.hpp"
#include <algorithm>
#include <cmath>

namespace terrain
{
    bool CaveBrushApplicator::apply(CaveSDFData& sdf, const ApplyParams& params)
    {
        if (!sdf.isInitialized())
            return false;

        CaveBrushType effectiveType = params.brushType;
        if (params.invert)
        {
            if (effectiveType == CaveBrushType::Carve)
                effectiveType = CaveBrushType::Fill;
            else if (effectiveType == CaveBrushType::Fill)
                effectiveType = CaveBrushType::Carve;
        }

        ApplyParams effectiveParams = params;
        effectiveParams.brushType = effectiveType;

        switch (effectiveType)
        {
        case CaveBrushType::Carve:
            applyCarve(sdf, effectiveParams);
            break;
        case CaveBrushType::Fill:
            applyFill(sdf, effectiveParams);
            break;
        case CaveBrushType::Smooth:
            applySmooth(sdf, effectiveParams);
            break;
        }

        sdf.isDirty = true;
        return true;
    }

    void CaveBrushApplicator::applyCarve(CaveSDFData& sdf, const ApplyParams& params)
    {
        float strength = params.brushStrength * params.deltaTime;

        for (uint32_t z = 0; z < sdf.config.resZ; ++z)
        {
            for (uint32_t y = 0; y < sdf.config.resY; ++y)
            {
                for (uint32_t x = 0; x < sdf.config.resX; ++x)
                {
                    // Only carve into originally-solid voxels (below terrain surface)
                    // Never modify air voxels above the surface — prevents mesh poking through
                    if (!sdf.originalSdfGrid.empty())
                    {
                        size_t idx = sdf.getIndex(x, y, z);
                        if (sdf.originalSdfGrid[idx] > 0.0f)
                            continue; // Above surface, skip
                    }

                    glm::vec3 worldPos = sdf.getWorldPosition(x, y, z);
                    float dist = computeNormalizedDistance3D(
                        worldPos, params.brushCenter, params.brushRadius, params.shape);

                    if (dist >= 1.0f)
                        continue;

                    float influence = applyFalloff(dist, params.falloff) * strength;

                    // Carve: push SDF toward positive (air/cave)
                    float current = sdf.getSDF(x, y, z);
                    float target = params.brushRadius * (1.0f - dist);
                    float newValue = current + influence;
                    newValue = std::min(newValue, target);
                    sdf.setSDF(x, y, z, std::clamp(newValue, -10.0f, 10.0f));
                }
            }
        }
    }

    void CaveBrushApplicator::applyFill(CaveSDFData& sdf, const ApplyParams& params)
    {
        float strength = params.brushStrength * params.deltaTime;

        for (uint32_t z = 0; z < sdf.config.resZ; ++z)
        {
            for (uint32_t y = 0; y < sdf.config.resY; ++y)
            {
                for (uint32_t x = 0; x < sdf.config.resX; ++x)
                {
                    // Only fill voxels that were originally solid (don't fill above surface)
                    if (!sdf.originalSdfGrid.empty())
                    {
                        size_t idx = sdf.getIndex(x, y, z);
                        if (sdf.originalSdfGrid[idx] > 0.0f)
                            continue;
                    }

                    glm::vec3 worldPos = sdf.getWorldPosition(x, y, z);
                    float dist = computeNormalizedDistance3D(
                        worldPos, params.brushCenter, params.brushRadius, params.shape);

                    if (dist >= 1.0f)
                        continue;

                    float influence = applyFalloff(dist, params.falloff) * strength;

                    // Fill: push SDF back toward original (restore solid)
                    float current = sdf.getSDF(x, y, z);
                    size_t idx = sdf.getIndex(x, y, z);
                    float original = sdf.originalSdfGrid.empty() ? -1.0f : sdf.originalSdfGrid[idx];
                    float newValue = current - influence;
                    newValue = std::max(newValue, original); // Don't go more solid than original
                    sdf.setSDF(x, y, z, std::clamp(newValue, -10.0f, 10.0f));
                }
            }
        }
    }

    void CaveBrushApplicator::applySmooth(CaveSDFData& sdf, const ApplyParams& params)
    {
        float strength = params.brushStrength * params.deltaTime * 0.1f; // Smooth is more subtle

        // Work on a copy to avoid feedback within single pass
        std::vector<float> smoothed = sdf.sdfGrid;

        for (uint32_t z = 1; z + 1 < sdf.config.resZ; ++z)
        {
            for (uint32_t y = 1; y + 1 < sdf.config.resY; ++y)
            {
                for (uint32_t x = 1; x + 1 < sdf.config.resX; ++x)
                {
                    glm::vec3 worldPos = sdf.getWorldPosition(x, y, z);
                    float dist = computeNormalizedDistance3D(
                        worldPos, params.brushCenter, params.brushRadius, params.shape);

                    if (dist >= 1.0f)
                        continue;

                    float influence = applyFalloff(dist, params.falloff) * strength;

                    // 6-neighbor Laplacian average
                    float avg = (sdf.getSDF(x - 1, y, z) + sdf.getSDF(x + 1, y, z) +
                                 sdf.getSDF(x, y - 1, z) + sdf.getSDF(x, y + 1, z) +
                                 sdf.getSDF(x, y, z - 1) + sdf.getSDF(x, y, z + 1)) / 6.0f;

                    float current = sdf.getSDF(x, y, z);
                    float newValue = current + (avg - current) * influence;
                    smoothed[sdf.getIndex(x, y, z)] = std::clamp(newValue, -10.0f, 10.0f);
                }
            }
        }

        sdf.sdfGrid = std::move(smoothed);
    }

    float CaveBrushApplicator::computeNormalizedDistance3D(
        const glm::vec3& worldPos,
        const glm::vec3& brushCenter,
        float brushRadius,
        BrushShape shape)
    {
        glm::vec3 delta = worldPos - brushCenter;

        if (shape == BrushShape::Circle)
        {
            return glm::length(delta) / brushRadius;
        }
        else
        {
            return std::max({std::abs(delta.x), std::abs(delta.y), std::abs(delta.z)}) / brushRadius;
        }
    }

    // Must match GPU shader brush_influence.glsl and other applicators
    float CaveBrushApplicator::applyFalloff(float t, BrushFalloff falloff)
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
}
