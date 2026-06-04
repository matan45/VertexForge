#pragma once
#include "TerrainExport.hpp"

#include "CaveSDFData.hpp"
#include "TerrainTile.hpp"
#include <vector>
#include <unordered_map>

namespace terrain
{
    class VF_TERRAIN_API CaveMeshGenerator
    {
    public:
        static bool generate(TerrainTile& tile);

    private:
        static void marchingCubes(
            const CaveSDFData& sdf,
            std::vector<resource::Vertex>& vertices,
            std::vector<uint32_t>& indices);

        struct ModifiedBounds
        {
            uint32_t startX = 0, startY = 0, startZ = 0;
            uint32_t endX = 0, endY = 0, endZ = 0;
            bool valid = false;
        };

        static ModifiedBounds computeModifiedBounds(const CaveSDFData& sdf);

        static uint64_t getEdgeVertexKey(
            const CaveSDFData& sdf,
            uint32_t x, uint32_t y, uint32_t z, int edgeIdx);

        static uint32_t getOrCreateEdgeVertex(
            const CaveSDFData& sdf,
            std::unordered_map<uint64_t, uint32_t>& edgeVertexCache,
            const glm::vec3 cornerPositions[8],
            const float cornerValues[8],
            const int edgeCorners[12][2],
            uint32_t x, uint32_t y, uint32_t z, int edgeIdx,
            std::vector<resource::Vertex>& vertices);

        static glm::vec3 interpolateEdge(
            const glm::vec3& p0, float v0,
            const glm::vec3& p1, float v1);

        static void buildMeshlets(TileLODData& lodData);
        static void computeBounds(TileLODData& lodData);

        static const int edgeTable[256];
        static const int triTable[256][16];
    };

} // namespace terrain
