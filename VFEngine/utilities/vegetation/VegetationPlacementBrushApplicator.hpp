#pragma once

#include "VegetationPlacementData.hpp"
#include "VegetationTypes.hpp"
#include <glm/glm.hpp>

namespace vegetation
{
    class VegetationPlacementBrushApplicator
    {
    public:
        struct SpreadParams
        {
            glm::vec2 brushCenter;       // World XZ position of brush center
            glm::vec2 tileWorldOrigin;   // World XZ origin of this tile
            float brushRadius;
            float density;               // Controls grid spacing (instances per unit area)
            float strength;              // Placement probability multiplier
            float opacity;               // Overall influence [0,1]
            float deltaTime;             // Frame delta for continuous painting
            float minScale;
            float maxScale;
            float randomRotation;        // [0,1] - amount of random Y rotation
            uint32_t speciesId;
            terrain::BrushFalloff falloff;
            terrain::BrushShape shape;
            float tileWorldSize;
            float collisionRadius = 0.0f; // If > 0, minimum spacing between instances
        };

        struct EraseParams
        {
            glm::vec3 brushCenter3D;     // 3D world position
            float brushRadius;
        };

        // Spread vegetation instances within brush radius using grid-based placement.
        // Deterministic per grid cell — repainting the same area won't re-randomize existing instances.
        // Returns true if any instances were added.
        static bool spread(VegetationPlacementData& placement, const SpreadParams& params);

        // Erase vegetation instances within brush radius. Returns true if any instances were removed.
        static bool erase(VegetationPlacementData& placement, const EraseParams& params);

    private:
        // Compute normalized distance [0,1] from brush center.
        static float computeNormalizedDistance(
            const glm::vec2& pos,
            const glm::vec2& brushCenter,
            float brushRadius,
            terrain::BrushShape shape);

        // Apply falloff curve.
        static float applyFalloff(float t, terrain::BrushFalloff falloff);

        // Generate a deterministic hash from grid cell coordinates
        static uint32_t cellHash(int cellX, int cellZ, uint32_t seed = 0);

        // Generate a float [0,1] from a hash value
        static float hashToFloat(uint32_t h);
    };
}
