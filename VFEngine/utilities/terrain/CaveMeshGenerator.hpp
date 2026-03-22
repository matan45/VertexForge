#pragma once

#include "CaveSDFData.hpp"
#include "TerrainTile.hpp"
#include <vector>

namespace terrain
{
    class CaveMeshGenerator
    {
    public:
        // Generate cave mesh from SDF data using Marching Cubes
        // Populates tile.caveLOD with vertices, indices, and meshlets
        static bool generate(TerrainTile& tile);

    private:
        // Marching Cubes isosurface extraction
        static void marchingCubes(
            const CaveSDFData& sdf,
            std::vector<resource::Vertex>& vertices,
            std::vector<uint32_t>& indices);

        // Interpolate vertex position along an edge between two voxels
        static glm::vec3 interpolateEdge(
            const glm::vec3& p0, float v0,
            const glm::vec3& p1, float v1);

        // Build meshlets from vertices/indices using meshoptimizer
        static void buildMeshlets(TileLODData& lodData);

        // Compute bounds for the LOD data
        static void computeBounds(TileLODData& lodData);

        // Marching Cubes edge table (256 entries)
        static const int edgeTable[256];

        // Marching Cubes triangle table (256 x 16)
        static const int triTable[256][16];
    };

} // namespace terrain
