#include "TerrainGPUAdapter.hpp"
#include "terrain/TerrainTile.hpp"
#include "print/EditorLogger.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace render::gpudriven
{
    TerrainGPUAdapter::TerrainGPUAdapter(TerrainMeshBuffer& terrainBuffer)
        : terrainBuffer_(terrainBuffer)
    {
    }

    TerrainGPUAdapter::~TerrainGPUAdapter()
    {
        clear();
    }

    TerrainTileAllocation* TerrainGPUAdapter::uploadTile(const terrain::TerrainTile& tile)
    {
        TerrainTileKey key{tile.coord.x, tile.coord.z};

        // Check if already uploaded
        auto existingIt = allocations_.find(key);
        if (existingIt != allocations_.end())
        {
            return &existingIt->second;
        }

        TerrainTileAllocation alloc;
        alloc.key = key;
        alloc.aabbMin = tile.worldBounds.min;
        alloc.aabbMax = tile.worldBounds.max;

        // Calculate bounding sphere
        glm::vec3 center = (alloc.aabbMin + alloc.aabbMax) * 0.5f;
        float radius = glm::length(alloc.aabbMax - center);
        alloc.boundingSphere = glm::vec4(center, radius);

        // Collect geometry counts for all LODs
        std::array<uint32_t, LOD_LEVEL_COUNT> vertexCounts{};
        std::array<uint32_t, LOD_LEVEL_COUNT> indexCounts{};
        std::array<uint32_t, LOD_LEVEL_COUNT> meshletCounts{};
        std::array<uint32_t, LOD_LEVEL_COUNT> meshletVertexCounts{};
        std::array<uint32_t, LOD_LEVEL_COUNT> meshletPrimitiveCounts{};

        for (uint32_t lod = 0; lod < LOD_LEVEL_COUNT; ++lod)
        {
            const auto& lodData = tile.lodLevels[lod];
            vertexCounts[lod] = static_cast<uint32_t>(lodData.vertices.size());
            indexCounts[lod] = static_cast<uint32_t>(lodData.indices.size());
            meshletCounts[lod] = static_cast<uint32_t>(lodData.meshlets.size());
            meshletVertexCounts[lod] = static_cast<uint32_t>(lodData.meshletVertices.size());
            meshletPrimitiveCounts[lod] = static_cast<uint32_t>(lodData.meshletPrimitives.size());

            // Store geometric error for GPU LOD selection
            alloc.geometricErrors[lod] = lodData.geometricError;
        }

        std::string tileKey = alloc.getMeshPath();

        // Allocate space in TerrainMeshBuffer
        TerrainTileGeometry* tileGeom = terrainBuffer_.allocateTile(
            tileKey,
            vertexCounts,
            indexCounts,
            meshletCounts,
            meshletVertexCounts,
            meshletPrimitiveCounts,
            alloc.aabbMin,
            alloc.aabbMax
        );

        if (!tileGeom)
        {
            vfLogError("TerrainGPUAdapter: Failed to allocate buffer for tile ({}, {})",
                       key.coordX, key.coordZ);
            return nullptr;
        }

        // Upload data for each LOD
        for (uint32_t lod = 0; lod < LOD_LEVEL_COUNT; ++lod)
        {
            if (!uploadLODData(alloc, tile.lodLevels[lod], lod, tile.worldOrigin))
            {
                vfLogError("TerrainGPUAdapter: Failed to upload LOD {} for tile ({}, {})",
                           lod, key.coordX, key.coordZ);
                // Continue with other LODs
            }
        }

        // Copy allocation info from buffer
        for (uint32_t lod = 0; lod < LOD_LEVEL_COUNT; ++lod)
        {
            const auto& geomLod = tileGeom->lods[lod];
            alloc.lodAllocs[lod].vertexOffset = geomLod.vertexOffset;
            alloc.lodAllocs[lod].vertexCount = geomLod.vertexCount;
            alloc.lodAllocs[lod].indexOffset = geomLod.indexOffset;
            alloc.lodAllocs[lod].indexCount = geomLod.indexCount;
            alloc.lodAllocs[lod].meshletOffset = geomLod.meshletOffset;
            alloc.lodAllocs[lod].meshletCount = geomLod.meshletCount;
            alloc.lodAllocs[lod].meshletVertexOffset = geomLod.meshletVertexOffset;
            alloc.lodAllocs[lod].meshletVertexCount = geomLod.meshletVertexCount;
            alloc.lodAllocs[lod].meshletPrimitiveOffset = geomLod.meshletPrimitiveOffset;
            alloc.lodAllocs[lod].meshletPrimitiveCount = geomLod.meshletPrimitiveCount;
            alloc.lodAllocs[lod].isAllocated = geomLod.isAllocated;
        }

        alloc.isUploaded = true;

        auto [it, inserted] = allocations_.emplace(key, std::move(alloc));
        return &it->second;
    }

    TerrainTileAllocation* TerrainGPUAdapter::uploadTileSingleLOD(
        const terrain::TerrainTile& tile, uint32_t lodLevel)
    {
        if (lodLevel >= LOD_LEVEL_COUNT)
        {
            vfLogError("TerrainGPUAdapter: Invalid LOD level {}", lodLevel);
            return nullptr;
        }

        TerrainTileKey key{tile.coord.x, tile.coord.z};

        // Check if already uploaded
        auto existingIt = allocations_.find(key);
        if (existingIt != allocations_.end())
        {
            // Already have this tile - return existing allocation
            return &existingIt->second;
        }

        TerrainTileAllocation alloc;
        alloc.key = key;
        alloc.aabbMin = tile.worldBounds.min;
        alloc.aabbMax = tile.worldBounds.max;

        // Calculate bounding sphere
        glm::vec3 center = (alloc.aabbMin + alloc.aabbMax) * 0.5f;
        float radius = glm::length(alloc.aabbMax - center);
        alloc.boundingSphere = glm::vec4(center, radius);

        // Store geometric errors for all LODs (needed for GPU LOD selection)
        for (uint32_t lod = 0; lod < LOD_LEVEL_COUNT; ++lod)
        {
            alloc.geometricErrors[lod] = tile.lodLevels[lod].geometricError;
        }

        // Only collect counts for the requested LOD - zero for others
        std::array<uint32_t, LOD_LEVEL_COUNT> vertexCounts{};
        std::array<uint32_t, LOD_LEVEL_COUNT> indexCounts{};
        std::array<uint32_t, LOD_LEVEL_COUNT> meshletCounts{};
        std::array<uint32_t, LOD_LEVEL_COUNT> meshletVertexCounts{};
        std::array<uint32_t, LOD_LEVEL_COUNT> meshletPrimitiveCounts{};

        const auto& lodData = tile.lodLevels[lodLevel];
        vertexCounts[lodLevel] = static_cast<uint32_t>(lodData.vertices.size());
        indexCounts[lodLevel] = static_cast<uint32_t>(lodData.indices.size());
        meshletCounts[lodLevel] = static_cast<uint32_t>(lodData.meshlets.size());
        meshletVertexCounts[lodLevel] = static_cast<uint32_t>(lodData.meshletVertices.size());
        meshletPrimitiveCounts[lodLevel] = static_cast<uint32_t>(lodData.meshletPrimitives.size());

        std::string tileKey = alloc.getMeshPath();

        // Allocate space only for the requested LOD
        TerrainTileGeometry* tileGeom = terrainBuffer_.allocateTile(
            tileKey,
            vertexCounts,
            indexCounts,
            meshletCounts,
            meshletVertexCounts,
            meshletPrimitiveCounts,
            alloc.aabbMin,
            alloc.aabbMax
        );

        if (!tileGeom)
        {
            vfLogError("TerrainGPUAdapter: Failed to allocate buffer for tile ({}, {}) LOD {}",
                       key.coordX, key.coordZ, lodLevel);
            return nullptr;
        }

        // Upload only the requested LOD
        if (!uploadLODData(alloc, lodData, lodLevel, tile.worldOrigin))
        {
            vfLogError("TerrainGPUAdapter: Failed to upload LOD {} for tile ({}, {})",
                       lodLevel, key.coordX, key.coordZ);
            terrainBuffer_.freeTile(tileKey);
            return nullptr;
        }

        // Copy allocation info from buffer
        for (uint32_t lod = 0; lod < LOD_LEVEL_COUNT; ++lod)
        {
            const auto& geomLod = tileGeom->lods[lod];
            alloc.lodAllocs[lod].vertexOffset = geomLod.vertexOffset;
            alloc.lodAllocs[lod].vertexCount = geomLod.vertexCount;
            alloc.lodAllocs[lod].indexOffset = geomLod.indexOffset;
            alloc.lodAllocs[lod].indexCount = geomLod.indexCount;
            alloc.lodAllocs[lod].meshletOffset = geomLod.meshletOffset;
            alloc.lodAllocs[lod].meshletCount = geomLod.meshletCount;
            alloc.lodAllocs[lod].meshletVertexOffset = geomLod.meshletVertexOffset;
            alloc.lodAllocs[lod].meshletVertexCount = geomLod.meshletVertexCount;
            alloc.lodAllocs[lod].meshletPrimitiveOffset = geomLod.meshletPrimitiveOffset;
            alloc.lodAllocs[lod].meshletPrimitiveCount = geomLod.meshletPrimitiveCount;
            alloc.lodAllocs[lod].isAllocated = geomLod.isAllocated;
        }

        alloc.isUploaded = true;

        auto [it, inserted] = allocations_.emplace(key, std::move(alloc));
        return &it->second;
    }

    void TerrainGPUAdapter::removeTile(const TerrainTileKey& key)
    {
        auto it = allocations_.find(key);
        if (it == allocations_.end())
        {
            return;
        }

        std::string tileKey = it->second.getMeshPath();

        // Free from buffer
        terrainBuffer_.freeTile(tileKey);

        allocations_.erase(it);
    }

    bool TerrainGPUAdapter::hasTile(const TerrainTileKey& key) const
    {
        return allocations_.find(key) != allocations_.end();
    }

    const TerrainTileAllocation* TerrainGPUAdapter::getAllocation(const TerrainTileKey& key) const
    {
        auto it = allocations_.find(key);
        return (it != allocations_.end()) ? &it->second : nullptr;
    }

    std::vector<mesh::MeshRenderData> TerrainGPUAdapter::buildRenderData(
        const std::vector<terrain::TerrainTile*>& tiles,
        entt::entity terrainEntity) const
    {
        std::vector<mesh::MeshRenderData> result;
        result.reserve(tiles.size());

        for (const terrain::TerrainTile* tile : tiles)
        {
            if (!tile || !tile->isVisible)
            {
                continue;
            }

            TerrainTileKey key{tile->coord.x, tile->coord.z};
            auto it = allocations_.find(key);
            if (it == allocations_.end() || !it->second.isUploaded)
            {
                continue;
            }

            const auto& alloc = it->second;

            mesh::MeshRenderData renderData;
            renderData.entity = terrainEntity;
            renderData.meshPath = alloc.getMeshPath();
            renderData.modelMatrix = glm::mat4(1.0f); // Identity - terrain uses world-space vertices
            renderData.albedo = glm::vec4(1.0f);
            renderData.metallic = 0.0f;
            renderData.roughness = 0.8f;
            renderData.ao = 1.0f;
            renderData.emission = 0.0f;

            result.push_back(std::move(renderData));
        }

        return result;
    }

    void TerrainGPUAdapter::clear()
    {
        for (auto& [key, alloc] : allocations_)
        {
            std::string tileKey = alloc.getMeshPath();
            terrainBuffer_.freeTile(tileKey);
        }
        allocations_.clear();
    }

    bool TerrainGPUAdapter::uploadLODData(
        TerrainTileAllocation& alloc,
        const terrain::TileLODData& lodData,
        uint32_t lodLevel,
        const glm::vec3& worldOrigin)
    {
        if (lodData.vertices.empty())
        {
            return true; // Empty LOD is OK
        }

        std::string tileKey = alloc.getMeshPath();

        // Transform vertices from tile-local space to world space
        std::vector<resource::Vertex> worldSpaceVertices = lodData.vertices;
        for (auto& vertex : worldSpaceVertices)
        {
            vertex.position.x += worldOrigin.x;
            // vertex.position.y is already the absolute height - leave as-is
            vertex.position.z += worldOrigin.z;
        }

        // Upload vertex data
        if (!terrainBuffer_.uploadLODVertices(tileKey, lodLevel,
                                               worldSpaceVertices.data(),
                                               static_cast<uint32_t>(worldSpaceVertices.size())))
        {
            return false;
        }

        // Upload index data
        if (!terrainBuffer_.uploadLODIndices(tileKey, lodLevel,
                                              lodData.indices.data(),
                                              static_cast<uint32_t>(lodData.indices.size())))
        {
            return false;
        }

        // Upload meshlet data if available
        if (!lodData.meshlets.empty())
        {
            // Get the allocation to determine base vertex offset
            const TerrainTileGeometry* tileGeom = terrainBuffer_.getTileGeometry(tileKey);
            if (!tileGeom)
            {
                return false;
            }

            uint32_t baseVertexOffset = tileGeom->lods[lodLevel].vertexOffset;
            uint32_t meshletVertexOffset = tileGeom->lods[lodLevel].meshletVertexOffset;
            uint32_t meshletPrimitiveOffset = tileGeom->lods[lodLevel].meshletPrimitiveOffset;

            // Convert meshlets to GPU format with world-space bounding spheres
            std::vector<GPUMeshlet> gpuMeshlets;
            gpuMeshlets.reserve(lodData.meshlets.size());

            for (const auto& srcMeshlet : lodData.meshlets)
            {
                GPUMeshlet meshlet = convertMeshlet(srcMeshlet, baseVertexOffset);

                // Transform bounding sphere center from local space to world space
                meshlet.boundingSphere.x += worldOrigin.x;
                meshlet.boundingSphere.z += worldOrigin.z;

                // Update offsets to use allocated positions
                meshlet.vertexOffset += meshletVertexOffset;
                meshlet.primitiveOffset += meshletPrimitiveOffset;

                gpuMeshlets.push_back(meshlet);
            }

            if (!terrainBuffer_.uploadLODMeshlets(tileKey, lodLevel,
                                                   gpuMeshlets.data(),
                                                   static_cast<uint32_t>(gpuMeshlets.size()),
                                                   lodData.meshletVertices.data(),
                                                   static_cast<uint32_t>(lodData.meshletVertices.size()),
                                                   lodData.meshletPrimitives.data(),
                                                   static_cast<uint32_t>(lodData.meshletPrimitives.size())))
            {
                return false;
            }
        }

        return true;
    }

    GPUMeshlet TerrainGPUAdapter::convertMeshlet(
        const resource::Meshlet& srcMeshlet,
        uint32_t globalVertexOffset) const
    {
        GPUMeshlet dst{};
        dst.vertexOffset = srcMeshlet.descriptor.vertexOffset;
        dst.primitiveOffset = srcMeshlet.descriptor.primitiveOffset;
        dst.vertexCount = srcMeshlet.descriptor.vertexCount;
        dst.primitiveCount = srcMeshlet.descriptor.primitiveCount;
        dst.padding0 = 0;
        dst.globalVertexOffset = globalVertexOffset;
        dst.boundingSphere = srcMeshlet.bounds.boundingSphere;
        dst.cone = srcMeshlet.bounds.cone;
        return dst;
    }

    std::vector<TerrainTileGPUData> TerrainGPUAdapter::buildGPUTileData(
        const std::vector<terrain::TerrainTile*>& tiles) const
    {
        std::vector<TerrainTileGPUData> result;
        result.reserve(tiles.size());

        for (const terrain::TerrainTile* tile : tiles)
        {
            if (!tile || !tile->isVisible)
            {
                continue;
            }

            TerrainTileKey key{tile->coord.x, tile->coord.z};
            auto it = allocations_.find(key);
            if (it == allocations_.end() || !it->second.isUploaded)
            {
                continue;
            }

            const auto& alloc = it->second;

            TerrainTileGPUData gpuTile{};

            // Model matrix - identity for world-space terrain
            gpuTile.modelMatrix = glm::mat4(1.0f);

            // Bounding volumes
            gpuTile.boundingSphere = alloc.boundingSphere;
            gpuTile.aabbMin = glm::vec4(alloc.aabbMin, 0.0f);
            gpuTile.aabbMax = glm::vec4(alloc.aabbMax, 0.0f);

            // LOD meshlet data for each level
            // Format: x = meshletOffset, y = meshletCount, z = baseVertexOffset, w = unused
            gpuTile.lod0MeshletData = glm::uvec4(
                alloc.lodAllocs[0].meshletOffset,
                alloc.lodAllocs[0].meshletCount,
                alloc.lodAllocs[0].vertexOffset,
                0
            );
            gpuTile.lod1MeshletData = glm::uvec4(
                alloc.lodAllocs[1].meshletOffset,
                alloc.lodAllocs[1].meshletCount,
                alloc.lodAllocs[1].vertexOffset,
                0
            );
            gpuTile.lod2MeshletData = glm::uvec4(
                alloc.lodAllocs[2].meshletOffset,
                alloc.lodAllocs[2].meshletCount,
                alloc.lodAllocs[2].vertexOffset,
                0
            );
            gpuTile.lod3MeshletData = glm::uvec4(
                alloc.lodAllocs[3].meshletOffset,
                alloc.lodAllocs[3].meshletCount,
                alloc.lodAllocs[3].vertexOffset,
                0
            );

            // Geometric errors per LOD level
            gpuTile.lodGeometricErrors = glm::vec4(
                alloc.geometricErrors[0],
                alloc.geometricErrors[1],
                alloc.geometricErrors[2],
                alloc.geometricErrors[3]
            );

            // Tile coordinates
            gpuTile.coordX = key.coordX;
            gpuTile.coordZ = key.coordZ;

            // Flags - mark as terrain tile
            gpuTile.flags = ObjectFlags::TerrainTile;

            // Material index - default for now
            gpuTile.materialIndex = 0;

            result.push_back(gpuTile);
        }

        return result;
    }
}
