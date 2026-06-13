#include "CaveMeshGenerator.hpp"
#include "../print/Log.hpp"
#include <meshoptimizer.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace terrain
{
    // Iso level the cave surface is extracted at. SDF convention: negative = solid,
    // positive = air, true surface at 0 — so the cave shell meets the heightmap exactly
    // at the surface (no overlap-into-solid hack). The cave mesh only covers CARVED
    // cells (see isCarvedCell), so it never duplicates / z-fights the pristine heightmap
    // surface; the heightmap mesh renders the rest, with holes punched where the cave
    // breaches it (punchCaveHolesForTile).
    static constexpr float caveIsoLevel = 0.0f;

    // Corner layout of a marching cell (matches getWorldPosition stepping).
    static constexpr int cornerOffsets[8][3] = {
        {0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1},
        {0, 1, 0}, {1, 1, 0}, {1, 1, 1}, {0, 1, 1}
    };

    static constexpr int edgeCorners[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7}
    };

    static bool isSolid(float value)
    {
        return value < caveIsoLevel;
    }

    bool CaveMeshGenerator::generate(TerrainTile& tile)
    {
        return generate(tile, NeighborCaves{});
    }

    bool CaveMeshGenerator::generate(TerrainTile& tile, const NeighborCaves& neighbors)
    {
        if (!tile.hasCaveData())
            return false;

        tile.caveLOD.clear();

        const auto& sdf = *tile.caveData;
        if (!sdf.hasCaveGeometry())
            return false;

        surfaceNets(sdf, neighbors, tile.caveLOD.vertices, tile.caveLOD.indices);

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
        float denom = v1 - v0;
        if (std::abs(denom) < 1e-6f)
            return (p0 + p1) * 0.5f;

        float t = std::clamp((caveIsoLevel - v0) / denom, 0.0f, 1.0f);
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

        // Use tracked dirty region if available (set during brush application).
        // Expand by a halo so the quad stitching across the dirty edge has both cells
        // present and so the surface never reaches the box's outer margin.
        constexpr uint32_t boundsMargin = 4;
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

        // Fall back to full scan (e.g., loaded from file).
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

    // Emit a triangle, orienting its winding so the front face (CCW under the terrain
    // pipeline's eBack/eCounterClockwise convention) points toward air — i.e. toward a
    // viewer standing inside the cave. Degenerate triangles are dropped (also keeps the
    // Jolt cave MeshShape clean).
    static void emitTriangle(
        const std::vector<resource::Vertex>& verts,
        std::vector<uint32_t>& indices,
        uint32_t a, uint32_t b, uint32_t c)
    {
        const glm::vec3& pa = verts[a].position;
        const glm::vec3& pb = verts[b].position;
        const glm::vec3& pc = verts[c].position;

        glm::vec3 geo = glm::cross(pb - pa, pc - pa);
        if (glm::dot(geo, geo) < 1e-12f)
            return; // degenerate / zero-area

        glm::vec3 airNormal = verts[a].normal + verts[b].normal + verts[c].normal;
        if (glm::dot(geo, airNormal) >= 0.0f)
        {
            indices.push_back(a); indices.push_back(b); indices.push_back(c);
        }
        else
        {
            indices.push_back(a); indices.push_back(c); indices.push_back(b);
        }
    }

    static void emitQuad(
        const std::vector<resource::Vertex>& verts,
        std::vector<uint32_t>& indices,
        uint32_t q0, uint32_t q1, uint32_t q2, uint32_t q3)
    {
        emitTriangle(verts, indices, q0, q1, q2);
        emitTriangle(verts, indices, q0, q2, q3);
    }

    void CaveMeshGenerator::surfaceNets(
        const CaveSDFData& sdf,
        const NeighborCaves& neighbors,
        std::vector<resource::Vertex>& vertices,
        std::vector<uint32_t>& indices)
    {
        vertices.clear();
        indices.clear();

        ModifiedBounds mb = computeModifiedBounds(sdf);
        if (!mb.valid)
            return;

        if (mb.endX <= mb.startX || mb.endY <= mb.startY || mb.endZ <= mb.startZ)
            return;

        const int resX = static_cast<int>(sdf.config.resX);
        const int resY = static_cast<int>(sdf.config.resY);
        const int resZ = static_cast<int>(sdf.config.resZ);

        // Ghost-aware SDF sample. Coordinates at or beyond resX/resZ read from the
        // neighbour tile's apron columns (neighbour col = coord - (res-1), since adjacent
        // tiles share a duplicated boundary column). Y has no vertical neighbour, and any
        // axis with no resident neighbour clamps at the edge — exactly the legacy single-
        // tile behaviour. This makes a tile's boundary geometry a pure function of the
        // (welded) shared SDF, so adjacent tiles mesh the seam identically.
        auto sampleSDF = [&](int cx, int cy, int cz) -> float
        {
            int yy = std::clamp(cy, 0, resY - 1);
            bool gx = cx >= resX;
            bool gz = cz >= resZ;
            if (gx && gz && neighbors.plusXZ)
            {
                int nx = cx - (resX - 1);
                int nz = cz - (resZ - 1);
                if (nx >= 0 && nx < resX && nz >= 0 && nz < resZ)
                    return neighbors.plusXZ->getSDF((uint32_t)nx, (uint32_t)yy, (uint32_t)nz);
            }
            else if (gx && !gz && neighbors.plusX)
            {
                int nx = cx - (resX - 1);
                int zz = std::clamp(cz, 0, resZ - 1);
                if (nx >= 0 && nx < resX)
                    return neighbors.plusX->getSDF((uint32_t)nx, (uint32_t)yy, (uint32_t)zz);
            }
            else if (gz && !gx && neighbors.plusZ)
            {
                int nz = cz - (resZ - 1);
                int xx = std::clamp(cx, 0, resX - 1);
                if (nz >= 0 && nz < resZ)
                    return neighbors.plusZ->getSDF((uint32_t)xx, (uint32_t)yy, (uint32_t)nz);
            }
            int xx = std::clamp(cx, 0, resX - 1);
            int zz = std::clamp(cz, 0, resZ - 1);
            return sdf.getSDF((uint32_t)xx, (uint32_t)yy, (uint32_t)zz);
        };

        // Ghost-aware central-difference gradient (points toward increasing SDF = air).
        auto gradAt = [&](int cx, int cy, int cz) -> glm::vec3
        {
            glm::vec3 g(
                sampleSDF(cx + 1, cy, cz) - sampleSDF(cx - 1, cy, cz),
                sampleSDF(cx, cy + 1, cz) - sampleSDF(cx, cy - 1, cz),
                sampleSDF(cx, cy, cz + 1) - sampleSDF(cx, cy, cz - 1));
            float len = glm::length(g);
            return len > 1e-6f ? g / len : glm::vec3(0.0f, 1.0f, 0.0f);
        };

        // Extend the cell range by one layer into a resident +X / +Z neighbour (the
        // "apron"): the extra boundary cells produce geometry coincident with the
        // neighbour's own mesh, closing the seam. The far corner of an apron cell is read
        // via the ghost sampler. No extension on the low side or in Y.
        const bool extendX = neighbors.plusX != nullptr && mb.endX >= sdf.config.resX - 1;
        const bool extendZ = neighbors.plusZ != nullptr && mb.endZ >= sdf.config.resZ - 1;
        const uint32_t cellHiX = extendX ? sdf.config.resX : mb.endX;
        const uint32_t cellHiY = mb.endY;
        const uint32_t cellHiZ = extendZ ? sdf.config.resZ : mb.endZ;

        const uint32_t boxX = cellHiX - mb.startX;
        const uint32_t boxY = cellHiY - mb.startY;
        const uint32_t boxZ = cellHiZ - mb.startZ;

        // Per-cell vertex index over the box (-1 = cell has no surface vertex).
        std::vector<int32_t> cellVertex(static_cast<size_t>(boxX) * boxY * boxZ, -1);

        auto cacheIndex = [&](uint32_t x, uint32_t y, uint32_t z) -> size_t
        {
            return (static_cast<size_t>(z - mb.startZ) * boxY + (y - mb.startY)) * boxX + (x - mb.startX);
        };

        // A cell is "carved" if any of its corners differs from the original heightmap
        // SDF. Pristine cells (the untouched terrain surface) are left to the heightmap
        // mesh, so the cave mesh contains only carved geometry and never z-fights the
        // surface. Apron/ghost corners are treated as carved (the seam is where a carve
        // crossed the boundary). With no original (procedural all-solid init) everything
        // is meshed.
        auto isCarvedCell = [&](uint32_t x, uint32_t y, uint32_t z) -> bool
        {
            if (sdf.originalSdfGrid.empty())
                return true;
            for (int i = 0; i < 8; ++i)
            {
                int cx = static_cast<int>(x) + cornerOffsets[i][0];
                int cy = static_cast<int>(y) + cornerOffsets[i][1];
                int cz = static_cast<int>(z) + cornerOffsets[i][2];
                if (cx >= resX || cz >= resZ)
                    return true; // apron/ghost corner
                size_t idx = sdf.getIndex((uint32_t)cx, (uint32_t)cy, (uint32_t)cz);
                if (std::abs(sdf.sdfGrid[idx] - sdf.originalSdfGrid[idx]) > 1e-4f)
                    return true;
            }
            return false;
        };

        // --- Pass 1: one vertex per surface-straddling, carved cell ---
        for (uint32_t z = mb.startZ; z < cellHiZ; ++z)
          for (uint32_t y = mb.startY; y < cellHiY; ++y)
            for (uint32_t x = mb.startX; x < cellHiX; ++x)
            {
                float cornerValues[8];
                glm::vec3 cornerPositions[8];
                int solidMask = 0;
                for (int i = 0; i < 8; ++i)
                {
                    int cx = static_cast<int>(x) + cornerOffsets[i][0];
                    int cy = static_cast<int>(y) + cornerOffsets[i][1];
                    int cz = static_cast<int>(z) + cornerOffsets[i][2];
                    cornerValues[i] = sampleSDF(cx, cy, cz);
                    cornerPositions[i] = sdf.getWorldPosition(
                        static_cast<uint32_t>(cx), static_cast<uint32_t>(cy), static_cast<uint32_t>(cz));
                    if (isSolid(cornerValues[i]))
                        solidMask |= (1 << i);
                }

                if (solidMask == 0 || solidMask == 0xFF)
                    continue; // fully air or fully solid: no surface here

                if (!isCarvedCell(x, y, z))
                    continue; // pristine heightmap surface: rendered by the terrain mesh

                glm::vec3 sum(0.0f);
                int crossings = 0;
                for (int e = 0; e < 12; ++e)
                {
                    int c0 = edgeCorners[e][0];
                    int c1 = edgeCorners[e][1];
                    if (isSolid(cornerValues[c0]) == isSolid(cornerValues[c1]))
                        continue;
                    sum += interpolateEdge(cornerPositions[c0], cornerValues[c0],
                                           cornerPositions[c1], cornerValues[c1]);
                    ++crossings;
                }
                if (crossings == 0)
                    continue;

                glm::vec3 pos = sum / static_cast<float>(crossings);

                // Smooth normal toward air: average the 8 corner gradients.
                glm::vec3 grad(0.0f);
                for (int i = 0; i < 8; ++i)
                    grad += gradAt(static_cast<int>(x) + cornerOffsets[i][0],
                                   static_cast<int>(y) + cornerOffsets[i][1],
                                   static_cast<int>(z) + cornerOffsets[i][2]);
                glm::vec3 normal = (glm::dot(grad, grad) < 1e-12f)
                    ? glm::vec3(0.0f, 1.0f, 0.0f)
                    : glm::normalize(grad);

                resource::Vertex vert{};
                vert.position = pos;
                vert.normal = normal;
                vert.texCoords = glm::vec2(
                    (pos.x - sdf.localOrigin.x) / (sdf.config.voxelSize * static_cast<float>(sdf.config.resX - 1)),
                    (pos.z - sdf.localOrigin.z) / (sdf.config.voxelSize * static_cast<float>(sdf.config.resZ - 1)));

                cellVertex[cacheIndex(x, y, z)] = static_cast<int32_t>(vertices.size());
                vertices.push_back(vert);
            }

        // --- Pass 2: one quad per crossed minimal edge ---
        // Minimal edges from corner 0 (x,y,z) run along +X (corner 1), +Y (corner 4),
        // +Z (corner 3). Each crossed edge yields a quad in the plane of the two other
        // axes, joining the four cells sharing that edge: (x,y,z), and its neighbours
        // stepped -1 in each of the two perpendicular axes.
        static constexpr int axisOtherCorner[3] = {1, 4, 3}; // +X, +Y, +Z
        for (uint32_t z = mb.startZ; z < cellHiZ; ++z)
          for (uint32_t y = mb.startY; y < cellHiY; ++y)
            for (uint32_t x = mb.startX; x < cellHiX; ++x)
            {
                bool s0 = isSolid(sampleSDF(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)));

                for (int axis = 0; axis < 3; ++axis)
                {
                    int oc = axisOtherCorner[axis];
                    bool s1 = isSolid(sampleSDF(
                        static_cast<int>(x) + cornerOffsets[oc][0],
                        static_cast<int>(y) + cornerOffsets[oc][1],
                        static_cast<int>(z) + cornerOffsets[oc][2]));
                    if (s0 == s1)
                        continue; // edge not crossed -> no face

                    int au = (axis + 1) % 3;
                    int av = (axis + 2) % 3;
                    int32_t base[3] = {static_cast<int32_t>(x), static_cast<int32_t>(y), static_cast<int32_t>(z)};

                    auto cellAt = [&](int du, int dv) -> int32_t
                    {
                        int32_t cc[3] = {base[0], base[1], base[2]};
                        cc[au] += du;
                        cc[av] += dv;
                        if (cc[0] < static_cast<int32_t>(mb.startX) || cc[0] >= static_cast<int32_t>(cellHiX) ||
                            cc[1] < static_cast<int32_t>(mb.startY) || cc[1] >= static_cast<int32_t>(cellHiY) ||
                            cc[2] < static_cast<int32_t>(mb.startZ) || cc[2] >= static_cast<int32_t>(cellHiZ))
                            return -1;
                        return cellVertex[cacheIndex(
                            static_cast<uint32_t>(cc[0]), static_cast<uint32_t>(cc[1]), static_cast<uint32_t>(cc[2]))];
                    };

                    int32_t q0 = cellAt(0, 0);
                    int32_t q1 = cellAt(-1, 0);
                    int32_t q2 = cellAt(-1, -1);
                    int32_t q3 = cellAt(0, -1);
                    if (q0 < 0 || q1 < 0 || q2 < 0 || q3 < 0)
                        continue; // boundary quad missing a corner cell

                    emitQuad(vertices, indices,
                             static_cast<uint32_t>(q0), static_cast<uint32_t>(q1),
                             static_cast<uint32_t>(q2), static_cast<uint32_t>(q3));
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

} // namespace terrain
