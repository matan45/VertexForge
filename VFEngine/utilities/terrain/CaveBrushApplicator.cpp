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

    // Compute voxel index bounds for the brush sphere — avoids iterating the entire grid
    struct VoxelBounds
    {
        uint32_t minX, maxX, minY, maxY, minZ, maxZ;
    };

    static VoxelBounds computeBrushVoxelBounds(const CaveSDFData& sdf, const glm::vec3& center, float radius)
    {
        VoxelBounds b;
        glm::vec3 localMin = center - glm::vec3(radius) - sdf.localOrigin;
        glm::vec3 localMax = center + glm::vec3(radius) - sdf.localOrigin;

        b.minX = static_cast<uint32_t>(std::max(0, static_cast<int>(localMin.x / sdf.config.voxelSize)));
        b.maxX = std::min(sdf.config.resX - 1, static_cast<uint32_t>(localMax.x / sdf.config.voxelSize) + 1);
        b.minY = static_cast<uint32_t>(std::max(0, static_cast<int>(localMin.y / sdf.config.yVoxelSize)));
        b.maxY = std::min(sdf.config.resY - 1, static_cast<uint32_t>(localMax.y / sdf.config.yVoxelSize) + 1);
        b.minZ = static_cast<uint32_t>(std::max(0, static_cast<int>(localMin.z / sdf.config.voxelSize)));
        b.maxZ = std::min(sdf.config.resZ - 1, static_cast<uint32_t>(localMax.z / sdf.config.voxelSize) + 1);

        return b;
    }

    void CaveBrushApplicator::applyCarve(CaveSDFData& sdf, const ApplyParams& params)
    {
        float strength = params.brushStrength * params.deltaTime;
        auto bounds = computeBrushVoxelBounds(sdf, params.brushCenter, params.brushRadius);

        for (uint32_t z = bounds.minZ; z <= bounds.maxZ; ++z)
        {
            for (uint32_t y = bounds.minY; y <= bounds.maxY; ++y)
            {
                for (uint32_t x = bounds.minX; x <= bounds.maxX; ++x)
                {
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
        auto bounds = computeBrushVoxelBounds(sdf, params.brushCenter, params.brushRadius);

        for (uint32_t z = bounds.minZ; z <= bounds.maxZ; ++z)
        {
            for (uint32_t y = bounds.minY; y <= bounds.maxY; ++y)
            {
                for (uint32_t x = bounds.minX; x <= bounds.maxX; ++x)
                {
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

                    float current = sdf.getSDF(x, y, z);
                    size_t idx = sdf.getIndex(x, y, z);
                    float original = sdf.originalSdfGrid.empty() ? -1.0f : sdf.originalSdfGrid[idx];
                    float newValue = current - influence;
                    newValue = std::max(newValue, original);
                    sdf.setSDF(x, y, z, std::clamp(newValue, -10.0f, 10.0f));
                }
            }
        }
    }

    void CaveBrushApplicator::applySmooth(CaveSDFData& sdf, const ApplyParams& params)
    {
        float strength = params.brushStrength * params.deltaTime * 0.1f;
        auto bounds = computeBrushVoxelBounds(sdf, params.brushCenter, params.brushRadius);

        // Clamp to interior (need neighbors for Laplacian)
        bounds.minX = std::max(bounds.minX, 1u);
        bounds.minY = std::max(bounds.minY, 1u);
        bounds.minZ = std::max(bounds.minZ, 1u);
        bounds.maxX = std::min(bounds.maxX, sdf.config.resX - 2);
        bounds.maxY = std::min(bounds.maxY, sdf.config.resY - 2);
        bounds.maxZ = std::min(bounds.maxZ, sdf.config.resZ - 2);

        std::vector<float> smoothed = sdf.sdfGrid;

        for (uint32_t z = bounds.minZ; z <= bounds.maxZ; ++z)
        {
            for (uint32_t y = bounds.minY; y <= bounds.maxY; ++y)
            {
                for (uint32_t x = bounds.minX; x <= bounds.maxX; ++x)
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
