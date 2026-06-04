#pragma once
#include "TerrainExport.hpp"

#include "CaveBrushTypes.hpp"
#include "CaveSDFData.hpp"
#include <glm/glm.hpp>

namespace terrain
{
    class VF_TERRAIN_API CaveBrushApplicator
    {
    public:
        struct ApplyParams
        {
            glm::vec3 brushCenter;       // World position of brush center (3D)
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

        // Iterates voxels within brush bounds, computes distance/falloff, optionally checks
        // originalSdfGrid for solid, and calls update(sdf, x, y, z, influence) for each affected voxel.
        template<typename UpdateFn>
        static void forEachBrushVoxel(CaveSDFData& sdf, const ApplyParams& params,
                                      float strength, bool checkOriginalSolid, UpdateFn&& update);

        static float computeNormalizedDistance3D(
            const glm::vec3& worldPos,
            const glm::vec3& brushCenter,
            float brushRadius,
            BrushShape shape);

        static float applyFalloff(float t, BrushFalloff falloff);
    };
}
