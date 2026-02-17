#include "TerrainTileGenerator.hpp"
#include "../print/EditorLogger.hpp"
#include <meshoptimizer.h>
#include <algorithm>

namespace terrain
{
    void TerrainTileGenerator::updateMeshletBounds(TileLODData& lodData) const
    {
        if (lodData.meshlets.empty() || lodData.vertices.empty())
            return;

        std::vector<unsigned char> triangleIndices;
        triangleIndices.reserve(resource::MAX_MESHLET_PRIMITIVES * 3);

        for (auto& meshlet : lodData.meshlets)
        {
            uint32_t vertexOffset = meshlet.descriptor.vertexOffset;
            uint32_t primitiveOffset = meshlet.descriptor.primitiveOffset;
            uint32_t primitiveCount = meshlet.descriptor.primitiveCount;

            triangleIndices.clear();
            for (uint32_t t = 0; t < primitiveCount; ++t)
            {
                uint32_t packed = lodData.meshletPrimitives[primitiveOffset + t];
                triangleIndices.push_back(static_cast<unsigned char>(packed & 0xFF));
                triangleIndices.push_back(static_cast<unsigned char>((packed >> 8) & 0xFF));
                triangleIndices.push_back(static_cast<unsigned char>((packed >> 16) & 0xFF));
            }

            meshopt_Bounds bounds = meshopt_computeMeshletBounds(
                &lodData.meshletVertices[vertexOffset],
                triangleIndices.data(),
                primitiveCount,
                reinterpret_cast<const float*>(lodData.vertices.data()),
                lodData.vertices.size(),
                sizeof(resource::Vertex)
            );

            meshlet.bounds.boundingSphere = glm::vec4(
                bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius
            );
            meshlet.bounds.cone = glm::vec4(
                bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2],
                bounds.cone_cutoff
            );
        }
    }

    void TerrainTileGenerator::generateMeshlets(TileLODData& lodData) const
    {
        lodData.meshlets.clear();
        lodData.meshletVertices.clear();
        lodData.meshletPrimitives.clear();

        if (lodData.indices.empty() || lodData.vertices.empty())
            return;

        const size_t maxMeshlets = meshopt_buildMeshletsBound(
            lodData.indices.size(),
            resource::MAX_MESHLET_VERTICES,
            resource::MAX_MESHLET_PRIMITIVES
        );

        std::vector<meshopt_Meshlet> meshoptMeshlets(maxMeshlets);
        std::vector<unsigned int> meshletVertexIndices(maxMeshlets * resource::MAX_MESHLET_VERTICES);
        std::vector<unsigned char> meshletTriangleIndices(maxMeshlets * resource::MAX_MESHLET_PRIMITIVES * 3);

        // cone_weight=0.0f: terrain is mostly planar, cone culling adds little value
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
            0.0f
        );

        if (meshletCount == 0)
        {
            vfLogWarning("TerrainTileGenerator: meshopt_buildMeshlets returned 0 meshlets for {} vertices, {} indices",
                         lodData.vertices.size(), lodData.indices.size());
            return;
        }

        const auto& lastMeshlet = meshoptMeshlets[meshletCount - 1];
        size_t totalVertexIndices = lastMeshlet.vertex_offset + lastMeshlet.vertex_count;
        size_t totalTriangleIndices = lastMeshlet.triangle_offset +
            ((lastMeshlet.triangle_count * 3 + 3) & ~3); // Round up to 4-byte alignment

        meshoptMeshlets.resize(meshletCount);
        meshletVertexIndices.resize(totalVertexIndices);
        meshletTriangleIndices.resize(totalTriangleIndices);

        lodData.meshlets.resize(meshletCount);
        lodData.meshletVertices.resize(totalVertexIndices);
        lodData.meshletPrimitives.reserve(meshletCount * resource::MAX_MESHLET_PRIMITIVES);

        for (size_t i = 0; i < totalVertexIndices; ++i)
        {
            lodData.meshletVertices[i] = meshletVertexIndices[i];
        }

        uint32_t primitiveOffset = 0;
        for (size_t i = 0; i < meshletCount; ++i)
        {
            const auto& m = meshoptMeshlets[i];
            auto& outMeshlet = lodData.meshlets[i];

            for (unsigned int t = 0; t < m.triangle_count; ++t)
            {
                size_t triOffset = m.triangle_offset + t * 3;

                if (triOffset + 2 >= meshletTriangleIndices.size())
                {
                    vfLogWarning("Meshlet triangle index out of bounds: offset {} >= size {}",
                                 triOffset + 2, meshletTriangleIndices.size());
                    break;
                }

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
                sizeof(resource::Vertex)
            );

            outMeshlet.bounds.boundingSphere = glm::vec4(
                bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius
            );
            outMeshlet.bounds.cone = glm::vec4(
                bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2],
                bounds.cone_cutoff
            );
        }
    }

    void TerrainTileGenerator::appendSkirtMeshlets(TileLODData& lodData,
                                                      uint32_t mainIndexCount) const
    {
        if (lodData.indices.size() <= mainIndexCount || lodData.vertices.empty())
            return;

        const uint32_t* skirtIndices = lodData.indices.data() + mainIndexCount;
        size_t skirtIndexCount = lodData.indices.size() - mainIndexCount;

        const size_t maxMeshlets = meshopt_buildMeshletsBound(
            skirtIndexCount,
            resource::MAX_MESHLET_VERTICES,
            resource::MAX_MESHLET_PRIMITIVES
        );

        std::vector<meshopt_Meshlet> meshoptMeshlets(maxMeshlets);
        std::vector<unsigned int> meshletVertexIndices(maxMeshlets * resource::MAX_MESHLET_VERTICES);
        std::vector<unsigned char> meshletTriangleIndices(maxMeshlets * resource::MAX_MESHLET_PRIMITIVES * 3);

        size_t meshletCount = meshopt_buildMeshlets(
            meshoptMeshlets.data(),
            meshletVertexIndices.data(),
            meshletTriangleIndices.data(),
            skirtIndices,
            skirtIndexCount,
            reinterpret_cast<const float*>(lodData.vertices.data()),
            lodData.vertices.size(),
            sizeof(resource::Vertex),
            resource::MAX_MESHLET_VERTICES,
            resource::MAX_MESHLET_PRIMITIVES,
            0.0f
        );

        if (meshletCount == 0)
            return;

        const auto& lastMeshlet = meshoptMeshlets[meshletCount - 1];
        size_t totalVertexIndices = lastMeshlet.vertex_offset + lastMeshlet.vertex_count;
        size_t totalTriangleIndices = lastMeshlet.triangle_offset +
            ((lastMeshlet.triangle_count * 3 + 3) & ~3);

        meshoptMeshlets.resize(meshletCount);
        meshletVertexIndices.resize(totalVertexIndices);
        meshletTriangleIndices.resize(totalTriangleIndices);

        uint32_t existingMeshletVertexCount = static_cast<uint32_t>(lodData.meshletVertices.size());
        uint32_t existingPrimitiveCount = static_cast<uint32_t>(lodData.meshletPrimitives.size());

        for (size_t i = 0; i < totalVertexIndices; ++i)
        {
            lodData.meshletVertices.push_back(meshletVertexIndices[i]);
        }

        uint32_t primitiveOffset = existingPrimitiveCount;
        for (size_t i = 0; i < meshletCount; ++i)
        {
            const auto& m = meshoptMeshlets[i];
            resource::Meshlet outMeshlet{};

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

            outMeshlet.descriptor.vertexOffset = m.vertex_offset + existingMeshletVertexCount;
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
                sizeof(resource::Vertex)
            );

            outMeshlet.bounds.boundingSphere = glm::vec4(
                bounds.center[0], bounds.center[1], bounds.center[2], bounds.radius
            );
            outMeshlet.bounds.cone = glm::vec4(
                bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2],
                bounds.cone_cutoff
            );

            lodData.meshlets.push_back(outMeshlet);
        }
    }

    void TerrainTileGenerator::overrideBoundaryNormals(
        std::vector<resource::Vertex>& vertices,
        const TerrainTile& tile,
        uint32_t lodLevel,
        uint32_t vertCount,
        const TileLookup& getTile) const
    {
        if (!getTile || vertices.empty())
            return;

        uint32_t skipFactor = getLODSkipFactor(lodLevel);
        uint32_t baseVertCount = config.getVertexCount();
        float gridSpacing = config.getVertexSpacing();

        // Sample height from full-resolution heightData, crossing into neighbors for out-of-bounds
        auto sampleHeight = [&](int32_t hx, int32_t hz) -> float
        {
            if (hx >= 0 && hx < static_cast<int32_t>(baseVertCount) &&
                hz >= 0 && hz < static_cast<int32_t>(baseVertCount))
            {
                return tile.getHeight(static_cast<uint32_t>(hx), static_cast<uint32_t>(hz));
            }

            if (hz >= static_cast<int32_t>(baseVertCount))
            {
                TileCoord nc = tile.coord + TileCoord::getNeighborOffset(TileEdge::North);
                const TerrainTile* n = getTile(nc);
                uint32_t clampedX = static_cast<uint32_t>(std::clamp(hx, 0, static_cast<int32_t>(baseVertCount) - 1));
                uint32_t neighborHz = static_cast<uint32_t>(hz - static_cast<int32_t>(baseVertCount) + 1);
                return n ? n->getHeight(clampedX, std::min(neighborHz, baseVertCount - 1))
                         : tile.getHeight(clampedX, baseVertCount - 1);
            }

            if (hz < 0)
            {
                TileCoord nc = tile.coord + TileCoord::getNeighborOffset(TileEdge::South);
                const TerrainTile* n = getTile(nc);
                uint32_t clampedX = static_cast<uint32_t>(std::clamp(hx, 0, static_cast<int32_t>(baseVertCount) - 1));
                uint32_t neighborHz = static_cast<uint32_t>(static_cast<int32_t>(baseVertCount) - 1 + hz);
                return n ? n->getHeight(clampedX, std::min(neighborHz, baseVertCount - 1))
                         : tile.getHeight(clampedX, 0);
            }

            if (hx >= static_cast<int32_t>(baseVertCount))
            {
                TileCoord nc = tile.coord + TileCoord::getNeighborOffset(TileEdge::East);
                const TerrainTile* n = getTile(nc);
                uint32_t clampedZ = static_cast<uint32_t>(std::clamp(hz, 0, static_cast<int32_t>(baseVertCount) - 1));
                uint32_t neighborHx = static_cast<uint32_t>(hx - static_cast<int32_t>(baseVertCount) + 1);
                return n ? n->getHeight(std::min(neighborHx, baseVertCount - 1), clampedZ)
                         : tile.getHeight(baseVertCount - 1, clampedZ);
            }

            if (hx < 0)
            {
                TileCoord nc = tile.coord + TileCoord::getNeighborOffset(TileEdge::West);
                const TerrainTile* n = getTile(nc);
                uint32_t clampedZ = static_cast<uint32_t>(std::clamp(hz, 0, static_cast<int32_t>(baseVertCount) - 1));
                uint32_t neighborHx = static_cast<uint32_t>(static_cast<int32_t>(baseVertCount) - 1 + hx);
                return n ? n->getHeight(std::min(neighborHx, baseVertCount - 1), clampedZ)
                         : tile.getHeight(0, clampedZ);
            }

            return 0.0f;
        };

        for (uint32_t z = 0; z < vertCount; ++z)
        {
            for (uint32_t x = 0; x < vertCount; ++x)
            {
                if (!isEdgeVertex(x, z, vertCount))
                    continue;

                uint32_t idx = z * vertCount + x;
                int32_t hx = static_cast<int32_t>(x * skipFactor);
                int32_t hz = static_cast<int32_t>(z * skipFactor);

                float hL = sampleHeight(hx - 1, hz);
                float hR = sampleHeight(hx + 1, hz);
                float hD = sampleHeight(hx, hz - 1);
                float hU = sampleHeight(hx, hz + 1);

                glm::vec3 normal(hL - hR, 2.0f * gridSpacing, hD - hU);

                float len = glm::length(normal);
                if (len > 1e-6f)
                    normal /= len;
                else
                    normal = glm::vec3(0.0f, 1.0f, 0.0f);

                vertices[idx].normal = normal;
            }
        }
    }

    float TerrainTileGenerator::computeGeometricError(
        const TerrainTile& tile, uint32_t lodLevel) const
    {
        if (lodLevel == 0)
            return 0.0f;

        uint32_t thisSkip = getLODSkipFactor(lodLevel);
        uint32_t prevSkip = getLODSkipFactor(lodLevel - 1);
        uint32_t baseVertCount = config.getVertexCount();

        float maxError = 0.0f;

        // Sample all vertices at current LOD resolution and compare against
        // what the previous LOD would interpolate at those positions
        for (uint32_t z = 0; z < baseVertCount; z += thisSkip)
        {
            for (uint32_t x = 0; x < baseVertCount; x += thisSkip)
            {
                float actualHeight = tile.getHeight(x, z);

                uint32_t prevX0 = (x / prevSkip) * prevSkip;
                uint32_t prevZ0 = (z / prevSkip) * prevSkip;
                uint32_t prevX1 = std::min(prevX0 + prevSkip, baseVertCount - 1);
                uint32_t prevZ1 = std::min(prevZ0 + prevSkip, baseVertCount - 1);

                float fx = (prevX1 != prevX0)
                               ? static_cast<float>(x - prevX0) / static_cast<float>(prevX1 - prevX0)
                               : 0.0f;
                float fz = (prevZ1 != prevZ0)
                               ? static_cast<float>(z - prevZ0) / static_cast<float>(prevZ1 - prevZ0)
                               : 0.0f;

                float h00 = tile.getHeight(prevX0, prevZ0);
                float h10 = tile.getHeight(prevX1, prevZ0);
                float h01 = tile.getHeight(prevX0, prevZ1);
                float h11 = tile.getHeight(prevX1, prevZ1);

                float interpolatedHeight =
                    h00 * (1.0f - fx) * (1.0f - fz) +
                    h10 * fx * (1.0f - fz) +
                    h01 * (1.0f - fx) * fz +
                    h11 * fx * fz;

                float error = std::abs(actualHeight - interpolatedHeight);
                maxError = std::max(maxError, error);
            }
        }

        // Minimum error ensures LOD transitions still happen on flat terrain
        float vertexSpacing = config.getVertexSpacing() * static_cast<float>(thisSkip);
        float minError = vertexSpacing * 0.5f;

        return std::max(maxError, minError);
    }

    void TerrainTileGenerator::computeAllLODErrors(TerrainTile& tile) const
    {
        for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod)
        {
            TileLODData& lodData = tile.getLODData(lod);
            lodData.geometricError = computeGeometricError(tile, lod);
        }
    }
}
