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

        if (progress)
            progress(0.2f, "Generating LODs");

        // Generate all LOD levels
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

        // Generate vertices
        generateVertices(lodData.vertices, tile, lodLevel);

        // Generate indices
        generateIndices(lodData.indices, lodLevel);

        // Calculate normals with cross-boundary ghost row data
        uint32_t vertCount = getLODVertexCount(lodLevel);
        uint32_t skipFactor = getLODSkipFactor(lodLevel);
        float spacing = config.getVertexSpacing() * static_cast<float>(skipFactor);
        TileNeighborContext neighborCtx = collectNeighborContext(tile, lodLevel, getTile);
        calculateNormals(lodData.vertices, lodData.indices, vertCount, spacing, neighborCtx);

        // Generate skirts for LOD crack prevention
        if (config.skirtDepth > 0.0f)
        {
            generateSkirts(lodData.vertices, lodData.indices, lodLevel, config.skirtDepth);
        }

        // Calculate bounds
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
        uint32_t skipFactor = getLODSkipFactor(lodLevel);
        float spacing = config.getVertexSpacing() * static_cast<float>(skipFactor);
        TileNeighborContext neighborCtx = collectNeighborContext(tile, lodLevel, getTile);
        calculateNormals(lodData.vertices, lodData.indices, vertCount, spacing, neighborCtx);

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

    void TerrainTileGenerator::updateMeshletBounds(TileLODData& lodData) const
    {
        if (lodData.meshlets.empty() || lodData.vertices.empty())
            return;

        // Temporary buffer for unpacking triangle indices
        std::vector<unsigned char> triangleIndices;
        triangleIndices.reserve(resource::MAX_MESHLET_PRIMITIVES * 3);

        for (auto& meshlet : lodData.meshlets)
        {
            uint32_t vertexOffset = meshlet.descriptor.vertexOffset;
            uint32_t primitiveOffset = meshlet.descriptor.primitiveOffset;
            uint32_t primitiveCount = meshlet.descriptor.primitiveCount;

            // Unpack triangle indices from packed uint32_t format
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
        {
            vfLogWarning("TerrainTileGenerator: meshopt_buildMeshlets returned 0 meshlets for {} vertices, {} indices",
                         lodData.vertices.size(), lodData.indices.size());
            return;
        }

        // 4. Trim arrays to actual size
        const auto& lastMeshlet = meshoptMeshlets[meshletCount - 1];
        size_t totalVertexIndices = lastMeshlet.vertex_offset + lastMeshlet.vertex_count;
        size_t totalTriangleIndices = lastMeshlet.triangle_offset +
            ((lastMeshlet.triangle_count * 3 + 3) & ~3); // Round up to 4-byte alignment

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

    TileNeighborContext TerrainTileGenerator::collectNeighborContext(
        const TerrainTile& tile,
        uint32_t lodLevel,
        const TileLookup& getTile) const
    {
        TileNeighborContext ctx;

        if (!getTile)
            return ctx;

        uint32_t vertCount = getLODVertexCount(lodLevel);
        uint32_t skipFactor = getLODSkipFactor(lodLevel);
        uint32_t baseVertCount = config.getVertexCount();

        // North neighbor: tile's z=max corresponds to neighbor's z=0
        // We need neighbor's first interior row (z = 1 * skipFactor)
        TileCoord northCoord = tile.coord + TileCoord::getNeighborOffset(TileEdge::North);
        const TerrainTile* northTile = getTile(northCoord);
        if (northTile)
        {
            ctx.edges[0].heights.resize(vertCount);
            ctx.edges[0].available = true;
            uint32_t neighborZ = std::min(1u * skipFactor, baseVertCount - 1);
            for (uint32_t i = 0; i < vertCount; ++i)
            {
                uint32_t neighborX = std::min(i * skipFactor, baseVertCount - 1);
                ctx.edges[0].heights[i] = northTile->getHeight(neighborX, neighborZ);
            }
        }

        // East neighbor: tile's x=max corresponds to neighbor's x=0
        // We need neighbor's first interior column (x = 1 * skipFactor)
        TileCoord eastCoord = tile.coord + TileCoord::getNeighborOffset(TileEdge::East);
        const TerrainTile* eastTile = getTile(eastCoord);
        if (eastTile)
        {
            ctx.edges[1].heights.resize(vertCount);
            ctx.edges[1].available = true;
            uint32_t neighborX = std::min(1u * skipFactor, baseVertCount - 1);
            for (uint32_t i = 0; i < vertCount; ++i)
            {
                uint32_t neighborZ = std::min(i * skipFactor, baseVertCount - 1);
                ctx.edges[1].heights[i] = eastTile->getHeight(neighborX, neighborZ);
            }
        }

        // South neighbor: tile's z=0 corresponds to neighbor's z=max
        // We need neighbor's last interior row (z = (baseVertCount-1) - 1*skipFactor)
        TileCoord southCoord = tile.coord + TileCoord::getNeighborOffset(TileEdge::South);
        const TerrainTile* southTile = getTile(southCoord);
        if (southTile)
        {
            ctx.edges[2].heights.resize(vertCount);
            ctx.edges[2].available = true;
            uint32_t neighborZ = (baseVertCount - 1) >= skipFactor
                ? (baseVertCount - 1) - skipFactor : 0;
            for (uint32_t i = 0; i < vertCount; ++i)
            {
                uint32_t neighborX = std::min(i * skipFactor, baseVertCount - 1);
                ctx.edges[2].heights[i] = southTile->getHeight(neighborX, neighborZ);
            }
        }

        // West neighbor: tile's x=0 corresponds to neighbor's x=max
        // We need neighbor's last interior column (x = (baseVertCount-1) - 1*skipFactor)
        TileCoord westCoord = tile.coord + TileCoord::getNeighborOffset(TileEdge::West);
        const TerrainTile* westTile = getTile(westCoord);
        if (westTile)
        {
            ctx.edges[3].heights.resize(vertCount);
            ctx.edges[3].available = true;
            uint32_t neighborX = (baseVertCount - 1) >= skipFactor
                ? (baseVertCount - 1) - skipFactor : 0;
            for (uint32_t i = 0; i < vertCount; ++i)
            {
                uint32_t neighborZ = std::min(i * skipFactor, baseVertCount - 1);
                ctx.edges[3].heights[i] = westTile->getHeight(neighborX, neighborZ);
            }
        }

        return ctx;
    }

    void TerrainTileGenerator::calculateNormals(
        std::vector<resource::Vertex>& vertices,
        const std::vector<uint32_t>& indices,
        uint32_t vertCount,
        float spacing,
        const TileNeighborContext& neighborCtx) const
    {
        if (vertices.empty() || indices.empty())
            return;

        // Reset all normals
        for (auto& v : vertices)
        {
            v.normal = glm::vec3(0.0f);
        }

        // Phase 1: Accumulate face normals from tile's own triangles
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

        // Phase 2: Add ghost triangle contributions for edge vertices.
        // For each edge with neighbor data, construct virtual quads between
        // the tile's edge vertices and the neighbor's first interior row,
        // then accumulate their face normals onto edge vertices only.

        // North edge (z = vertCount-1): ghost extends in +Z direction
        if (neighborCtx.edges[0].available)
        {
            uint32_t z = vertCount - 1;
            float ghostZ = static_cast<float>(z + 1) * spacing;

            for (uint32_t x = 0; x < vertCount - 1; ++x)
            {
                uint32_t idxBL = z * vertCount + x;
                uint32_t idxBR = z * vertCount + (x + 1);

                glm::vec3 pBL = vertices[idxBL].position;
                glm::vec3 pBR = vertices[idxBR].position;

                glm::vec3 pTL(static_cast<float>(x) * spacing,
                              neighborCtx.edges[0].heights[x], ghostZ);
                glm::vec3 pTR(static_cast<float>(x + 1) * spacing,
                              neighborCtx.edges[0].heights[x + 1], ghostZ);

                // Ghost triangle 1: BL -> TL -> BR
                glm::vec3 fn1 = glm::cross(pTL - pBL, pBR - pBL);
                // Ghost triangle 2: BR -> TL -> TR
                glm::vec3 fn2 = glm::cross(pTL - pBR, pTR - pBR);

                vertices[idxBL].normal += fn1;
                vertices[idxBR].normal += fn1 + fn2;
            }
        }

        // East edge (x = vertCount-1): ghost extends in +X direction
        if (neighborCtx.edges[1].available)
        {
            uint32_t x = vertCount - 1;
            float ghostX = static_cast<float>(x + 1) * spacing;

            for (uint32_t z = 0; z < vertCount - 1; ++z)
            {
                uint32_t idxBL = z * vertCount + x;
                uint32_t idxTL = (z + 1) * vertCount + x;

                glm::vec3 pBL = vertices[idxBL].position;
                glm::vec3 pTL = vertices[idxTL].position;

                glm::vec3 pBR(ghostX, neighborCtx.edges[1].heights[z],
                              static_cast<float>(z) * spacing);
                glm::vec3 pTR(ghostX, neighborCtx.edges[1].heights[z + 1],
                              static_cast<float>(z + 1) * spacing);

                // Ghost triangle 1: BL -> BR -> TL
                glm::vec3 fn1 = glm::cross(pBR - pBL, pTL - pBL);
                // Ghost triangle 2: TL -> BR -> TR
                glm::vec3 fn2 = glm::cross(pBR - pTL, pTR - pTL);

                vertices[idxBL].normal += fn1;
                vertices[idxTL].normal += fn1 + fn2;
            }
        }

        // South edge (z = 0): ghost extends in -Z direction
        if (neighborCtx.edges[2].available)
        {
            uint32_t z = 0;
            float ghostZ = -spacing;

            for (uint32_t x = 0; x < vertCount - 1; ++x)
            {
                uint32_t idxTL = z * vertCount + x;
                uint32_t idxTR = z * vertCount + (x + 1);

                glm::vec3 pTL = vertices[idxTL].position;
                glm::vec3 pTR = vertices[idxTR].position;

                glm::vec3 pBL(static_cast<float>(x) * spacing,
                              neighborCtx.edges[2].heights[x], ghostZ);
                glm::vec3 pBR(static_cast<float>(x + 1) * spacing,
                              neighborCtx.edges[2].heights[x + 1], ghostZ);

                // Ghost triangle 1: TL -> TR -> BL
                glm::vec3 fn1 = glm::cross(pTR - pTL, pBL - pTL);
                // Ghost triangle 2: TR -> BR -> BL
                glm::vec3 fn2 = glm::cross(pBR - pTR, pBL - pTR);

                vertices[idxTL].normal += fn1;
                vertices[idxTR].normal += fn1 + fn2;
            }
        }

        // West edge (x = 0): ghost extends in -X direction
        if (neighborCtx.edges[3].available)
        {
            uint32_t x = 0;
            float ghostX = -spacing;

            for (uint32_t z = 0; z < vertCount - 1; ++z)
            {
                uint32_t idxBR = z * vertCount + x;
                uint32_t idxTR = (z + 1) * vertCount + x;

                glm::vec3 pBR = vertices[idxBR].position;
                glm::vec3 pTR = vertices[idxTR].position;

                glm::vec3 pBL(ghostX, neighborCtx.edges[3].heights[z],
                              static_cast<float>(z) * spacing);
                glm::vec3 pTL(ghostX, neighborCtx.edges[3].heights[z + 1],
                              static_cast<float>(z + 1) * spacing);

                // Ghost triangle 1: BR -> TR -> BL
                glm::vec3 fn1 = glm::cross(pTR - pBR, pBL - pBR);
                // Ghost triangle 2: TR -> TL -> BL
                glm::vec3 fn2 = glm::cross(pTL - pTR, pBL - pTR);

                vertices[idxBR].normal += fn1;
                vertices[idxTR].normal += fn1 + fn2;
            }
        }

        // Phase 3: Normalize all normals
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

    float TerrainTileGenerator::computeGeometricError(
        const TerrainTile& tile, uint32_t lodLevel) const
    {
        if (lodLevel == 0)
            return 0.0f; // Highest detail has no error

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

                // Find the enclosing quad in the previous LOD grid
                uint32_t prevX0 = (x / prevSkip) * prevSkip;
                uint32_t prevZ0 = (z / prevSkip) * prevSkip;
                uint32_t prevX1 = std::min(prevX0 + prevSkip, baseVertCount - 1);
                uint32_t prevZ1 = std::min(prevZ0 + prevSkip, baseVertCount - 1);

                // Compute interpolation factors
                float fx = (prevX1 != prevX0)
                               ? static_cast<float>(x - prevX0) / static_cast<float>(prevX1 - prevX0)
                               : 0.0f;
                float fz = (prevZ1 != prevZ0)
                               ? static_cast<float>(z - prevZ0) / static_cast<float>(prevZ1 - prevZ0)
                               : 0.0f;

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

        // For flat terrain (error = 0), use vertex spacing as minimum error
        // This ensures LOD selection works based on distance even for flat terrain
        float vertexSpacing = config.getVertexSpacing() * static_cast<float>(thisSkip);
        float minError = vertexSpacing * 0.5f; // Half the vertex spacing at this LOD

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

        // Store metadata only - actual snapped heights are computed inline
        // in getStitchedHeight() from tile.heightData for per-LOD correctness
        info.needsSnapping = true;
        info.neighborLOD = neighborLOD;
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

    bool TerrainTileGenerator::isEdgeVertex(uint32_t x, uint32_t z, uint32_t vertCount) const
    {
        return x == 0 || x == vertCount - 1 || z == 0 || z == vertCount - 1;
    }

    bool TerrainTileGenerator::isCornerVertex(uint32_t x, uint32_t z, uint32_t vertCount) const
    {
        bool onXEdge = (x == 0 || x == vertCount - 1);
        bool onZEdge = (z == 0 || z == vertCount - 1);
        return onXEdge && onZEdge;
    }

    TileEdge TerrainTileGenerator::getEdgeForVertex(uint32_t x, uint32_t z, uint32_t vertCount) const
    {
        // Priority: South > North > West > East (for non-corner vertices)
        // For corners, this returns the primary edge
        if (z == 0) return TileEdge::South;
        if (z == vertCount - 1) return TileEdge::North;
        if (x == 0) return TileEdge::West;
        return TileEdge::East;
    }

    uint32_t TerrainTileGenerator::getEdgeVertexIndex(uint32_t x, uint32_t z, uint32_t vertCount, TileEdge edge) const
    {
        switch (edge)
        {
        case TileEdge::North: return x; // Top row: index by x
        case TileEdge::South: return x; // Bottom row: index by x
        case TileEdge::East: return z; // Right column: index by z
        case TileEdge::West: return z; // Left column: index by z
        default: return 0;
        }
    }

    float TerrainTileGenerator::getStitchedHeight(
        const TerrainTile& tile,
        uint32_t x, uint32_t z,
        uint32_t vertCount,
        uint32_t lodLevel) const
    {
        // Get original height from heightData (always current after syncTileEdges)
        uint32_t skipFactor = getLODSkipFactor(lodLevel);
        uint32_t heightX = x * skipFactor;
        uint32_t heightZ = z * skipFactor;
        float originalHeight = tile.getHeight(heightX, heightZ);

        if (!isEdgeVertex(x, z, vertCount))
        {
            return originalHeight;
        }

        uint32_t baseVertCount = config.getVertexCount();

        // Compute snapped height for one edge by sampling this tile's own heightData
        // at the neighbor's coarser LOD grid positions along the shared boundary.
        // Since syncTileEdges() ensures identical boundary heights between tiles,
        // we can sample from our own heightData instead of the neighbor's.
        auto snapForEdge = [&](TileEdge edge, uint32_t edgeIdx) -> std::pair<bool, float>
        {
            const NeighborInfo& ni = tile.neighbors[static_cast<uint8_t>(edge)];
            if (!ni.exists)
                return {false, 0.0f};

            // Per-LOD check: only snap if neighbor is coarser than the LOD being generated
            if (ni.lodLevel <= lodLevel)
                return {false, 0.0f};

            uint32_t neighborSkip = getLODSkipFactor(ni.lodLevel);
            uint32_t neighborVertCount = getLODVertexCount(ni.lodLevel);

            if (neighborVertCount < 2)
                return {false, 0.0f};

            // Map this LOD's edge vertex to the neighbor's coarser grid
            float ratio = static_cast<float>(neighborVertCount - 1)
                        / static_cast<float>(vertCount - 1);
            float nIdx = static_cast<float>(edgeIdx) * ratio;
            uint32_t j0 = static_cast<uint32_t>(nIdx);
            uint32_t j1 = std::min(j0 + 1, neighborVertCount - 1);
            float t = nIdx - static_cast<float>(j0);

            // Sample heights along the shared boundary at the coarser grid positions
            uint32_t pos0 = std::min(j0 * neighborSkip, baseVertCount - 1);
            uint32_t pos1 = std::min(j1 * neighborSkip, baseVertCount - 1);

            float h0, h1;
            switch (edge)
            {
            case TileEdge::North: // z = max, boundary row
                h0 = tile.getHeight(pos0, baseVertCount - 1);
                h1 = tile.getHeight(pos1, baseVertCount - 1);
                break;
            case TileEdge::South: // z = 0, boundary row
                h0 = tile.getHeight(pos0, 0);
                h1 = tile.getHeight(pos1, 0);
                break;
            case TileEdge::East: // x = max, boundary column
                h0 = tile.getHeight(baseVertCount - 1, pos0);
                h1 = tile.getHeight(baseVertCount - 1, pos1);
                break;
            case TileEdge::West: // x = 0, boundary column
                h0 = tile.getHeight(0, pos0);
                h1 = tile.getHeight(0, pos1);
                break;
            default:
                return {false, 0.0f};
            }

            return {true, glm::mix(h0, h1, t)};
        };

        // Handle corner vertices (lie on two edges)
        if (isCornerVertex(x, z, vertCount))
        {
            TileEdge edge1, edge2;
            uint32_t idx1, idx2;

            if (z == 0) // South edge
            {
                edge1 = TileEdge::South;
                idx1 = x;
                edge2 = (x == 0) ? TileEdge::West : TileEdge::East;
                idx2 = 0;
            }
            else // North edge (z == vertCount - 1)
            {
                edge1 = TileEdge::North;
                idx1 = x;
                edge2 = (x == 0) ? TileEdge::West : TileEdge::East;
                idx2 = vertCount - 1;
            }

            auto [needs1, snap1] = snapForEdge(edge1, idx1);
            auto [needs2, snap2] = snapForEdge(edge2, idx2);

            if (needs1 && needs2)
                return (snap1 + snap2) * 0.5f;
            if (needs1)
                return snap1;
            if (needs2)
                return snap2;

            return originalHeight;
        }

        // Regular edge vertex (not a corner)
        TileEdge edge = getEdgeForVertex(x, z, vertCount);
        uint32_t edgeIdx = getEdgeVertexIndex(x, z, vertCount, edge);
        auto [needs, snapped] = snapForEdge(edge, edgeIdx);

        return needs ? snapped : originalHeight;
    }
}
