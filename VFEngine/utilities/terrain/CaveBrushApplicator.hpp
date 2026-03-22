#pragma once

#include "CaveBrushTypes.hpp"
#include "CaveSDFData.hpp"
#include <glm/glm.hpp>

namespace terrain
{
    class CaveBrushApplicator
    {
    public:
        struct ApplyParams
        {
            glm::vec3 brushCenter;       // World position of brush center (3D)
            glm::vec2 tileWorldOrigin;   // World XZ origin of this tile
            float brushRadius;
            float brushStrength;
            CaveBrushType brushType;
            BrushFalloff falloff;
            BrushShape shape;
            float deltaTime;
            bool invert;                 // Reverse operation (carve becomes fill, fill becomes carve)
        };

        // Apply cave brush to a single tile's SDF data. Returns true if any voxels were modified.
        static bool apply(CaveSDFData& sdf, const ApplyParams& params);

    private:
        static void applyCarve(CaveSDFData& sdf, const ApplyParams& params);
        static void applyFill(CaveSDFData& sdf, const ApplyParams& params);
        static void applySmooth(CaveSDFData& sdf, const ApplyParams& params);

        // Compute 3D normalized distance from voxel world position to brush center
        static float computeNormalizedDistance3D(
            const glm::vec3& worldPos,
            const glm::vec3& brushCenter,
            float brushRadius,
            BrushShape shape);

        // Must match other brush applicators
        static float applyFalloff(float t, BrushFalloff falloff);
    };
}
