#include "VegetationPlacementBrushApplicator.hpp"
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>
#include <random>

namespace vegetation
{
    bool VegetationPlacementBrushApplicator::scatter(
        VegetationPlacementData& placement, const ScatterParams& params)
    {
        if (params.brushRadius <= 0.0f || params.density <= 0.0f)
        {
            return false;
        }

        // Calculate number of candidate points based on brush area and density
        float brushArea = glm::pi<float>() * params.brushRadius * params.brushRadius;
        uint32_t candidateCount = static_cast<uint32_t>(brushArea * params.density);
        candidateCount = std::max(candidateCount, 1u);
        candidateCount = std::min(candidateCount, 1000u); // Cap to prevent excessive placement

        // Tile bounds in world XZ - only place instances within this tile
        float tileMinX = params.tileWorldOrigin.x;
        float tileMinZ = params.tileWorldOrigin.y;
        float tileMaxX = tileMinX + params.tileWorldSize;
        float tileMaxZ = tileMinZ + params.tileWorldSize;

        // Seed RNG from position hash for deterministic results
        uint32_t seed = positionHash(params.brushCenter);
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> distAngle(0.0f, glm::two_pi<float>());
        std::uniform_real_distribution<float> distRadius(0.0f, 1.0f);
        std::uniform_real_distribution<float> distScale(params.minScale, params.maxScale);
        std::uniform_real_distribution<float> distRotation(0.0f, glm::two_pi<float>());

        // Compute minimum spacing: use collision radius if set, otherwise derive from density
        float minSpacing;
        if (params.collisionRadius > 0.0f)
        {
            minSpacing = params.collisionRadius * 2.0f; // Diameter as spacing
        }
        else
        {
            minSpacing = params.brushRadius / std::max(std::sqrt(params.density * brushArea), 1.0f);
            minSpacing = std::max(minSpacing, 0.5f);
        }
        float minSpacingSq = minSpacing * minSpacing;

        bool added = false;

        for (uint32_t i = 0; i < candidateCount; ++i)
        {
            // Generate random point within brush radius
            float angle = distAngle(rng);
            float r = std::sqrt(distRadius(rng)) * params.brushRadius;

            glm::vec2 offset(r * std::cos(angle), r * std::sin(angle));
            glm::vec2 candidatePos = params.brushCenter + offset;

            // Skip candidates outside this tile's bounds
            if (candidatePos.x < tileMinX || candidatePos.x >= tileMaxX ||
                candidatePos.y < tileMinZ || candidatePos.y >= tileMaxZ)
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

            // Apply falloff to acceptance probability
            float falloffValue = applyFalloff(dist, params.falloff);
            std::uniform_real_distribution<float> distAccept(0.0f, 1.0f);
            if (distAccept(rng) > falloffValue)
            {
                continue;
            }

            // Check minimum spacing against existing instances to avoid duplicates
            bool tooClose = false;
            for (const auto& existing : placement.getInstances())
            {
                float dx = existing.position.x - candidatePos.x;
                float dz = existing.position.z - candidatePos.y;
                if (dx * dx + dz * dz < minSpacingSq)
                {
                    tooClose = true;
                    break;
                }
            }
            if (tooClose) continue;

            VegetationInstance instance;
            instance.position = glm::vec3(candidatePos.x, 0.0f, candidatePos.y);
            instance.scale = distScale(rng);
            instance.speciesId = params.speciesId;

            // Apply random rotation based on randomRotation parameter
            if (params.randomRotation > 0.0f)
            {
                instance.rotation = distRotation(rng) * params.randomRotation;
            }
            else
            {
                instance.rotation = 0.0f;
            }

            placement.addInstance(instance);
            added = true;
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

    uint32_t VegetationPlacementBrushApplicator::positionHash(const glm::vec2& pos)
    {
        // Simple hash combining x and y coordinates for deterministic seeding
        uint32_t hx = static_cast<uint32_t>(std::hash<float>{}(pos.x));
        uint32_t hy = static_cast<uint32_t>(std::hash<float>{}(pos.y));
        return hx ^ (hy * 2654435761u);
    }
}
