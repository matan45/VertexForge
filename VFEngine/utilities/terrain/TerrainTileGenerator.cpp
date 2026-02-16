#include "TerrainTileGenerator.hpp"
#include "../print/EditorLogger.hpp"
#include <algorithm>
#include <cmath>
#include <meshoptimizer.h>

namespace terrain
{
    TerrainTileGenerator::TerrainTileGenerator(const TerrainTileConfig& config)
        : config(config)
    {
    }

    void TerrainTileGenerator::setConfig(const TerrainTileConfig& config_)
    {
        config = config_;
    }

    void TerrainTileGenerator::setHeightSampler(HeightSampler sampler)
    {
        heightSampler = std::move(sampler);
    }

    std::unique_ptr<TerrainTile> TerrainTileGenerator::generateTile(
        const TileCoord& coord,
        ProgressCallback progress) const
    {
        auto tile = std::make_unique<TerrainTile>(coord, config);

        if (progress)
            progress(0.0f, "Sampling heights");

        // If we have a height sampler, use it to populate height data
        if (heightSampler)
        {
            uint32_t vertexCount = config.getVertexCount();
            float spacing = config.getVertexSpacing();
            glm::vec3 origin = tile->computeWorldOrigin();

            std::vector<float> heights(static_cast<size_t>(vertexCount) * vertexCount);

            for (uint32_t z = 0; z < vertexCount; ++z)
            {
                for (uint32_t x = 0; x < vertexCount; ++x)
                {
                    float worldX = origin.x + static_cast<float>(x) * spacing;
                    float worldZ = origin.z + static_cast<float>(z) * spacing;
                    heights[static_cast<size_t>(z) * vertexCount + x] = heightSampler(worldX, worldZ);
                }
            }

            tile->initializeFromHeights(heights);
        }

        // Initialize weight map with default layer 0 = 1.0
        tile->initializeWeightMap(1);

        if (progress)
            progress(0.2f, "Generating LODs");

        generateAllLODs(*tile, progress);

        if (progress)
            progress(1.0f, "Complete");

        return tile;
    }

    void TerrainTileGenerator::generateLODGeometry(TerrainTile& tile, uint32_t lodLevel,
                                                    const TileLookup& getTile) const
    {
        if (lodLevel >= TERRAIN_LOD_COUNT)
            return;

        TileLODData& lodData = tile.getLODData(lodLevel);
        lodData.clear();

        generateVertices(lodData.vertices, tile, lodLevel);
        generateIndices(lodData.indices, lodLevel);

        // Calculate normals: face-weighted for interior, analytical for boundary
        uint32_t vertCount = getLODVertexCount(lodLevel);
        calculateNormals(lodData.vertices, lodData.indices, vertCount);
        overrideBoundaryNormals(lodData.vertices, tile, lodLevel, vertCount, getTile);

        // Generate skirts for LOD crack prevention
        if (config.skirtDepth > 0.0f)
        {
            generateSkirts(lodData.vertices, lodData.indices, lodLevel, config.skirtDepth);
        }

        calculateBounds(lodData);

        // Extract edge vertices for stitching
        extractEdgeVertices(tile, lodLevel);

        // Generate meshlets for GPU mesh shading
        generateMeshlets(lodData);

        tile.clearLODDirty(lodLevel);
        if (tile.dirtyLODMask == 0)
        {
            tile.isDirty = false;
        }
    }

    void TerrainTileGenerator::generateLODGeometryFast(TerrainTile& tile, uint32_t lodLevel,
                                                        const TileLookup& getTile) const
    {
        if (lodLevel >= TERRAIN_LOD_COUNT)
            return;

        TileLODData& lodData = tile.getLODData(lodLevel);

        // Save meshlet topology (clustering doesn't change when only heights change)
        auto meshlets = std::move(lodData.meshlets);
        auto meshletVertices = std::move(lodData.meshletVertices);
        auto meshletPrimitives = std::move(lodData.meshletPrimitives);

        lodData.clear();

        // Regenerate geometry with updated heights
        generateVertices(lodData.vertices, tile, lodLevel);
        generateIndices(lodData.indices, lodLevel);

        uint32_t vertCount = getLODVertexCount(lodLevel);
        calculateNormals(lodData.vertices, lodData.indices, vertCount);
        overrideBoundaryNormals(lodData.vertices, tile, lodLevel, vertCount, getTile);

        if (config.skirtDepth > 0.0f)
        {
            generateSkirts(lodData.vertices, lodData.indices, lodLevel, config.skirtDepth);
        }

        calculateBounds(lodData);
        extractEdgeVertices(tile, lodLevel);

        // Restore meshlet topology and update only the bounds
        lodData.meshlets = std::move(meshlets);
        lodData.meshletVertices = std::move(meshletVertices);
        lodData.meshletPrimitives = std::move(meshletPrimitives);

        updateMeshletBounds(lodData);

        tile.clearLODDirty(lodLevel);
        if (tile.dirtyLODMask == 0)
        {
            tile.isDirty = false;
        }
    }

