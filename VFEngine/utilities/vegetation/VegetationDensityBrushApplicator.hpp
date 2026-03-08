#pragma once

#include "VegetationDensityMap.hpp"
#include "VegetationTypes.hpp"
#include <glm/glm.hpp>

namespace vegetation
{
    class VegetationDensityBrushApplicator
    {
    public:
        struct ApplyParams
        {
            glm::vec2 brushCenter;       // World XZ position of brush center
            glm::vec2 tileWorldOrigin;   // World XZ origin of this tile
            float brushRadius;
            float brushStrength;
            float brushOpacity;
            float vertexSpacing;
            uint32_t verticesPerSide;
            terrain::BrushFalloff falloff;
            terrain::BrushShape shape;
            DensityBrushType brushType;
            float deltaTime;
            bool invert;
        };

        // Apply brush to a single tile's density map. Returns true if any texels were modified.
        static bool apply(VegetationDensityMap& densityMap, const ApplyParams& params);

    private:
        // Compute normalized distance [0,1] from brush center.
        // Returns >= 1.0 if outside brush radius.
        static float computeNormalizedDistance(
            const glm::vec2& texelWorldPos,
            const glm::vec2& brushCenter,
            float brushRadius,
            terrain::BrushShape shape);

        // Apply falloff curve matching GPU shader.
        // Input t is normalized distance [0,1]. Returns influence [0,1].
        static float applyFalloff(float t, terrain::BrushFalloff falloff);

        static void paintDensity(VegetationDensityMap& dm, uint32_t x, uint32_t z,
                                 float influence);
        static void eraseDensity(VegetationDensityMap& dm, uint32_t x, uint32_t z,
                                 float influence);
        static void smoothDensity(VegetationDensityMap& dm, uint32_t x, uint32_t z,
                                  float influence);
        static void fillDensity(VegetationDensityMap& dm, uint32_t x, uint32_t z,
                                float influence);
    };
}
