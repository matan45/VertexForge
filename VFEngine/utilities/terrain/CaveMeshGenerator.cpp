#include "CaveMeshGenerator.hpp"
#include "../print/Log.hpp"
#include <meshoptimizer.h>
#include <algorithm>
#include <cmath>

namespace terrain
{
    // Shift isosurface slightly into solid terrain so cave mesh overlaps
    // the heightmap surface, sealing gaps at cave/terrain boundary.
    static constexpr float caveIsoOffset = -0.3f;

    static constexpr int cornerOffsets[8][3] = {
        {0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1},
        {0, 1, 0}, {1, 1, 0}, {1, 1, 1}, {0, 1, 1}
    };

    static constexpr int edgeCorners[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7}
    };

    bool CaveMeshGenerator::generate(TerrainTile& tile)
    {
        if (!tile.hasCaveData())
            return false;

        tile.caveLOD.clear();

        const auto& sdf = *tile.caveData;
        if (!sdf.hasCaveGeometry())
            return false;

        marchingCubes(sdf, tile.caveLOD.vertices, tile.caveLOD.indices);

        if (tile.caveLOD.vertices.empty() || tile.caveLOD.indices.empty())
            return false;

        glm::vec3 tileOriginOffset(tile.worldOrigin.x, 0.0f, tile.worldOrigin.z);
        for (auto& v : tile.caveLOD.vertices)
            v.position -= tileOriginOffset;

        meshopt_optimizeVertexCache(
            tile.caveLOD.indices.data(),
            tile.caveLOD.indices.data(),
            tile.caveLOD.indices.size(),
            tile.caveLOD.vertices.size());

        buildMeshlets(tile.caveLOD);
        computeBounds(tile.caveLOD);
        tile.caveLOD.mainMeshletCount = static_cast<uint32_t>(tile.caveLOD.meshlets.size());

        return true;
    }

    glm::vec3 CaveMeshGenerator::interpolateEdge(
        const glm::vec3& p0, float v0,
        const glm::vec3& p1, float v1)
    {
        if (std::abs(v0 - v1) < 1e-6f)
            return (p0 + p1) * 0.5f;

        float t = std::clamp((caveIsoOffset - v0) / (v1 - v0), 0.0f, 1.0f);
        return p0 + t * (p1 - p0);
    }

    CaveMeshGenerator::ModifiedBounds CaveMeshGenerator::computeModifiedBounds(
        const CaveSDFData& sdf)
    {
        ModifiedBounds mb;
        mb.endX = sdf.config.resX - 1;
        mb.endY = sdf.config.resY - 1;
        mb.endZ = sdf.config.resZ - 1;
        mb.valid = true;

        if (sdf.originalSdfGrid.empty())
            return mb;

        // Use tracked dirty region if available (set during brush application)
        // Expand by 3 voxels to ensure marching cubes covers the shifted isosurface boundary
        constexpr uint32_t boundsMargin = 3;
        if (sdf.hasDirtyRegion)
        {
            mb.startX = sdf.dirtyMin.x > boundsMargin ? sdf.dirtyMin.x - boundsMargin : 0;
            mb.startY = sdf.dirtyMin.y > boundsMargin ? sdf.dirtyMin.y - boundsMargin : 0;
            mb.startZ = sdf.dirtyMin.z > boundsMargin ? sdf.dirtyMin.z - boundsMargin : 0;
            mb.endX = std::min(sdf.dirtyMax.x + boundsMargin, sdf.config.resX - 1);
            mb.endY = std::min(sdf.dirtyMax.y + boundsMargin, sdf.config.resY - 1);
            mb.endZ = std::min(sdf.dirtyMax.z + boundsMargin, sdf.config.resZ - 1);
            return mb;
        }

        // Fall back to full scan (e.g., loaded from file)
        uint32_t mMinX = sdf.config.resX, mMinY = sdf.config.resY, mMinZ = sdf.config.resZ;
        uint32_t mMaxX = 0, mMaxY = 0, mMaxZ = 0;
        bool anyModified = false;

        for (uint32_t z = 0; z < sdf.config.resZ; ++z)
            for (uint32_t y = 0; y < sdf.config.resY; ++y)
                for (uint32_t x = 0; x < sdf.config.resX; ++x)
                {
                    size_t idx = sdf.getIndex(x, y, z);
                    if (std::abs(sdf.sdfGrid[idx] - sdf.originalSdfGrid[idx]) > 1e-4f)
                    {
                        mMinX = std::min(mMinX, x); mMinY = std::min(mMinY, y); mMinZ = std::min(mMinZ, z);
                        mMaxX = std::max(mMaxX, x); mMaxY = std::max(mMaxY, y); mMaxZ = std::max(mMaxZ, z);
                        anyModified = true;
                    }
                }

        if (!anyModified) { mb.valid = false; return mb; }

        mb.startX = mMinX > boundsMargin ? mMinX - boundsMargin : 0;
        mb.startY = mMinY > boundsMargin ? mMinY - boundsMargin : 0;
        mb.startZ = mMinZ > boundsMargin ? mMinZ - boundsMargin : 0;
        mb.endX = std::min(mMaxX + boundsMargin, sdf.config.resX - 1);
        mb.endY = std::min(mMaxY + boundsMargin, sdf.config.resY - 1);
        mb.endZ = std::min(mMaxZ + boundsMargin, sdf.config.resZ - 1);
        return mb;
    }

    uint64_t CaveMeshGenerator::getEdgeVertexKey(
        const CaveSDFData& sdf,
        uint32_t x, uint32_t y, uint32_t z, int edgeIdx)
    {
        uint32_t ex = x, ey = y, ez = z;
        int canonEdge = edgeIdx;

        switch (edgeIdx)
        {
        case 0:  ex = x;     ey = y;     ez = z;     canonEdge = 0; break;
        case 1:  ex = x + 1; ey = y;     ez = z;     canonEdge = 3; break;
        case 2:  ex = x;     ey = y;     ez = z + 1; canonEdge = 0; break;
        case 3:  ex = x;     ey = y;     ez = z;     canonEdge = 3; break;
        case 4:  ex = x;     ey = y + 1; ez = z;     canonEdge = 0; break;
        case 5:  ex = x + 1; ey = y + 1; ez = z;     canonEdge = 3; break;
        case 6:  ex = x;     ey = y + 1; ez = z + 1; canonEdge = 0; break;
        case 7:  ex = x;     ey = y + 1; ez = z;     canonEdge = 3; break;
        case 8:  ex = x;     ey = y;     ez = z;     canonEdge = 8; break;
        case 9:  ex = x + 1; ey = y;     ez = z;     canonEdge = 8; break;
        case 10: ex = x + 1; ey = y;     ez = z + 1; canonEdge = 8; break;
        case 11: ex = x;     ey = y;     ez = z + 1; canonEdge = 8; break;
        }

        uint64_t flatIdx = static_cast<uint64_t>(ez) * sdf.config.resX * sdf.config.resY +
                           static_cast<uint64_t>(ey) * sdf.config.resX +
                           static_cast<uint64_t>(ex);
        return flatIdx * 12 + static_cast<uint64_t>(canonEdge);
    }

    uint32_t CaveMeshGenerator::getOrCreateEdgeVertex(
        const CaveSDFData& sdf,
        std::unordered_map<uint64_t, uint32_t>& edgeVertexCache,
        const glm::vec3 cornerPositions[8],
        const float cornerValues[8],
        const int edgeCornersArr[12][2],
        uint32_t x, uint32_t y, uint32_t z, int edgeIdx,
        std::vector<resource::Vertex>& vertices)
    {
        uint64_t edgeKey = getEdgeVertexKey(sdf, x, y, z, edgeIdx);
        auto it = edgeVertexCache.find(edgeKey);
        if (it != edgeVertexCache.end())
            return it->second;

        int c0 = edgeCornersArr[edgeIdx][0];
        int c1 = edgeCornersArr[edgeIdx][1];

        glm::vec3 pos = interpolateEdge(
            cornerPositions[c0], cornerValues[c0],
            cornerPositions[c1], cornerValues[c1]);

        glm::vec3 normal = sdf.computeGradient(
            static_cast<uint32_t>(std::clamp(static_cast<int>(pos.x - sdf.localOrigin.x) / std::max(sdf.config.voxelSize, 1e-6f), 0.0f, static_cast<float>(sdf.config.resX - 1))),
            static_cast<uint32_t>(std::clamp(static_cast<int>((pos.y - sdf.localOrigin.y) / std::max(sdf.config.yVoxelSize, 1e-6f)), 0, static_cast<int>(sdf.config.resY - 1))),
            static_cast<uint32_t>(std::clamp(static_cast<int>((pos.z - sdf.localOrigin.z) / std::max(sdf.config.voxelSize, 1e-6f)), 0, static_cast<int>(sdf.config.resZ - 1))));
        normal = -normal;

        resource::Vertex vert{};
        vert.position = pos;
        vert.normal = normal;
        vert.texCoords = glm::vec2(
            (pos.x - sdf.localOrigin.x) / (sdf.config.voxelSize * static_cast<float>(sdf.config.resX - 1)),
            (pos.z - sdf.localOrigin.z) / (sdf.config.voxelSize * static_cast<float>(sdf.config.resZ - 1)));

        uint32_t idx = static_cast<uint32_t>(vertices.size());
        vertices.push_back(vert);
        edgeVertexCache[edgeKey] = idx;
        return idx;
    }

    void CaveMeshGenerator::marchingCubes(
        const CaveSDFData& sdf,
        std::vector<resource::Vertex>& vertices,
        std::vector<uint32_t>& indices)
    {
        vertices.clear();
        indices.clear();

        ModifiedBounds mb = computeModifiedBounds(sdf);
        if (!mb.valid)
            return;

        std::unordered_map<uint64_t, uint32_t> edgeVertexCache;

        for (uint32_t z = mb.startZ; z < mb.endZ; ++z)
          for (uint32_t y = mb.startY; y < mb.endY; ++y)
            for (uint32_t x = mb.startX; x < mb.endX; ++x)
            {
                float cornerValues[8];
                glm::vec3 cornerPositions[8];
                for (int i = 0; i < 8; ++i)
                {
                    cornerValues[i] = sdf.getSDF(x + cornerOffsets[i][0], y + cornerOffsets[i][1], z + cornerOffsets[i][2]);
                    cornerPositions[i] = sdf.getWorldPosition(x + cornerOffsets[i][0], y + cornerOffsets[i][1], z + cornerOffsets[i][2]);
                }

                int cubeIndex = 0;
                for (int i = 0; i < 8; ++i)
                    if (cornerValues[i] > caveIsoOffset) cubeIndex |= (1 << i);

                if (edgeTable[cubeIndex] == 0)
                    continue;

                uint32_t ev[12] = {};
                for (int e = 0; e < 12; ++e)
                    if (edgeTable[cubeIndex] & (1 << e))
                        ev[e] = getOrCreateEdgeVertex(sdf, edgeVertexCache, cornerPositions,
                            cornerValues, edgeCorners, x, y, z, e, vertices);

                for (int i = 0; triTable[cubeIndex][i] != -1; i += 3)
                {
                    uint32_t a = ev[triTable[cubeIndex][i]];
                    uint32_t b = ev[triTable[cubeIndex][i + 1]];
                    uint32_t c = ev[triTable[cubeIndex][i + 2]];
                    indices.push_back(a); indices.push_back(c); indices.push_back(b);
                }
            }
    }

    static void packMeshletPrimitives(
        TileLODData& lodData,
        const std::vector<meshopt_Meshlet>& meshoptMeshlets,
        const std::vector<unsigned int>& meshletVertexIndices,
        const std::vector<unsigned char>& meshletTriangleIndices,
        size_t meshletCount)
    {
        lodData.meshlets.resize(meshletCount);
        lodData.meshletPrimitives.reserve(meshletCount * resource::MAX_MESHLET_PRIMITIVES);

        uint32_t primitiveOffset = 0;
        for (size_t i = 0; i < meshletCount; ++i)
        {
            const auto& m = meshoptMeshlets[i];
            auto& outMeshlet = lodData.meshlets[i];

            for (unsigned int t = 0; t < m.triangle_count; ++t)
            {
                size_t triOffset = m.triangle_offset + t * 3;
                if (triOffset + 2 >= meshletTriangleIndices.size())
                    break;

                uint32_t packed =
                    static_cast<uint32_t>(meshletTriangleIndices[triOffset]) |
                    (static_cast<uint32_t>(meshletTriangleIndices[triOffset + 1]) << 8) |
                    (static_cast<uint32_t>(meshletTriangleIndices[triOffset + 2]) << 16);
                lodData.meshletPrimitives.push_back(packed);
            }

            outMeshlet.descriptor.vertexOffset = m.vertex_offset;
            outMeshlet.descriptor.primitiveOffset = primitiveOffset;
            outMeshlet.descriptor.vertexCount = static_cast<uint8_t>(m.vertex_count);
            outMeshlet.descriptor.primitiveCount = static_cast<uint8_t>(m.triangle_count);
            outMeshlet.descriptor.padding = 0;
            primitiveOffset += m.triangle_count;

            meshopt_Bounds bounds = meshopt_computeMeshletBounds(
                &meshletVertexIndices[m.vertex_offset],
                &meshletTriangleIndices[m.triangle_offset],
                m.triangle_count,
                reinterpret_cast<const float*>(lodData.vertices.data()),
                lodData.vertices.size(),
                sizeof(resource::Vertex));

            outMeshlet.bounds.boundingSphere = glm::vec4(
                bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius);
            // Disable backface culling for cave meshlets — cave geometry is
            // viewed from inside, so normal cone culling would incorrectly hide faces.
            // cone.w >= 1.0 makes coneCullTest() always return true (visible).
            outMeshlet.bounds.cone = glm::vec4(
                bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2],
                1.0f);
        }
    }

    void CaveMeshGenerator::buildMeshlets(TileLODData& lodData)
    {
        lodData.meshlets.clear();
        lodData.meshletVertices.clear();
        lodData.meshletPrimitives.clear();

        if (lodData.indices.empty() || lodData.vertices.empty())
            return;

        const size_t maxMeshlets = meshopt_buildMeshletsBound(
            lodData.indices.size(),
            resource::MAX_MESHLET_VERTICES,
            resource::MAX_MESHLET_PRIMITIVES);

        std::vector<meshopt_Meshlet> meshoptMeshlets(maxMeshlets);
        std::vector<unsigned int> meshletVertexIndices(maxMeshlets * resource::MAX_MESHLET_VERTICES);
        std::vector<unsigned char> meshletTriangleIndices(maxMeshlets * resource::MAX_MESHLET_PRIMITIVES * 3);

        size_t meshletCount = meshopt_buildMeshlets(
            meshoptMeshlets.data(),
            meshletVertexIndices.data(),
            meshletTriangleIndices.data(),
            lodData.indices.data(),
            lodData.indices.size(),
            reinterpret_cast<const float*>(lodData.vertices.data()),
            lodData.vertices.size(),
            sizeof(resource::Vertex),
            resource::MAX_MESHLET_VERTICES,
            resource::MAX_MESHLET_PRIMITIVES,
            0.5f);

        if (meshletCount == 0)
            return;

        const auto& lastMeshlet = meshoptMeshlets[meshletCount - 1];
        size_t totalVertexIndices = lastMeshlet.vertex_offset + lastMeshlet.vertex_count;
        size_t totalTriangleIndices = lastMeshlet.triangle_offset +
            ((lastMeshlet.triangle_count * 3 + 3) & ~3);

        meshoptMeshlets.resize(meshletCount);
        meshletVertexIndices.resize(totalVertexIndices);
        meshletTriangleIndices.resize(totalTriangleIndices);

        lodData.meshletVertices.resize(totalVertexIndices);
        for (size_t i = 0; i < totalVertexIndices; ++i)
            lodData.meshletVertices[i] = meshletVertexIndices[i];

        packMeshletPrimitives(lodData, meshoptMeshlets, meshletVertexIndices,
                              meshletTriangleIndices, meshletCount);
    }

    void CaveMeshGenerator::computeBounds(TileLODData& lodData)
    {
        if (lodData.vertices.empty())
            return;

        glm::vec3 minPos(std::numeric_limits<float>::max());
        glm::vec3 maxPos(std::numeric_limits<float>::lowest());

        for (const auto& v : lodData.vertices)
        {
            minPos = glm::min(minPos, v.position);
            maxPos = glm::max(maxPos, v.position);
        }

        lodData.aabb = math::AABB(minPos, maxPos);

        glm::vec3 center = (minPos + maxPos) * 0.5f;
        float radius = 0.0f;
        for (const auto& v : lodData.vertices)
            radius = std::max(radius, glm::length(v.position - center));

        lodData.boundingSphere = glm::vec4(center, radius);
    }

#include "CaveMeshTables.inl"

} // namespace terrain
