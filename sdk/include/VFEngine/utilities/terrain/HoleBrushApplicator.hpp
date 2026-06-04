#pragma once
#include "TerrainExport.hpp"

#include "HoleBrushTypes.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace terrain
{
    class VF_TERRAIN_API HoleBrushApplicator
    {
    public:
        struct ApplyParams
        {
            glm::vec2 brushCenter;       // World XZ position of brush center
            glm::vec2 tileWorldOrigin;   // World XZ origin of this tile
            float brushRadius;
            float vertexSpacing;
            uint32_t quadsPerSide;       // vertexCount - 1
            BrushFalloff falloff;
            BrushShape shape;
            bool erase;                  // false = create holes, true = fill holes
        };

        // Apply hole brush to a single tile's per-quad holeMask. Returns true if any quads were modified.
        static bool apply(std::vector<uint8_t>& holeMask, const ApplyParams& params);

    private:
        static float computeNormalizedDistance(
            const glm::vec2& sampleWorldPos,
            const glm::vec2& brushCenter,
            float brushRadius,
            BrushShape shape);

        static float applyFalloff(float t, BrushFalloff falloff);
    };
}
