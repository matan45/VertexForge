#include "VegetationPlacementBrushApplicator.hpp"
#include "../print/Log.hpp"
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
            vfLogWarning("Vegetation brush: area too large ({} cells, max {}). Reduce brush radius or density.",
                         totalCells, maxCells);
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
                float heightY = 0.0f;
                if (params.heightData && params.heightVertexCount > 0)
                {
                    heightY = sampleTerrainHeight(candidateX, candidateZ,
                        params.tileWorldOrigin, params.tileWorldSize,
                        params.heightData, params.heightVertexCount);
                }
                instance.position = glm::vec3(candidateX, heightY, candidateZ);
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

    float VegetationPlacementBrushApplicator::sampleTerrainHeight(
        float worldX, float worldZ,
        const glm::vec2& tileWorldOrigin, float tileWorldSize,
        const float* heightData, uint32_t vertexCount)
    {
        // Convert world position to normalized tile-local [0,1] coordinates
        float localX = (worldX - tileWorldOrigin.x) / tileWorldSize;
        float localZ = (worldZ - tileWorldOrigin.y) / tileWorldSize;

        // Convert to heightfield grid coordinates
        float quadCount = static_cast<float>(vertexCount - 1);
        float gx = localX * quadCount;
        float gz = localZ * quadCount;

        // Clamp to valid grid range
        gx = std::clamp(gx, 0.0f, quadCount);
        gz = std::clamp(gz, 0.0f, quadCount);

        // Bilinear interpolation
        uint32_t x0 = static_cast<uint32_t>(gx);
        uint32_t z0 = static_cast<uint32_t>(gz);
        uint32_t x1 = std::min(x0 + 1, vertexCount - 1);
        uint32_t z1 = std::min(z0 + 1, vertexCount - 1);

        float fx = gx - static_cast<float>(x0);
        float fz = gz - static_cast<float>(z0);

        float h00 = heightData[z0 * vertexCount + x0];
        float h10 = heightData[z0 * vertexCount + x1];
        float h01 = heightData[z1 * vertexCount + x0];
        float h11 = heightData[z1 * vertexCount + x1];

        float h0 = h00 + (h10 - h00) * fx;
        float h1 = h01 + (h11 - h01) * fx;
        return h0 + (h1 - h0) * fz;
    }
}
