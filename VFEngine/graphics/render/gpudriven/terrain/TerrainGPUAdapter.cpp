#include "print/Log.hpp"
#include "TerrainGPUAdapter.hpp"
#include "terrain/TerrainTile.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cstring>

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

        auto existingIt = allocations_.find(key);
        if (existingIt != allocations_.end())
        {
            return &existingIt->second;
        }

        TerrainTileAllocation alloc;
        alloc.key = key;
        alloc.aabbMin = tile.worldBounds.min;
        alloc.aabbMax = tile.worldBounds.max;

        glm::vec3 center = (alloc.aabbMin + alloc.aabbMax) * 0.5f;
        float radius = glm::length(alloc.aabbMax - center);
        alloc.boundingSphere = glm::vec4(center, radius);

        std::array<uint32_t, TERRAIN_LOD_LEVEL_COUNT> vertexCounts{};
        std::array<uint32_t, TERRAIN_LOD_LEVEL_COUNT> indexCounts{};
        std::array<uint32_t, TERRAIN_LOD_LEVEL_COUNT> meshletCounts{};
        std::array<uint32_t, TERRAIN_LOD_LEVEL_COUNT> meshletVertexCounts{};
        std::array<uint32_t, TERRAIN_LOD_LEVEL_COUNT> meshletPrimitiveCounts{};

        for (uint32_t lod = 0; lod < TERRAIN_LOD_LEVEL_COUNT; ++lod)
        {
            const auto& lodData = tile.lodLevels[lod];
            vertexCounts[lod] = static_cast<uint32_t>(lodData.vertices.size());
            indexCounts[lod] = static_cast<uint32_t>(lodData.indices.size());
            meshletCounts[lod] = static_cast<uint32_t>(lodData.meshlets.size());
            meshletVertexCounts[lod] = static_cast<uint32_t>(lodData.meshletVertices.size());
            meshletPrimitiveCounts[lod] = static_cast<uint32_t>(lodData.meshletPrimitives.size());

            alloc.geometricErrors[lod] = lodData.geometricError;
        }

        std::string tileKey = alloc.getMeshPath();

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

        for (uint32_t lod = 0; lod < TERRAIN_LOD_LEVEL_COUNT; ++lod)
        {
            if (!uploadLODData(alloc, tile.lodLevels[lod], lod, tile.worldOrigin))
            {
                vfLogError("TerrainGPUAdapter: Failed to upload LOD {} for tile ({}, {})",
                           lod, key.coordX, key.coordZ);
                // Continue with other LODs
            }
        }

        for (uint32_t lod = 0; lod < TERRAIN_LOD_LEVEL_COUNT; ++lod)
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

    bool TerrainGPUAdapter::uploadTileAddLOD(const terrain::TerrainTile& tile, uint32_t lodLevel)
    {
        if (lodLevel >= TERRAIN_LOD_LEVEL_COUNT)
        {
            vfLogError("TerrainGPUAdapter: Invalid LOD level {}", lodLevel);
            return false;
        }

        TerrainTileKey key{tile.coord.x, tile.coord.z};
        std::string tileKeyStr = "terrain_" + std::to_string(key.coordX) + "_" + std::to_string(key.coordZ);

        const auto& lodData = tile.lodLevels[lodLevel];
        if (lodData.isEmpty())
        {
            return true; // Empty LOD is considered success
        }

        if (!terrainBuffer_.allocateTileLOD(
                tileKeyStr,
                lodLevel,
                static_cast<uint32_t>(lodData.vertices.size()),
                static_cast<uint32_t>(lodData.indices.size()),
                static_cast<uint32_t>(lodData.meshlets.size()),
                static_cast<uint32_t>(lodData.meshletVertices.size()),
                static_cast<uint32_t>(lodData.meshletPrimitives.size()),
                tile.worldBounds.min,
                tile.worldBounds.max))
        {
            vfLogError("TerrainGPUAdapter: Failed to allocate LOD {} for tile ({}, {})",
                       lodLevel, key.coordX, key.coordZ);
            return false;
        }

        auto it = allocations_.find(key);
        if (it == allocations_.end())
        {
            TerrainTileAllocation alloc;
            alloc.key = key;

            auto [insertIt, success] = allocations_.emplace(key, std::move(alloc));
            it = insertIt;
        }

        // Always refresh bounds and geometric errors from tile (may have changed due to sculpting)
        it->second.aabbMin = tile.worldBounds.min;
        it->second.aabbMax = tile.worldBounds.max;

        glm::vec3 center = (it->second.aabbMin + it->second.aabbMax) * 0.5f;
        float radius = glm::length(it->second.aabbMax - center);
        it->second.boundingSphere = glm::vec4(center, radius);

        for (uint32_t lod = 0; lod < TERRAIN_LOD_LEVEL_COUNT; ++lod)
        {
            it->second.geometricErrors[lod] = tile.lodLevels[lod].geometricError;
        }

        if (!uploadLODData(it->second, lodData, lodLevel, tile.worldOrigin))
        {
            vfLogError("TerrainGPUAdapter: Failed to upload LOD {} data for tile ({}, {})",
                       lodLevel, key.coordX, key.coordZ);
            terrainBuffer_.freeTileLOD(tileKeyStr, lodLevel);
            return false;
        }

        const TerrainTileGeometry* tileGeom = terrainBuffer_.getTileGeometry(tileKeyStr);
        if (tileGeom)
        {
            const auto& geomLod = tileGeom->lods[lodLevel];
            it->second.lodAllocs[lodLevel].vertexOffset = geomLod.vertexOffset;
            it->second.lodAllocs[lodLevel].vertexCount = geomLod.vertexCount;
            it->second.lodAllocs[lodLevel].indexOffset = geomLod.indexOffset;
            it->second.lodAllocs[lodLevel].indexCount = geomLod.indexCount;
            it->second.lodAllocs[lodLevel].meshletOffset = geomLod.meshletOffset;
            it->second.lodAllocs[lodLevel].meshletCount = geomLod.meshletCount;
            it->second.lodAllocs[lodLevel].meshletVertexOffset = geomLod.meshletVertexOffset;
            it->second.lodAllocs[lodLevel].meshletVertexCount = geomLod.meshletVertexCount;
            it->second.lodAllocs[lodLevel].meshletPrimitiveOffset = geomLod.meshletPrimitiveOffset;
            it->second.lodAllocs[lodLevel].meshletPrimitiveCount = geomLod.meshletPrimitiveCount;
            it->second.lodAllocs[lodLevel].isAllocated = geomLod.isAllocated;
        }

        it->second.isUploaded = it->second.hasAnyAllocation();
        gpuTileDataDirty_ = true;

        return true;
    }

    void TerrainGPUAdapter::removeTileLOD(const TerrainTileKey& key, uint32_t lodLevel)
    {
        if (lodLevel >= TERRAIN_LOD_LEVEL_COUNT) return;

        auto it = allocations_.find(key);
        if (it == allocations_.end()) return;

        std::string tileKeyStr = it->second.getMeshPath();

        terrainBuffer_.freeTileLOD(tileKeyStr, lodLevel);
        it->second.lodAllocs[lodLevel] = TerrainLODAllocation{};
        gpuTileDataDirty_ = true;

        if (!it->second.hasAnyAllocation())
        {
            allocations_.erase(it);
        }
        else
        {
            it->second.isUploaded = true;
        }
    }

    bool TerrainGPUAdapter::hasTile(const TerrainTileKey& key) const
    {
        return allocations_.find(key) != allocations_.end();
    }

    void TerrainGPUAdapter::clear()
    {
        for (auto& [key, alloc] : allocations_)
        {
            std::string tileKey = alloc.getMeshPath();
            terrainBuffer_.freeTile(tileKey);
        }
        allocations_.clear();
        cachedGPUTileData_.clear();
        gpuTileDataDirty_ = true;
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

        // Upload vertices in tile-local space (model matrix handles world transform)
        if (!terrainBuffer_.uploadLODVertices(tileKey, lodLevel,
                                               lodData.vertices.data(),
                                               static_cast<uint32_t>(lodData.vertices.size())))
        {
            return false;
        }

        if (!terrainBuffer_.uploadLODIndices(tileKey, lodLevel,
                                              lodData.indices.data(),
                                              static_cast<uint32_t>(lodData.indices.size())))
        {
            return false;
        }

        if (!lodData.meshlets.empty())
        {
            const TerrainTileGeometry* tileGeom = terrainBuffer_.getTileGeometry(tileKey);
            if (!tileGeom)
            {
                return false;
            }

            uint32_t baseVertexOffset = tileGeom->lods[lodLevel].vertexOffset;
            uint32_t meshletVertexOffset = tileGeom->lods[lodLevel].meshletVertexOffset;
            uint32_t meshletPrimitiveOffset = tileGeom->lods[lodLevel].meshletPrimitiveOffset;

            std::vector<GPUMeshlet> gpuMeshlets;
            gpuMeshlets.reserve(lodData.meshlets.size());

            for (const auto& srcMeshlet : lodData.meshlets)
            {
                GPUMeshlet meshlet = convertMeshlet(srcMeshlet, baseVertexOffset);

                // Bounding sphere stays in local space; task shader transforms via modelMatrix
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

    bool TerrainGPUAdapter::uploadWeightMap(const terrain::TerrainTile& tile)
    {
        if (!tile.hasWeightMap())
        {
            return false;
        }

        TerrainTileKey key{tile.coord.x, tile.coord.z};
        auto it = allocations_.find(key);
        if (it == allocations_.end())
        {
            return false;
        }

        auto& alloc = it->second;
        const auto& wm = tile.weightMap;

        uint32_t totalBytes = wm.resolution * wm.resolution * terrain::WEIGHT_CHANNELS;

        std::string tileKey = alloc.getMeshPath();
        uint32_t offsetElements = terrainBuffer_.allocateWeightMap(tileKey, totalBytes);
        if (offsetElements == FreeListAllocator::ALLOCATION_FAILED)
        {
            vfLogError("TerrainGPUAdapter: Failed to allocate weight map for tile ({}, {})",
                       key.coordX, key.coordZ);
            return false;
        }

        std::vector<uint8_t> packedData(totalBytes);

        for (uint32_t z = 0; z < wm.resolution; ++z)
        {
            for (uint32_t x = 0; x < wm.resolution; ++x)
            {
                uint32_t pixelOffset = (z * wm.resolution + x) * terrain::WEIGHT_CHANNELS;
                for (uint8_t ch = 0; ch < terrain::WEIGHT_CHANNELS; ++ch)
                {
                    packedData[pixelOffset + ch] = static_cast<uint8_t>(
                        wm.getWeight(ch, x, z) * 255.0f + 0.5f);
                }
            }
        }

        if (!terrainBuffer_.uploadWeightMapData(tileKey, packedData.data(), totalBytes))
        {
            vfLogError("TerrainGPUAdapter: Failed to upload weight map for tile ({}, {})",
                       key.coordX, key.coordZ);
            return false;
        }

        // Store byte offset (elements * 4) for shader access
        alloc.weightMapOffset = offsetElements * 4;
        alloc.weightMapUploaded = true;
        gpuTileDataDirty_ = true;

        return true;
    }

    static float packLayerIndicesAsFloat(const uint8_t* indices)
    {
        uint32_t packed = static_cast<uint32_t>(indices[0])
            | (static_cast<uint32_t>(indices[1]) << 8)
            | (static_cast<uint32_t>(indices[2]) << 16)
            | (static_cast<uint32_t>(indices[3]) << 24);
        float result;
        std::memcpy(&result, &packed, sizeof(float));
        return result;
    }

    void TerrainGPUAdapter::populateGPUTile(
        TerrainTileGPUData& gpuTile,
        const TerrainTileAllocation& alloc,
        const terrain::TerrainTile& tile,
        const TerrainTileKey& key)
    {
        gpuTile.modelMatrix = glm::translate(glm::mat4(1.0f),
            glm::vec3(tile.worldOrigin.x, 0.0f, tile.worldOrigin.z));

        glm::vec3 center = (tile.worldBounds.min + tile.worldBounds.max) * 0.5f;
        gpuTile.boundingSphere = glm::vec4(center, glm::length(tile.worldBounds.max - center));
        gpuTile.aabbMin = glm::vec4(tile.worldBounds.min, static_cast<float>(tile.weightMap.resolution));
        gpuTile.aabbMax = glm::vec4(tile.worldBounds.max, packLayerIndicesAsFloat(&tile.weightMap.layerIndices[0]));

        glm::uvec4* meshletPtrs[] = {
            &gpuTile.lod0MeshletData, &gpuTile.lod1MeshletData,
            &gpuTile.lod2MeshletData, &gpuTile.lod3MeshletData,
            &gpuTile.lod4MeshletData, &gpuTile.lod5MeshletData
        };
        for (uint32_t i = 0; i < TERRAIN_LOD_LEVEL_COUNT; ++i)
        {
            *meshletPtrs[i] = glm::uvec4(
                alloc.lodAllocs[i].meshletOffset, alloc.lodAllocs[i].meshletCount,
                alloc.lodAllocs[i].vertexOffset, tile.lodLevels[i].mainMeshletCount);
        }

        gpuTile.lodGeometricErrors = glm::vec4(
            tile.lodLevels[0].geometricError, tile.lodLevels[1].geometricError,
            tile.lodLevels[2].geometricError, tile.lodLevels[3].geometricError);
        gpuTile.lodGeometricErrors2 = glm::vec4(
            tile.lodLevels[4].geometricError, tile.lodLevels[5].geometricError,
            packLayerIndicesAsFloat(&tile.weightMap.layerIndices[4]), 0.0f);

        gpuTile.coordX = key.coordX;
        gpuTile.coordZ = key.coordZ;
        gpuTile.flags = ObjectFlags::TerrainTile;
        if (hasSelectedTile_ && key.coordX == selectedCoordX_ && key.coordZ == selectedCoordZ_)
            gpuTile.flags |= ObjectFlags::Selected;
        gpuTile.weightMapOffset = alloc.weightMapUploaded ? alloc.weightMapOffset : 0;
    }

    const std::vector<TerrainTileGPUData>& TerrainGPUAdapter::buildGPUTileData(
        const std::vector<terrain::TerrainTile*>& tiles)
    {
        if (!gpuTileDataDirty_)
            return cachedGPUTileData_;

        cachedGPUTileData_.clear();
        cachedGPUTileData_.reserve(tiles.size());

        for (const terrain::TerrainTile* tile : tiles)
        {
            if (!tile || !tile->isVisible)
                continue;

            TerrainTileKey key{tile->coord.x, tile->coord.z};
            auto it = allocations_.find(key);
            if (it == allocations_.end() || !it->second.isUploaded)
                continue;

            TerrainTileGPUData gpuTile{};
            populateGPUTile(gpuTile, it->second, *tile, key);
            cachedGPUTileData_.push_back(gpuTile);
        }

        gpuTileDataDirty_ = false;
        return cachedGPUTileData_;
    }

    void TerrainGPUAdapter::setSelectedTile(int32_t coordX, int32_t coordZ)
    {
        if (!hasSelectedTile_ || selectedCoordX_ != coordX || selectedCoordZ_ != coordZ)
        {
            selectedCoordX_ = coordX;
            selectedCoordZ_ = coordZ;
            hasSelectedTile_ = true;
            gpuTileDataDirty_ = true;
        }
    }

    void TerrainGPUAdapter::clearSelectedTile()
    {
        if (hasSelectedTile_)
        {
            hasSelectedTile_ = false;
            gpuTileDataDirty_ = true;
        }
    }
}
