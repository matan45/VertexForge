#include "TerrainTileGenerator.hpp"
#include "../print/EditorLogger.hpp"
#include <algorithm>
#include <cmath>
#include <meshoptimizer.h>

namespace terrain
{
    TerrainTileGenerator::TerrainTileGenerator(const TerrainTileConfig& config)
        : config_(config)
    {
    }

    void TerrainTileGenerator::setConfig(const TerrainTileConfig& config)
    {
        config_ = config;
    }

    void TerrainTileGenerator::setHeightSampler(HeightSampler sampler)
    {
        heightSampler_ = std::move(sampler);
    }

    std::unique_ptr<TerrainTile> TerrainTileGenerator::generateTile(
        const TileCoord& coord,
        ProgressCallback progress) const
    {
        auto tile = std::make_unique<TerrainTile>(coord, config_);

        if (progress)
            progress(0.0f, "Sampling heights");

        // If we have a height sampler, use it to populate height data
        if (heightSampler_)
        {
            uint32_t vertexCount = config_.getVertexCount();
            float spacing = config_.getVertexSpacing();
            glm::vec3 origin = tile->computeWorldOrigin();

            std::vector<float> heights(static_cast<size_t>(vertexCount) * vertexCount);

            for (uint32_t z = 0; z < vertexCount; ++z)
            {
                for (uint32_t x = 0; x < vertexCount; ++x)
                {
                    float worldX = origin.x + static_cast<float>(x) * spacing;
                    float worldZ = origin.z + static_cast<float>(z) * spacing;
                    heights[static_cast<size_t>(z) * vertexCount + x] = heightSampler_(worldX, worldZ);
                }
            }

            tile->initializeFromHeights(heights);
        }

        if (progress)
            progress(0.2f, "Generating LODs");

        // Generate all LOD levels
        generateAllLODs(*tile, progress);

        if (progress)
            progress(1.0f, "Complete");

        return tile;
    }

    void TerrainTileGenerator::generateLODGeometry(TerrainTile& tile, uint32_t lodLevel) const
    {
        if (lodLevel >= TERRAIN_LOD_COUNT)
            return;

        TileLODData& lodData = tile.getLODData(lodLevel);
        lodData.clear();

        // Generate vertices
        generateVertices(lodData.vertices, tile, lodLevel);

        // Generate indices
        generateIndices(lodData.indices, lodLevel);

        // Calculate normals
        calculateNormals(lodData.vertices, lodData.indices);

        // Generate skirts for LOD crack prevention
        if (config_.skirtDepth > 0.0f)
        {
            generateSkirts(lodData.vertices, lodData.indices, lodLevel, config_.skirtDepth);
        }

        // Calculate bounds
        calculateBounds(lodData);

        // Extract edge vertices for stitching
        extractEdgeVertices(tile, lodLevel);

        // Generate meshlets for GPU mesh shading
        generateMeshlets(lodData);

        tile.isDirty = false;
    }

    void TerrainTileGenerator::generateAllLODs(TerrainTile& tile, ProgressCallback progress) const
    {
        for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod)
        {
            if (progress)
            {
                float lodProgress = 0.2f + (static_cast<float>(lod) / TERRAIN_LOD_COUNT) * 0.6f;
                progress(lodProgress, "Generating LOD " + std::to_string(lod));
            }

            generateLODGeometry(tile, lod);
        }

        // Compute geometric error metrics for each LOD level
        if (progress)
            progress(0.85f, "Computing error metrics");

        computeAllLODErrors(tile);

