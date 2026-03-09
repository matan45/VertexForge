#include "VegetationPlacementBrushApplicator.hpp"
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>
#include <random>

namespace vegetation
{
    bool VegetationPlacementBrushApplicator::place(
        VegetationPlacementData& placement, const PlaceParams& params)
    {
        float candidateX = params.brushPosition.x;
        float candidateZ = params.brushPosition.z;

        // Check tile bounds
        float tileMinX = params.tileWorldOrigin.x;
        float tileMinZ = params.tileWorldOrigin.y;
        float tileMaxX = tileMinX + params.tileWorldSize;
        float tileMaxZ = tileMinZ + params.tileWorldSize;

        if (candidateX < tileMinX || candidateX >= tileMaxX ||
            candidateZ < tileMinZ || candidateZ >= tileMaxZ)
        {
            return false;
        }

        // Check minimum spacing against existing instances
        if (params.collisionRadius > 0.0f)
        {
            float minSpacingSq = (params.collisionRadius * 2.0f) * (params.collisionRadius * 2.0f);
            for (const auto& existing : placement.getInstances())
            {
                float dx = existing.position.x - candidateX;
                float dz = existing.position.z - candidateZ;
                if (dx * dx + dz * dz < minSpacingSq)
                {
                    return false;
                }
            }
        }

        VegetationInstance instance;
        float heightY = params.brushPosition.y;
        if (params.heightData && params.heightVertexCount > 0)
        {
            heightY = sampleTerrainHeight(candidateX, candidateZ,
                params.tileWorldOrigin, params.tileWorldSize,
                params.heightData, params.heightVertexCount);
        }
        instance.position = glm::vec3(candidateX, heightY, candidateZ);
        instance.speciesId = params.speciesId;

        // Random scale
        instance.scale = params.minScale +
            randomFloat() * (params.maxScale - params.minScale);

        // Random rotation
        if (params.randomRotation > 0.0f)
        {
            instance.rotation = randomFloat() * glm::two_pi<float>() * params.randomRotation;
        }
        else
        {
            instance.rotation = 0.0f;
        }

        placement.addInstance(instance);
        return true;
    }

    bool VegetationPlacementBrushApplicator::spread(
        VegetationPlacementData& placement, const SpreadParams& params)
    {
        if (params.brushRadius <= 0.0f || params.density <= 0.0f)
            return false;

        // Grid spacing from density and collision radius
        float spacingFromDensity = 1.0f / std::max(std::sqrt(params.density), 0.1f);
        float minSpacing = (params.collisionRadius > 0.0f)
            ? std::max(params.collisionRadius * 2.0f, spacingFromDensity)
            : std::max(spacingFromDensity, 0.5f);
        float minSpacingSq = minSpacing * minSpacing;
        float cellSize = minSpacing;

        // Tile bounds
        float tileMinX = params.tileWorldOrigin.x;
        float tileMinZ = params.tileWorldOrigin.y;
        float tileMaxX = tileMinX + params.tileWorldSize;
        float tileMaxZ = tileMinZ + params.tileWorldSize;

        // Grid cell range covering brush area, clamped to tile
        float brushMinX = std::max(params.brushCenter.x - params.brushRadius, tileMinX);
        float brushMaxX = std::min(params.brushCenter.x + params.brushRadius, tileMaxX);
        float brushMinZ = std::max(params.brushCenter.y - params.brushRadius, tileMinZ);
        float brushMaxZ = std::min(params.brushCenter.y + params.brushRadius, tileMaxZ);

        if (brushMinX >= brushMaxX || brushMinZ >= brushMaxZ)
            return false;

        int cellStartX = static_cast<int>(std::floor(brushMinX / cellSize));
        int cellEndX = static_cast<int>(std::floor(brushMaxX / cellSize));
        int cellStartZ = static_cast<int>(std::floor(brushMinZ / cellSize));
        int cellEndZ = static_cast<int>(std::floor(brushMaxZ / cellSize));

        int totalCells = (cellEndX - cellStartX + 1) * (cellEndZ - cellStartZ + 1);
        if (totalCells > 2000)
            return false;

        bool added = false;

        for (int cz = cellStartZ; cz <= cellEndZ; ++cz)
        {
            for (int cx = cellStartX; cx <= cellEndX; ++cx)
            {
                // Deterministic per-cell jitter
                uint32_t h0 = cellHash(cx, cz, 0);
                uint32_t h1 = cellHash(cx, cz, 1);
                uint32_t h2 = cellHash(cx, cz, 2);
                uint32_t h3 = cellHash(cx, cz, 3);

                float jitterX = (hashToFloat(h0) - 0.5f) * cellSize * 0.8f;
                float jitterZ = (hashToFloat(h1) - 0.5f) * cellSize * 0.8f;
                float candidateX = (static_cast<float>(cx) + 0.5f) * cellSize + jitterX;
                float candidateZ = (static_cast<float>(cz) + 0.5f) * cellSize + jitterZ;

                // Skip if outside tile or brush circle
                if (candidateX < tileMinX || candidateX >= tileMaxX ||
                    candidateZ < tileMinZ || candidateZ >= tileMaxZ)
                    continue;

                float dx = candidateX - params.brushCenter.x;
                float dz = candidateZ - params.brushCenter.y;
                if (dx * dx + dz * dz > params.brushRadius * params.brushRadius)
                    continue;

                // Check spacing against existing instances
                bool tooClose = false;
                for (const auto& existing : placement.getInstances())
                {
                    float ex = existing.position.x - candidateX;
                    float ez = existing.position.z - candidateZ;
                    if (ex * ex + ez * ez < minSpacingSq)
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
                instance.scale = params.minScale +
                    hashToFloat(h2) * (params.maxScale - params.minScale);
                instance.rotation = (params.randomRotation > 0.0f)
                    ? hashToFloat(h3) * glm::two_pi<float>() * params.randomRotation
                    : 0.0f;

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

    float VegetationPlacementBrushApplicator::randomFloat()
    {
        static thread_local std::mt19937 rng(std::random_device{}());
        static thread_local std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        return dist(rng);
    }

    uint32_t VegetationPlacementBrushApplicator::cellHash(int cellX, int cellZ, uint32_t seed)
    {
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
