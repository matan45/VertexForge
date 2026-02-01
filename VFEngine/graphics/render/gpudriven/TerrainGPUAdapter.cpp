#include "TerrainGPUAdapter.hpp"
#include "MergedMeshBuffer.hpp"
#include "MeshletBuffer.hpp"
#include "terrain/TerrainTile.hpp"
#include "print/EditorLogger.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace render::gpudriven
{
    TerrainGPUAdapter::TerrainGPUAdapter(MergedMeshBuffer& mergedBuffer, MeshletBuffer& meshletBuffer)
        : mergedBuffer_(mergedBuffer)
        , meshletBuffer_(meshletBuffer)
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

        // Allocate vertex/index space in MergedMeshBuffer
        SubmeshLocation* meshLoc = mergedBuffer_.allocateTerrainTile(
            tileKey,
            vertexCounts,
            indexCounts,
            alloc.aabbMin,
            alloc.aabbMax
        );

        if (!meshLoc)
        {
            vfLogError("TerrainGPUAdapter: Failed to allocate mesh buffer for tile ({}, {})",
                       key.coordX, key.coordZ);
            return nullptr;
        }

        // Allocate meshlet space in MeshletBuffer
        MeshletAllocation* meshletAlloc = meshletBuffer_.allocateTerrainTile(
            tileKey,
            meshletCounts,
            meshletVertexCounts,
            meshletPrimitiveCounts
        );

        if (!meshletAlloc)
        {
            mergedBuffer_.freeTerrainTile(tileKey);
            vfLogError("TerrainGPUAdapter: Failed to allocate meshlet buffer for tile ({}, {})",
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

        // Copy allocation info from buffers
        for (uint32_t lod = 0; lod < LOD_LEVEL_COUNT; ++lod)
        {
            alloc.lodAllocs[lod].vertexOffset = meshLoc->lods[lod].vertexOffset;
            alloc.lodAllocs[lod].vertexCount = meshLoc->lods[lod].vertexCount;
            alloc.lodAllocs[lod].indexOffset = meshLoc->lods[lod].indexOffset;
            alloc.lodAllocs[lod].indexCount = meshLoc->lods[lod].indexCount;

            if (meshletAlloc->lods[lod].isAllocated)
            {
                alloc.lodAllocs[lod].meshletOffset = meshletAlloc->lods[lod].meshletOffset;
                alloc.lodAllocs[lod].meshletCount = meshletAlloc->lods[lod].meshletCount;
                alloc.lodAllocs[lod].meshletVertexOffset = meshletAlloc->lods[lod].vertexOffset;
                alloc.lodAllocs[lod].meshletVertexCount = meshletAlloc->lods[lod].vertexCount;
                alloc.lodAllocs[lod].meshletPrimitiveOffset = meshletAlloc->lods[lod].primitiveOffset;
                alloc.lodAllocs[lod].meshletPrimitiveCount = meshletAlloc->lods[lod].primitiveCount;
                alloc.lodAllocs[lod].isAllocated = true;
            }
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

        // Free from both buffers
        mergedBuffer_.freeTerrainTile(tileKey);
        meshletBuffer_.freeTerrainTile(tileKey);

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
            mergedBuffer_.freeTerrainTile(tileKey);
            meshletBuffer_.freeTerrainTile(tileKey);
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

        // Upload vertex/index data to MergedMeshBuffer
        bool meshSuccess = mergedBuffer_.uploadTerrainLOD(
            tileKey,
            lodLevel,
            lodData.vertices.data(),
            static_cast<uint32_t>(lodData.vertices.size()),
            lodData.indices.data(),
            static_cast<uint32_t>(lodData.indices.size())
        );

        if (!meshSuccess)
        {
            return false;
        }

        // Upload meshlet data if available
        if (!lodData.meshlets.empty())
        {
            // Get the allocation to determine base vertex offset
            const SubmeshLocation* meshLoc = mergedBuffer_.getSubmeshLocation(tileKey, "terrain", 0);
            if (!meshLoc)
            {
                return false;
            }

            uint32_t baseVertexOffset = meshLoc->lods[lodLevel].vertexOffset;

            // Convert meshlets to GPU format
            std::vector<GPUMeshlet> gpuMeshlets;
            gpuMeshlets.reserve(lodData.meshlets.size());

            for (const auto& srcMeshlet : lodData.meshlets)
            {
                gpuMeshlets.push_back(convertMeshlet(srcMeshlet, baseVertexOffset));
            }

            // Get meshlet allocation to determine offsets
            const MeshletAllocation* meshletAlloc = meshletBuffer_.getAllocation(tileKey, "terrain", 0);
            if (!meshletAlloc || !meshletAlloc->lods[lodLevel].isAllocated)
            {
                return false;
            }

            const auto& lodAlloc = meshletAlloc->lods[lodLevel];

            // Offset vertex indices and primitive indices for this LOD's allocation
            std::vector<uint32_t> vertexIndices = lodData.meshletVertices;
            std::vector<uint32_t> primitives = lodData.meshletPrimitives;

            // Update meshlet offsets to use allocated positions
            for (size_t i = 0; i < gpuMeshlets.size(); ++i)
            {
                gpuMeshlets[i].vertexOffset += lodAlloc.vertexOffset;
                gpuMeshlets[i].primitiveOffset += lodAlloc.primitiveOffset;
            }

            bool meshletSuccess = meshletBuffer_.uploadTerrainMeshletLOD(
                tileKey,
                lodLevel,
                gpuMeshlets,
                vertexIndices,
                primitives
            );

            if (!meshletSuccess)
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

            // Material index - default for now, will be set by terrain material system (VK-178)
            gpuTile.materialIndex = 0;

            result.push_back(gpuTile);
        }

        return result;
    }
}