        if (progress)
            progress(0.9f, "Finalizing");
    }

    void TerrainTileGenerator::generateMeshlets(TileLODData& lodData) const
    {
        lodData.meshlets.clear();
        lodData.meshletVertices.clear();
        lodData.meshletPrimitives.clear();

        if (lodData.indices.empty() || lodData.vertices.empty())
            return;

        // 1. Calculate max meshlets needed
        const size_t maxMeshlets = meshopt_buildMeshletsBound(
            lodData.indices.size(),
            resource::MAX_MESHLET_VERTICES,
            resource::MAX_MESHLET_PRIMITIVES
        );

        // 2. Allocate temporary arrays for meshoptimizer
        std::vector<meshopt_Meshlet> meshoptMeshlets(maxMeshlets);
        std::vector<unsigned int> meshletVertexIndices(maxMeshlets * resource::MAX_MESHLET_VERTICES);
        std::vector<unsigned char> meshletTriangleIndices(maxMeshlets * resource::MAX_MESHLET_PRIMITIVES * 3);

        // 3. Build meshlets (cone_weight=0.0f for terrain - mostly planar surfaces)
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
            return;

        // 4. Trim arrays to actual size
        const auto& lastMeshlet = meshoptMeshlets[meshletCount - 1];
        size_t totalVertexIndices = lastMeshlet.vertex_offset + lastMeshlet.vertex_count;
        size_t totalTriangleIndices = lastMeshlet.triangle_offset +
            ((lastMeshlet.triangle_count * 3 + 3) & ~3);  // Round up to 4-byte alignment

        meshoptMeshlets.resize(meshletCount);
        meshletVertexIndices.resize(totalVertexIndices);
        meshletTriangleIndices.resize(totalTriangleIndices);

        // 5. Convert to engine format
        lodData.meshlets.resize(meshletCount);
        lodData.meshletVertices.resize(totalVertexIndices);
        lodData.meshletPrimitives.reserve(meshletCount * resource::MAX_MESHLET_PRIMITIVES);

        // Copy vertex indices
        for (size_t i = 0; i < totalVertexIndices; ++i)
        {
            lodData.meshletVertices[i] = meshletVertexIndices[i];
        }

        // 6. Pack triangle indices and compute bounds for each meshlet
        uint32_t primitiveOffset = 0;
        for (size_t i = 0; i < meshletCount; ++i)
        {
            const auto& m = meshoptMeshlets[i];
            auto& outMeshlet = lodData.meshlets[i];

            // Pack 3×uint8 triangle indices into uint32
            for (unsigned int t = 0; t < m.triangle_count; ++t)
            {
                size_t triOffset = m.triangle_offset + t * 3;

                // Bounds check to prevent buffer overflow if meshoptimizer returns unexpected data
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

            // Fill descriptor
            outMeshlet.descriptor.vertexOffset = m.vertex_offset;
            outMeshlet.descriptor.primitiveOffset = primitiveOffset;
            outMeshlet.descriptor.vertexCount = static_cast<uint8_t>(m.vertex_count);
            outMeshlet.descriptor.primitiveCount = static_cast<uint8_t>(m.triangle_count);
            outMeshlet.descriptor.padding = 0;
            primitiveOffset += m.triangle_count;

            // 7. Compute bounding sphere and cone for per-meshlet culling
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

    void TerrainTileGenerator::generateVertices(
        std::vector<resource::Vertex>& vertices,
        const TerrainTile& tile,
        uint32_t lodLevel) const
    {
        uint32_t vertCount = getLODVertexCount(lodLevel);
        uint32_t skipFactor = getLODSkipFactor(lodLevel);
        float spacing = config_.getVertexSpacing() * static_cast<float>(skipFactor);

        vertices.clear();
        vertices.reserve(static_cast<size_t>(vertCount) * vertCount);

        for (uint32_t z = 0; z < vertCount; ++z)
        {
            for (uint32_t x = 0; x < vertCount; ++x)
            {
                resource::Vertex v{};

                // Get height from tile's height data using skip factor
                uint32_t heightX = x * skipFactor;
                uint32_t heightZ = z * skipFactor;
                float height = tile.getHeight(heightX, heightZ);

                // Position in tile-local space
                v.position = glm::vec3(
                    static_cast<float>(x) * spacing,
                    height,
                    static_cast<float>(z) * spacing
                );

                // UV coordinates [0, 1]
                v.texCoords = glm::vec2(
                    static_cast<float>(x) / static_cast<float>(vertCount - 1),
                    static_cast<float>(z) / static_cast<float>(vertCount - 1)
                );

                // Normal will be calculated after indices are generated
                v.normal = glm::vec3(0.0f, 1.0f, 0.0f);

                // Initialize bone data (unused for terrain)
                v.boneIndices = glm::ivec4(-1, -1, -1, -1);
                v.boneWeights = glm::vec4(0.0f);

                vertices.push_back(v);
            }
        }
    }

    void TerrainTileGenerator::generateIndices(
        std::vector<uint32_t>& indices,
        uint32_t lodLevel) const
    {
        uint32_t vertCount = getLODVertexCount(lodLevel);
        uint32_t quadCount = vertCount - 1;

        indices.clear();
        indices.reserve(static_cast<size_t>(quadCount) * quadCount * 6);

        for (uint32_t z = 0; z < quadCount; ++z)
        {
            for (uint32_t x = 0; x < quadCount; ++x)
            {
                uint32_t topLeft = z * vertCount + x;
                uint32_t topRight = topLeft + 1;
                uint32_t bottomLeft = (z + 1) * vertCount + x;
                uint32_t bottomRight = bottomLeft + 1;

                // First triangle (CCW winding for Vulkan front-face)
                indices.push_back(topLeft);
                indices.push_back(bottomLeft);
                indices.push_back(topRight);

                // Second triangle
                indices.push_back(topRight);
                indices.push_back(bottomLeft);
                indices.push_back(bottomRight);
            }
        }
    }

    void TerrainTileGenerator::calculateNormals(
        std::vector<resource::Vertex>& vertices,
        const std::vector<uint32_t>& indices) const
    {
        if (vertices.empty() || indices.empty())
            return;

        // Reset all normals
        for (auto& v : vertices)
        {
            v.normal = glm::vec3(0.0f);
        }

        // Accumulate face normals
        for (size_t i = 0; i < indices.size(); i += 3)
        {
            uint32_t i0 = indices[i];
            uint32_t i1 = indices[i + 1];
            uint32_t i2 = indices[i + 2];

            const glm::vec3& p0 = vertices[i0].position;
            const glm::vec3& p1 = vertices[i1].position;
            const glm::vec3& p2 = vertices[i2].position;

            glm::vec3 edge1 = p1 - p0;
            glm::vec3 edge2 = p2 - p0;
            glm::vec3 faceNormal = glm::cross(edge1, edge2);

            // Weight by triangle area (magnitude of cross product)
            vertices[i0].normal += faceNormal;
            vertices[i1].normal += faceNormal;
            vertices[i2].normal += faceNormal;
        }

        // Normalize all normals
        for (auto& v : vertices)
        {
            float length = glm::length(v.normal);
            if (length > 1e-6f)
            {
                v.normal /= length;
            }
            else
            {
                v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            }
        }
    }

    void TerrainTileGenerator::calculateBounds(TileLODData& lodData) const
    {
        if (lodData.vertices.empty())
        {
            lodData.aabb = math::AABB();
            lodData.boundingSphere = glm::vec4(0.0f);
            return;
        }

        // Calculate AABB
        glm::vec3 minPos = lodData.vertices[0].position;
        glm::vec3 maxPos = lodData.vertices[0].position;

        for (const auto& v : lodData.vertices)
        {
            minPos = glm::min(minPos, v.position);
            maxPos = glm::max(maxPos, v.position);
        }

        lodData.aabb = math::AABB(minPos, maxPos);

        // Calculate bounding sphere (use AABB center and max distance)
        glm::vec3 center = lodData.aabb.getCenter();
        float maxDistSq = 0.0f;

        for (const auto& v : lodData.vertices)
        {
            glm::vec3 diff = v.position - center;
            float distSq = glm::dot(diff, diff);
            maxDistSq = std::max(maxDistSq, distSq);
        }

        lodData.boundingSphere = glm::vec4(center, std::sqrt(maxDistSq));
    }

    void TerrainTileGenerator::generateSkirts(
        std::vector<resource::Vertex>& vertices,
        std::vector<uint32_t>& indices,
        uint32_t lodLevel,
        float skirtDepth) const
    {
        // Store the original vertex count before adding skirt vertices
        std::vector<resource::Vertex> mainVertices = vertices;

        // Add skirts for all four edges
        addSkirtEdge(vertices, indices, mainVertices, TileEdge::North, lodLevel, skirtDepth);
        addSkirtEdge(vertices, indices, mainVertices, TileEdge::East, lodLevel, skirtDepth);
        addSkirtEdge(vertices, indices, mainVertices, TileEdge::South, lodLevel, skirtDepth);
        addSkirtEdge(vertices, indices, mainVertices, TileEdge::West, lodLevel, skirtDepth);
    }

    void TerrainTileGenerator::addSkirtEdge(
        std::vector<resource::Vertex>& vertices,
        std::vector<uint32_t>& indices,
        const std::vector<resource::Vertex>& mainVertices,
        TileEdge edge,
        uint32_t lodLevel,
        float skirtDepth) const
    {
        uint32_t vertCount = getLODVertexCount(lodLevel);

        // Determine which vertices form this edge
        std::vector<uint32_t> edgeIndices;
        edgeIndices.reserve(vertCount);

        for (uint32_t i = 0; i < vertCount; ++i)
        {
            uint32_t idx = 0;
            switch (edge)
            {
                case TileEdge::North:  // Top edge (z = vertCount-1)
                    idx = (vertCount - 1) * vertCount + i;
                    break;
                case TileEdge::South:  // Bottom edge (z = 0)
                    idx = i;
                    break;
                case TileEdge::East:   // Right edge (x = vertCount-1)
                    idx = i * vertCount + (vertCount - 1);
                    break;
                case TileEdge::West:   // Left edge (x = 0)
                    idx = i * vertCount;
                    break;
            }
            edgeIndices.push_back(idx);
        }

        // Create skirt vertices (lowered versions of edge vertices)
        uint32_t skirtStartIndex = static_cast<uint32_t>(vertices.size());

        for (uint32_t idx : edgeIndices)
        {
            resource::Vertex skirtVertex = mainVertices[idx];
            skirtVertex.position.y -= skirtDepth;
            vertices.push_back(skirtVertex);
        }

        // Create skirt triangles connecting edge vertices to skirt vertices
        for (size_t i = 0; i < edgeIndices.size() - 1; ++i)
        {
            uint32_t topCurrent = edgeIndices[i];
            uint32_t topNext = edgeIndices[i + 1];
            uint32_t bottomCurrent = skirtStartIndex + static_cast<uint32_t>(i);
            uint32_t bottomNext = skirtStartIndex + static_cast<uint32_t>(i) + 1;

            // Two triangles per quad
            // Winding depends on edge direction to maintain consistent facing
            switch (edge)
            {
                case TileEdge::North:
                case TileEdge::East:
                    indices.push_back(topCurrent);
                    indices.push_back(topNext);
                    indices.push_back(bottomCurrent);

                    indices.push_back(bottomCurrent);
                    indices.push_back(topNext);
                    indices.push_back(bottomNext);
                    break;

                case TileEdge::South:
                case TileEdge::West:
                    indices.push_back(topCurrent);
                    indices.push_back(bottomCurrent);
                    indices.push_back(topNext);

                    indices.push_back(topNext);
                    indices.push_back(bottomCurrent);
                    indices.push_back(bottomNext);
                    break;
            }
        }
    }

    void TerrainTileGenerator::extractEdgeVertices(TerrainTile& tile, uint32_t lodLevel) const
    {
        if (lodLevel >= TERRAIN_LOD_COUNT)
            return;

        const TileLODData& lodData = tile.getLODData(lodLevel);
        if (lodData.isEmpty())
            return;

        uint32_t vertCount = getLODVertexCount(lodLevel);

        // Extract vertices for each edge
        for (uint8_t edgeIdx = 0; edgeIdx < 4; ++edgeIdx)
        {
            TileEdge edge = static_cast<TileEdge>(edgeIdx);
            EdgeVertices& edgeVerts = tile.edgeVertices[lodLevel][edgeIdx];
            edgeVerts.clear();
            edgeVerts.indices.reserve(vertCount);
            edgeVerts.positions.reserve(vertCount);

            for (uint32_t i = 0; i < vertCount; ++i)
            {
                uint32_t idx = 0;
                switch (edge)
                {
                    case TileEdge::North:
                        idx = (vertCount - 1) * vertCount + i;
                        break;
                    case TileEdge::South:
                        idx = i;
                        break;
                    case TileEdge::East:
                        idx = i * vertCount + (vertCount - 1);
                        break;
                    case TileEdge::West:
                        idx = i * vertCount;
                        break;
                }

                edgeVerts.indices.push_back(idx);

                // Store world-space position for neighbor comparison
                glm::vec3 worldPos = tile.worldOrigin + lodData.vertices[idx].position;
                edgeVerts.positions.push_back(worldPos);
            }
        }
    }

    uint32_t TerrainTileGenerator::calculateLOD(
        const glm::vec3& cameraPosition,
        const TerrainTile& tile) const
    {
        // Calculate distance from camera to tile center
        glm::vec3 tileCenter = tile.worldBounds.getCenter();
        float distance = glm::length(cameraPosition - tileCenter);

        // Find appropriate LOD level based on distance thresholds
        for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod)
        {
            if (distance < config_.lodDistances[lod])
            {
                return lod;
            }
        }

        return TERRAIN_LOD_COUNT - 1;  // Lowest detail
    }

    uint32_t TerrainTileGenerator::getLODVertexCount(uint32_t lodLevel) const
    {
        uint32_t baseVerts = config_.getVertexCount();
        uint32_t skip = 1u << lodLevel;  // 1, 2, 4, 8
        return (baseVerts - 1) / skip + 1;
    }

    uint32_t TerrainTileGenerator::getLODSkipFactor(uint32_t lodLevel) const
    {
        return 1u << lodLevel;  // 1, 2, 4, 8
    }

    float TerrainTileGenerator::computeGeometricError(
        const TerrainTile& tile, uint32_t lodLevel) const
    {
        if (lodLevel == 0)
            return 0.0f;  // Highest detail has no error

        uint32_t thisSkip = getLODSkipFactor(lodLevel);
        uint32_t prevSkip = getLODSkipFactor(lodLevel - 1);
        uint32_t baseVertCount = config_.getVertexCount();

        float maxError = 0.0f;

        // Sample all vertices at current LOD resolution and compare against
        // what the previous LOD would interpolate at those positions
        for (uint32_t z = 0; z < baseVertCount; z += thisSkip)
        {
            for (uint32_t x = 0; x < baseVertCount; x += thisSkip)
            {
                float actualHeight = tile.getHeight(x, z);

                // Find the enclosing quad in the previous LOD grid
                uint32_t prevX0 = (x / prevSkip) * prevSkip;
                uint32_t prevZ0 = (z / prevSkip) * prevSkip;
                uint32_t prevX1 = std::min(prevX0 + prevSkip, baseVertCount - 1);
                uint32_t prevZ1 = std::min(prevZ0 + prevSkip, baseVertCount - 1);

                // Compute interpolation factors
                float fx = (prevX1 != prevX0) ?
                    static_cast<float>(x - prevX0) / static_cast<float>(prevX1 - prevX0) : 0.0f;
                float fz = (prevZ1 != prevZ0) ?
                    static_cast<float>(z - prevZ0) / static_cast<float>(prevZ1 - prevZ0) : 0.0f;

                // Get heights at the four corners of the enclosing quad
                float h00 = tile.getHeight(prevX0, prevZ0);
                float h10 = tile.getHeight(prevX1, prevZ0);
                float h01 = tile.getHeight(prevX0, prevZ1);
                float h11 = tile.getHeight(prevX1, prevZ1);

                // Bilinear interpolation
                float interpolatedHeight =
                    h00 * (1.0f - fx) * (1.0f - fz) +
                    h10 * fx * (1.0f - fz) +
                    h01 * (1.0f - fx) * fz +
                    h11 * fx * fz;

                float error = std::abs(actualHeight - interpolatedHeight);
                maxError = std::max(maxError, error);
            }
        }

        return maxError;
    }

    void TerrainTileGenerator::computeAllLODErrors(TerrainTile& tile) const
    {
        for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod)
        {
            TileLODData& lodData = tile.getLODData(lod);
            lodData.geometricError = computeGeometricError(tile, lod);
        }
    }

    void TerrainTileGenerator::computeEdgeStitching(
        TerrainTile& tile, TileEdge edge, uint8_t neighborLOD) const
    {
        uint8_t currentLOD = tile.currentLOD;
        uint8_t edgeIndex = static_cast<uint8_t>(edge);
        EdgeStitchInfo& info = tile.edgeStitchInfo[edgeIndex];

        // Only stitch if neighbor has coarser (higher numbered) LOD
        if (neighborLOD <= currentLOD)
        {
            info.clear();
            return;
        }

        info.needsSnapping = true;
        info.neighborLOD = neighborLOD;

        // Get edge vertices for current and neighbor LOD levels
        const EdgeVertices& currentEdge = tile.edgeVertices[currentLOD][edgeIndex];
        const EdgeVertices& neighborEdge = tile.edgeVertices[neighborLOD][edgeIndex];

        if (currentEdge.positions.empty() || neighborEdge.positions.empty())
        {
            info.clear();
            return;
        }

        uint32_t currentCount = static_cast<uint32_t>(currentEdge.positions.size());
        uint32_t neighborCount = static_cast<uint32_t>(neighborEdge.positions.size());

        info.snappedHeights.resize(currentCount);

        // Map current edge vertices to interpolated positions on neighbor's coarser edge
        float ratio = static_cast<float>(neighborCount - 1) / static_cast<float>(currentCount - 1);

        for (uint32_t i = 0; i < currentCount; ++i)
        {
            float neighborIdx = static_cast<float>(i) * ratio;
            uint32_t idx0 = static_cast<uint32_t>(neighborIdx);
            uint32_t idx1 = std::min(idx0 + 1, neighborCount - 1);
            float t = neighborIdx - static_cast<float>(idx0);

            // Interpolate Y (height) from neighbor's edge
            float y0 = neighborEdge.positions[idx0].y;
            float y1 = neighborEdge.positions[idx1].y;
            info.snappedHeights[i] = glm::mix(y0, y1, t);
        }
    }

    void TerrainTileGenerator::updateEdgeStitching(TerrainTile& tile) const
    {
        for (uint8_t i = 0; i < 4; ++i)
        {
            const NeighborInfo& neighbor = tile.neighbors[i];
            if (neighbor.exists)
            {
                computeEdgeStitching(tile, static_cast<TileEdge>(i), neighbor.lodLevel);
            }
            else
            {
                tile.edgeStitchInfo[i].clear();
            }
        }
    }

} // namespace terrain
