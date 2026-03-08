#pragma once

#include "VegetationPlacementData.hpp"
#include "VegetationTypes.hpp"
#include <glm/glm.hpp>

namespace vegetation
{
    class VegetationPlacementBrushApplicator
    {
    public:
        struct ScatterParams
        {
            glm::vec2 brushCenter;       // World XZ position of brush center
            glm::vec2 tileWorldOrigin;   // World XZ origin of this tile
            float brushRadius;
            float density;               // Instances per unit area
            float minScale;
            float maxScale;
            float randomRotation;        // [0,1] - amount of random Y rotation
            uint32_t speciesId;
            terrain::BrushFalloff falloff;
            terrain::BrushShape shape;
            float tileWorldSize;
        };

        struct EraseParams
        {
            glm::vec3 brushCenter3D;     // 3D world position
            float brushRadius;
        };

        // Scatter vegetation instances within brush radius. Returns true if any instances were added.
        static bool scatter(VegetationPlacementData& placement, const ScatterParams& params);

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

        // Generate a deterministic seed from position
        static uint32_t positionHash(const glm::vec2& pos);
    };
}
