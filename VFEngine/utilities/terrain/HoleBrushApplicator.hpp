#pragma once

#include "HoleBrushTypes.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace terrain
{
    class HoleBrushApplicator
    {
    public:
        struct ApplyParams
        {
            glm::vec2 brushCenter;       // World XZ position of brush center
            glm::vec2 tileWorldOrigin;   // World XZ origin of this tile
            float brushRadius;
            float vertexSpacing;
            uint32_t verticesPerSide;
            BrushFalloff falloff;
            BrushShape shape;
            bool erase;                  // false = create holes, true = fill holes
        };

        // Apply hole brush to a single tile's holeMask. Returns true if any vertices were modified.
        static bool apply(std::vector<uint8_t>& holeMask, const ApplyParams& params);

    private:
        static float computeNormalizedDistance(
            const glm::vec2& vertexWorldPos,
            const glm::vec2& brushCenter,
            float brushRadius,
            BrushShape shape);

        static float applyFalloff(float t, BrushFalloff falloff);
    };
}
