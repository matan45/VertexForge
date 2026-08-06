#pragma once
#include "TerrainExport.hpp"

#include "BrushTypes.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace terrain
{
    // CPU height brush. The third member of the applicator family alongside WeightBrushApplicator
    // and HoleBrushApplicator, and the only one the editor does not use: sculpting runs on the GPU
    // (ITerrainBrushComputeProvider), which exists solely in EditorBootstrap. Scripts need a path
    // that also works in the Runtime and in the CPU-only test binary, so this mirrors the shader's
    // maths -- same world-space sampling, same distance metric, same exclusive rim, same
    // clamp-last -- rather than sharing its code.
    class VF_TERRAIN_API HeightBrushApplicator
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
            HeightEditMode mode;
            float amount;                // World-Y metres: delta for Add, target height for Set
            float minHeight;             // Tile config bounds; applied last, as the shader does
            float maxHeight;
        };

        // Apply the brush to a single tile's height plane. Returns true if any vertex changed.
        static bool apply(std::vector<float>& heightData, const ApplyParams& params);

    private:
        // Normalized distance [0,1) from brush center; >= 1.0 means outside the brush.
        static float computeNormalizedDistance(
            const glm::vec2& sampleWorldPos,
            const glm::vec2& brushCenter,
            float brushRadius,
            BrushShape shape);

        // Falloff curve matching the terrain brush shaders
        // (resources/shaders/terrain/brush_compute.glsl, brush_influence.glsl).
        static float applyFalloff(float t, BrushFalloff falloff);
    };
}