    void TerrainTileGenerator::regenerateLOD(TerrainTile& tile, uint32_t lodLevel,
                                              const TileLookup& getTile) const
    {
        if (lodLevel >= TERRAIN_LOD_COUNT)
            return;

        TileLODData& lodData = tile.getLODData(lodLevel);

        if (lodData.hasMeshlets())
        {
            // Fast path: meshlet topology unchanged, only heights changed
            generateLODGeometryFast(tile, lodLevel, getTile);
        }
        else
        {
            // Full path: first generation or topology change
            generateLODGeometry(tile, lodLevel, getTile);
        }

        lodData.geometricError = computeGeometricError(tile, lodLevel);
        tile.setLODGPUDirty(lodLevel);
    }

    void TerrainTileGenerator::generateAllLODs(TerrainTile& tile, ProgressCallback progress,
                                                const TileLookup& getTile) const
    {
        for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod)
        {
            if (progress)
            {
                float lodProgress = 0.2f + (static_cast<float>(lod) / TERRAIN_LOD_COUNT) * 0.6f;
                progress(lodProgress, "Generating LOD " + std::to_string(lod));
            }

            generateLODGeometry(tile, lod, getTile);
        }

        // Compute geometric error metrics for each LOD level
        if (progress)
            progress(0.85f, "Computing error metrics");

        computeAllLODErrors(tile);

        if (progress)
            progress(0.9f, "Finalizing");
    }

    void TerrainTileGenerator::generateVertices(
        std::vector<resource::Vertex>& vertices,
        const TerrainTile& tile,
        uint32_t lodLevel) const
    {
        uint32_t vertCount = getLODVertexCount(lodLevel);
        uint32_t skipFactor = getLODSkipFactor(lodLevel);
        float spacing = config.getVertexSpacing() * static_cast<float>(skipFactor);

        vertices.clear();
        vertices.reserve(static_cast<size_t>(vertCount) * vertCount);

        for (uint32_t z = 0; z < vertCount; ++z)
        {
            for (uint32_t x = 0; x < vertCount; ++x)
            {
                resource::Vertex v{};

                // Get height - applies edge stitching for edge/corner vertices
                float height = getStitchedHeight(tile, x, z, vertCount, lodLevel);

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

                // CCW winding for Vulkan front-face
                indices.push_back(topLeft);
                indices.push_back(bottomLeft);
                indices.push_back(topRight);

                indices.push_back(topRight);
                indices.push_back(bottomLeft);
                indices.push_back(bottomRight);
            }
        }
    }

    void TerrainTileGenerator::calculateNormals(
        std::vector<resource::Vertex>& vertices,
        const std::vector<uint32_t>& indices,
        uint32_t vertCount) const
    {
        if (vertices.empty() || indices.empty())
            return;

        for (auto& v : vertices)
        {
            v.normal = glm::vec3(0.0f);
        }

        // Accumulate face normals from tile's own triangles
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

        // Boundary vertex normals are overridden by overrideBoundaryNormals()
        // using analytical central differences from full-resolution heightData,
        // guaranteeing matching normals across tiles regardless of LOD.

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
        std::vector<resource::Vertex> mainVertices = vertices;

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
            case TileEdge::North: // Top edge (z = vertCount-1)
                idx = (vertCount - 1) * vertCount + i;
                break;
            case TileEdge::South: // Bottom edge (z = 0)
                idx = i;
                break;
            case TileEdge::East: // Right edge (x = vertCount-1)
                idx = i * vertCount + (vertCount - 1);
                break;
            case TileEdge::West: // Left edge (x = 0)
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

    uint32_t TerrainTileGenerator::calculateLOD(
        const glm::vec3& cameraPosition,
        const TerrainTile& tile) const
    {
        glm::vec3 tileCenter = tile.worldBounds.getCenter();
        float distance = glm::length(cameraPosition - tileCenter);

        // Find appropriate LOD level based on distance thresholds
        for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod)
        {
            if (distance < config.lodDistances[lod])
            {
                return lod;
            }
        }

        return TERRAIN_LOD_COUNT - 1; // Lowest detail
    }

    uint32_t TerrainTileGenerator::getLODVertexCount(uint32_t lodLevel) const
    {
        uint32_t baseVerts = config.getVertexCount();
        uint32_t skip = 1u << lodLevel; // 1, 2, 4, 8
        return (baseVerts - 1) / skip + 1;
    }

    uint32_t TerrainTileGenerator::getLODSkipFactor(uint32_t lodLevel) const
    {
        return 1u << lodLevel; // 1, 2, 4, 8
    }
}
