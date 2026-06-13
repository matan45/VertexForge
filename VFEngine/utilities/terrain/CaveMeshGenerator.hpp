#pragma once
#include "TerrainExport.hpp"

#include "CaveSDFData.hpp"
#include "TerrainTile.hpp"
#include <vector>

namespace terrain
{
    // SDF grids of the +X / +Z (and the +X+Z diagonal) neighbour tiles, used as a
    // one-cell "apron" so the mesher can extend its surface into the neighbour and
    // produce geometry that coincides with the neighbour's own mesh at the shared
    // boundary — i.e. crack-free seams. All optional (null when the neighbour isn't
    // resident); the mesher falls back to clamping at the tile edge.
    struct NeighborCaves
    {
        const CaveSDFData* plusX = nullptr;
        const CaveSDFData* plusZ = nullptr;
        const CaveSDFData* plusXZ = nullptr;
    };

    class VF_TERRAIN_API CaveMeshGenerator
    {
    public:
        static bool generate(TerrainTile& tile, const NeighborCaves& neighbors);
        static bool generate(TerrainTile& tile);

    private:
        struct ModifiedBounds
        {
            uint32_t startX = 0, startY = 0, startZ = 0;
            uint32_t endX = 0, endY = 0, endZ = 0;
            bool valid = false;
        };

        static ModifiedBounds computeModifiedBounds(const CaveSDFData& sdf);

        // Naive Surface Nets isosurface extraction. One vertex per surface-straddling
        // cell (at the centroid of its edge zero-crossings); one quad per crossed
        // minimal edge. Far smoother than Marching Cubes for organic caves and the
        // boundary vertices are a pure function of the corner SDF values (needed for
        // crack-free cross-tile stitching via the neighbour apron).
        static void surfaceNets(
            const CaveSDFData& sdf,
            const NeighborCaves& neighbors,
            std::vector<resource::Vertex>& vertices,
            std::vector<uint32_t>& indices);

        static glm::vec3 interpolateEdge(
            const glm::vec3& p0, float v0,
            const glm::vec3& p1, float v1);

        static void buildMeshlets(TileLODData& lodData);
        static void computeBounds(TileLODData& lodData);
    };

} // namespace terrain
