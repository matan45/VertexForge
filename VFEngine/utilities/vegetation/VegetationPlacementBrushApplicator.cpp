#include "VegetationPlacementBrushApplicator.hpp"
#include <iostream>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

namespace vegetation
{
    bool VegetationPlacementBrushApplicator::spread(
        VegetationPlacementData& placement, const SpreadParams& params)
    {
        if (params.brushRadius <= 0.0f || params.density <= 0.0f)
        {
            return false;
        }

        // Compute grid spacing from density and collision radius
        // Higher density = tighter spacing
        float spacingFromDensity = 1.0f / std::max(std::sqrt(params.density), 0.1f);
        float minSpacing = (params.collisionRadius > 0.0f)
            ? std::max(params.collisionRadius * 2.0f, spacingFromDensity)
            : std::max(spacingFromDensity, 0.5f);
        float minSpacingSq = minSpacing * minSpacing;

        // Grid cell size matches spacing
        float cellSize = minSpacing;

        // Tile bounds in world XZ
        float tileMinX = params.tileWorldOrigin.x;
        float tileMinZ = params.tileWorldOrigin.y;
        float tileMaxX = tileMinX + params.tileWorldSize;
        float tileMaxZ = tileMinZ + params.tileWorldSize;

        // Compute grid cell range covering the brush area
        float brushMinX = params.brushCenter.x - params.brushRadius;
        float brushMaxX = params.brushCenter.x + params.brushRadius;
        float brushMinZ = params.brushCenter.y - params.brushRadius;
        float brushMaxZ = params.brushCenter.y + params.brushRadius;

        // Clamp to tile bounds
        brushMinX = std::max(brushMinX, tileMinX);
        brushMaxX = std::min(brushMaxX, tileMaxX);
        brushMinZ = std::max(brushMinZ, tileMinZ);
        brushMaxZ = std::min(brushMaxZ, tileMaxZ);

        if (brushMinX >= brushMaxX || brushMinZ >= brushMaxZ)
        {
            return false;
        }

        int cellStartX = static_cast<int>(std::floor(brushMinX / cellSize));
        int cellEndX = static_cast<int>(std::floor(brushMaxX / cellSize));
        int cellStartZ = static_cast<int>(std::floor(brushMinZ / cellSize));
        int cellEndZ = static_cast<int>(std::floor(brushMaxZ / cellSize));

        // Cap iteration count to prevent excessive processing
        int maxCells = 2000;
        int totalCells = (cellEndX - cellStartX + 1) * (cellEndZ - cellStartZ + 1);
        if (totalCells > maxCells)
        {
            return false;
        }

        bool added = false;

        for (int cz = cellStartZ; cz <= cellEndZ; ++cz)
        {
            for (int cx = cellStartX; cx <= cellEndX; ++cx)
            {
                // Deterministic per-cell hash for jitter, scale, rotation
                uint32_t h0 = cellHash(cx, cz, 0);
                uint32_t h1 = cellHash(cx, cz, 1);
                uint32_t h2 = cellHash(cx, cz, 2);
                uint32_t h3 = cellHash(cx, cz, 3);

                // Cell center with jitter
                float jitterX = (hashToFloat(h0) - 0.5f) * cellSize * 0.8f;
                float jitterZ = (hashToFloat(h1) - 0.5f) * cellSize * 0.8f;
                float candidateX = (static_cast<float>(cx) + 0.5f) * cellSize + jitterX;
                float candidateZ = (static_cast<float>(cz) + 0.5f) * cellSize + jitterZ;

                glm::vec2 candidatePos(candidateX, candidateZ);

                // Skip candidates outside tile bounds
                if (candidateX < tileMinX || candidateX >= tileMaxX ||
                    candidateZ < tileMinZ || candidateZ >= tileMaxZ)
                {
                    continue;
                }

                // Check if candidate is within brush shape
                float dist = computeNormalizedDistance(
                    candidatePos, params.brushCenter, params.brushRadius, params.shape);

                if (dist >= 1.0f)
                {
                    continue;
                }

                // Apply falloff to acceptance probability (deterministic, no deltaTime)
                // This ensures placement is stable — same brush position always produces same result
                float falloffValue = applyFalloff(dist, params.falloff);
                float acceptance = falloffValue * params.opacity;

                // Use deterministic hash for acceptance test
                if (hashToFloat(h3) > acceptance)
                {
                    continue;
                }

                // Check minimum spacing against existing instances
                bool tooClose = false;
                for (const auto& existing : placement.getInstances())
                {
                    float dx = existing.position.x - candidateX;
                    float dz = existing.position.z - candidateZ;
                    if (dx * dx + dz * dz < minSpacingSq)
                    {
                        tooClose = true;
                        break;
                    }
                }
                if (tooClose) continue;

                VegetationInstance instance;
                instance.position = glm::vec3(candidateX, 0.0f, candidateZ);
                instance.speciesId = params.speciesId;

                // Deterministic scale from cell hash
                instance.scale = params.minScale +
                    hashToFloat(h2) * (params.maxScale - params.minScale);

                // Deterministic rotation from cell hash
                if (params.randomRotation > 0.0f)
                {
                    instance.rotation = hashToFloat(h3) * glm::two_pi<float>() * params.randomRotation;
                }
                else
                {
                    instance.rotation = 0.0f;
                }

                placement.addInstance(instance);
                added = true;
            }
        }

        if (added)
        {
            std::cout << "VegSpread: added instances, total=" << placement.getInstanceCount()
                << " brush=(" << params.brushCenter.x << "," << params.brushCenter.y << ")"
                << " radius=" << params.brushRadius << " density=" << params.density
                << " cellSize=" << cellSize << std::endl;
        }

        return added;
    }

    bool VegetationPlacementBrushApplicator::erase(
        VegetationPlacementData& placement, const EraseParams& params)
    {
        size_t countBefore = placement.getInstanceCount();
        placement.removeInstancesInRadius(params.brushCenter3D, params.brushRadius);
        return placement.getInstanceCount() < countBefore;
    }

    float VegetationPlacementBrushApplicator::computeNormalizedDistance(
        const glm::vec2& pos,
        const glm::vec2& brushCenter,
        float brushRadius,
        terrain::BrushShape shape)
    {
        glm::vec2 delta = pos - brushCenter;

        if (shape == terrain::BrushShape::Circle)
        {
            return glm::length(delta) / brushRadius;
        }
        else
        {
            return std::max(std::abs(delta.x), std::abs(delta.y)) / brushRadius;
        }
    }

    float VegetationPlacementBrushApplicator::applyFalloff(float t, terrain::BrushFalloff falloff)
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

    uint32_t VegetationPlacementBrushApplicator::cellHash(int cellX, int cellZ, uint32_t seed)
    {
        // Simple integer hash combining cell coordinates and seed
        uint32_t h = static_cast<uint32_t>(cellX) * 73856093u
                   ^ static_cast<uint32_t>(cellZ) * 19349663u
                   ^ seed * 83492791u;
        h = (h ^ (h >> 16)) * 0x45d9f3bu;
        h = (h ^ (h >> 16)) * 0x45d9f3bu;
        h = h ^ (h >> 16);
        return h;
    }

    float VegetationPlacementBrushApplicator::hashToFloat(uint32_t h)
    {
        return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);
    }
}
