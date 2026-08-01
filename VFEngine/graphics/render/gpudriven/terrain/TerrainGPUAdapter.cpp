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

        // Notify acceleration structure manager about the best available LOD
        if (onTileLODReady)
        {
            // Find best (lowest number) allocated LOD for this tile
            for (uint32_t lod = 0; lod < TERRAIN_LOD_LEVEL_COUNT; ++lod)
            {
                const auto& lodAlloc = it->second.lodAllocs[lod];
                if (lodAlloc.isAllocated && lodAlloc.vertexCount > 0 && lodAlloc.indexCount > 0)
                {
                    std::string asTileKey = std::to_string(key.coordX) + "_" + std::to_string(key.coordZ);
                    onTileLODReady(asTileKey, lodAlloc.vertexOffset, lodAlloc.vertexCount,
                                   lodAlloc.indexOffset, lodAlloc.indexCount);
                    break;
                }
            }
        }

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
            if (onTileRemoved)
            {
                std::string asTileKey = std::to_string(key.coordX) + "_" + std::to_string(key.coordZ);
                onTileRemoved(asTileKey);
            }
            std::string tileKey = alloc.getMeshPath();
            terrainBuffer_.freeTile(tileKey);
        }
        allocations_.clear();
        cachedGPUTileData_.clear();
        gpuTileDataDirty_ = true;

        // VK-1613: the mask is material state, and this adapter no longer holds the material's
        // terrain. Leaving it set would carry the previous scene's hidden layers into a scene whose
        // material load never overwrites it (one with no terrain material at all). The version bump
        // is only tidiness — every per-tile stamp was just erased with the allocations.
        layerEnabledMask_ = terrain::ALL_TERRAIN_LAYERS_ENABLED;
        ++maskVersion_;
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
            // VK-1613: log once per tile, not once per frame. needsWeightMapUpload keeps returning
            // true for a tile that has never uploaded (deliberately — it is publishing
            // weightMapOffset = 0 and sampling another tile's weights until it succeeds), so an
            // ungated error here would spam every frame for as long as the arena stays full.
            if (!alloc.weightMapAllocFailed)
            {
                alloc.weightMapAllocFailed = true;
                vfLogError("TerrainGPUAdapter: Failed to allocate weight map for tile ({}, {}) — "
                           "weight arena is full; this tile will render with another tile's weights "
                           "until space frees up",
                           key.coordX, key.coordZ);
            }
            return false;
        }
        alloc.weightMapAllocFailed = false;

        std::vector<uint8_t> packedData(totalBytes);

        // VK-1613: resolve each channel's visibility ONCE (it is per channel, not per texel) and
        // then write a hard 0 for a hidden layer. The composite culls at w < 0.001 and normalizes by
        // the surviving weight, so a zeroed channel disappears and the rest scale up — no shader
        // change, and `wm` itself is never modified, so unhiding restores the artist's paint exactly.
        std::array<bool, terrain::WEIGHT_CHANNELS> channelVisible{};
        for (uint8_t ch = 0; ch < terrain::WEIGHT_CHANNELS; ++ch)
        {
            channelVisible[ch] = terrain::isWeightChannelEnabled(wm, ch, layerEnabledMask_);
        }

        for (uint32_t z = 0; z < wm.resolution; ++z)
        {
            for (uint32_t x = 0; x < wm.resolution; ++x)
            {
                uint32_t pixelOffset = (z * wm.resolution + x) * terrain::WEIGHT_CHANNELS;
                for (uint8_t ch = 0; ch < terrain::WEIGHT_CHANNELS; ++ch)
                {
                    packedData[pixelOffset + ch] = channelVisible[ch]
                        ? static_cast<uint8_t>(wm.getWeight(ch, x, z) * 255.0f + 0.5f)
                        : uint8_t{0};
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
        alloc.weightMaskVersion = maskVersion_;
        gpuTileDataDirty_ = true;

        return true;
    }

    void TerrainGPUAdapter::setLayerEnabledMask(uint32_t mask)
    {
        if (layerEnabledMask_ == mask)
        {
            return;
        }

        layerEnabledMask_ = mask;
        // Every stamped tile is now out of date. Bumping one counter is what makes this scale: the
        // alternative — walking the tiles and marking them dirty — cannot reach tiles that are not
        // resident yet, and those are exactly the ones that would come back with the old visibility.
        ++maskVersion_;
    }

    bool TerrainGPUAdapter::needsWeightMapUpload(const terrain::TerrainTile& tile) const
    {
        if (!tile.hasWeightMap())
        {
            // Nothing to pack. Without this guard a tile whose CPU weight data has been evicted
            // would fail uploadWeightMap() every frame forever, since neither flag below can clear.
            return false;
        }

        if (tile.weightMapGPUDirty)
        {
            return true;
        }

        auto it = allocations_.find(TerrainTileKey{tile.coord.x, tile.coord.z});
        if (it == allocations_.end())
        {
            return false;
        }

        // !weightMapUploaded belongs in this disjunction rather than as a precondition on the
        // version compare: removeTileLOD erases the whole allocation when the last LOD goes, while
        // TerrainMeshBuffer::freeTileLOD does not free the weight region — so a re-added tile arrives
        // with weightMapUploaded == false, weightMapOffset == 0 and nobody having set the dirty flag,
        // and would otherwise sample whichever tile owns offset 0 in the shared weight SSBO.
        return !it->second.weightMapUploaded || it->second.weightMaskVersion != maskVersion_;
    }

    // VK-1620 --- terrain heights for the RVT world-height plane -------------------------------
    //
    // The bake needs the terrain surface at each page texel. It cannot read the terrain VERTEX
    // buffer for that: TerrainStreamManager pins only the COARSEST LOD, and populateGPUTile writes
    // lodNMeshletData for every LOD whether allocated or not, so a LOD-0 vertex read on a tile that
    // only has its fallback resident returns whatever tile owns vertex slot 0 — wrong heights, no
    // validation error. This is a straight copy of the CPU heightData grid instead, which is
    // authoritative and LOD-independent, and lands on the SAME grid as the splat weights
    // (TerrainTile::initializeWeightMap sizes the weight map from config.getVertexCount(), which is
    // also heightData's stride) so the bake's two samples are co-located by construction.
    bool TerrainGPUAdapter::needsHeightFieldUpload(const terrain::TerrainTile& tile) const
    {
        if (!terrainBuffer_.isHeightFieldEnabled() || !tile.hasHeightData())
        {
            // Same guard as the weight map's: without it, a tile whose CPU heights were evicted
            // would fail upload every frame forever because neither flag below can clear.
            return false;
        }

        if (tile.heightFieldGPUDirty)
            return true;

        auto it = allocations_.find(TerrainTileKey{tile.coord.x, tile.coord.z});
        if (it == allocations_.end())
            return false;

        // !heightFieldUploaded is a disjunct, not a precondition, for the same reason it is on the
        // weight map: removeTileLOD erases the allocation when the last LOD goes while the arena
        // region survives, so a re-added tile arrives uploaded == false with nobody having set the
        // dirty flag, and would otherwise publish offset 0 and read another tile's heights.
        return !it->second.heightFieldUploaded;
    }

    bool TerrainGPUAdapter::uploadHeightField(const terrain::TerrainTile& tile)
    {
        if (!terrainBuffer_.isHeightFieldEnabled() || !tile.hasHeightData())
            return false;

        TerrainTileKey key{tile.coord.x, tile.coord.z};
        auto it = allocations_.find(key);
        if (it == allocations_.end())
            return false;

        auto& alloc = it->second;

        const uint32_t vertexCount = tile.config.getVertexCount();
        const uint32_t totalBytes = vertexCount * vertexCount * static_cast<uint32_t>(sizeof(float));
        if (tile.heightData.size() != static_cast<size_t>(vertexCount) * vertexCount)
        {
            vfLogError("TerrainGPUAdapter: tile ({}, {}) height data is {} floats, expected {} — "
                       "skipping world-height upload",
                       key.coordX, key.coordZ, tile.heightData.size(),
                       static_cast<size_t>(vertexCount) * vertexCount);
            return false;
        }

        const std::string tileKey = alloc.getMeshPath();
        const uint32_t offsetElements = terrainBuffer_.allocateHeightField(tileKey, totalBytes);
        if (offsetElements == FreeListAllocator::ALLOCATION_FAILED)
        {
            // Gated to once per tile per failure episode, exactly like the weight arena's: the
            // needs-upload predicate keeps returning true for a tile that never uploaded, so an
            // ungated error would spam every frame while the arena stays full.
            if (!alloc.heightFieldAllocFailed)
            {
                alloc.heightFieldAllocFailed = true;
                vfLogError("TerrainGPUAdapter: Failed to allocate height field for tile ({}, {}) — "
                           "height arena is full; props over this tile will not blend into it",
                           key.coordX, key.coordZ);
            }
            return false;
        }
        alloc.heightFieldAllocFailed = false;

        // heightData is already the exact row-major float grid the bake indexes, so there is no
        // repacking step here — unlike the weight map, which has to interleave 8 channels per texel.
        if (!terrainBuffer_.uploadHeightFieldData(tileKey, tile.heightData.data(), totalBytes))
        {
            vfLogError("TerrainGPUAdapter: Failed to upload height field for tile ({}, {})",
                       key.coordX, key.coordZ);
            return false;
        }

        alloc.heightFieldOffset = offsetElements;
        alloc.heightFieldUploaded = true;
        gpuTileDataDirty_ = true;
        return true;
    }

    bool TerrainGPUAdapter::uploadCaveMesh(const terrain::TerrainTile& tile)
    {
        if (!tile.hasCaveGeometry())
            return false;

        const auto& caveLOD = tile.caveLOD;
        if (caveLOD.isEmpty() || !caveLOD.hasMeshlets())
            return false;

        TerrainTileKey key{tile.coord.x, tile.coord.z};
        std::string caveKey = "terrain_" + std::to_string(key.coordX) + "_" + std::to_string(key.coordZ) + "_cave";

        // Free previous cave allocation if any
        terrainBuffer_.freeTileLOD(caveKey, 0);

        // Allocate cave mesh as LOD 0 of a separate "cave tile" in the mesh buffer
        if (!terrainBuffer_.allocateTileLOD(
                caveKey,
                0, // Use LOD 0 slot
                static_cast<uint32_t>(caveLOD.vertices.size()),
                static_cast<uint32_t>(caveLOD.indices.size()),
                static_cast<uint32_t>(caveLOD.meshlets.size()),
                static_cast<uint32_t>(caveLOD.meshletVertices.size()),
                static_cast<uint32_t>(caveLOD.meshletPrimitives.size()),
                tile.worldBounds.min,
                tile.worldBounds.max))
        {
            vfLogError("TerrainGPUAdapter: Failed to allocate cave mesh for tile ({}, {})",
                       key.coordX, key.coordZ);
            return false;
        }

        auto it = allocations_.find(key);
        if (it == allocations_.end())
            return false;

        // Upload vertices
        if (!terrainBuffer_.uploadLODVertices(caveKey, 0,
                                               caveLOD.vertices.data(),
                                               static_cast<uint32_t>(caveLOD.vertices.size())))
        {
            terrainBuffer_.freeTileLOD(caveKey, 0);
            return false;
        }

        // Upload indices
        if (!terrainBuffer_.uploadLODIndices(caveKey, 0,
                                              caveLOD.indices.data(),
                                              static_cast<uint32_t>(caveLOD.indices.size())))
        {
            terrainBuffer_.freeTileLOD(caveKey, 0);
            return false;
        }

        const TerrainTileGeometry* caveGeom = terrainBuffer_.getTileGeometry(caveKey);
        if (!caveGeom)
        {
            terrainBuffer_.freeTileLOD(caveKey, 0);
            return false;
        }

        const auto& geomLod = caveGeom->lods[0];
        if (!uploadCaveMeshlets(caveKey, caveLOD, geomLod))
        {
            terrainBuffer_.freeTileLOD(caveKey, 0);
            return false;
        }

        // Store allocation offsets
        it->second.caveAlloc.vertexOffset = geomLod.vertexOffset;
        it->second.caveAlloc.vertexCount = geomLod.vertexCount;
        it->second.caveAlloc.indexOffset = geomLod.indexOffset;
        it->second.caveAlloc.indexCount = geomLod.indexCount;
        it->second.caveAlloc.meshletOffset = geomLod.meshletOffset;
        it->second.caveAlloc.meshletCount = geomLod.meshletCount;
        it->second.caveAlloc.meshletVertexOffset = geomLod.meshletVertexOffset;
        it->second.caveAlloc.meshletVertexCount = geomLod.meshletVertexCount;
        it->second.caveAlloc.meshletPrimitiveOffset = geomLod.meshletPrimitiveOffset;
        it->second.caveAlloc.meshletPrimitiveCount = geomLod.meshletPrimitiveCount;
        it->second.caveAlloc.isAllocated = true;

        gpuTileDataDirty_ = true;
        return true;
    }

    bool TerrainGPUAdapter::releaseCaveMesh(const terrain::TerrainTile& tile)
    {
        TerrainTileKey key{tile.coord.x, tile.coord.z};

        auto it = allocations_.find(key);
        if (it == allocations_.end() || !it->second.caveAlloc.isAllocated)
            return false; // nothing allocated -> nothing to free

        std::string caveKey = "terrain_" + std::to_string(key.coordX) + "_" + std::to_string(key.coordZ) + "_cave";
        terrainBuffer_.freeTileLOD(caveKey, 0);

        it->second.caveAlloc = TerrainLODAllocation{}; // isAllocated = false, offsets cleared

        // buildGPUTileData now writes caveMeshletData = 0 for this tile, so the task
        // shader emits no cave meshlets and the cave stops rendering.
        gpuTileDataDirty_ = true;
        return true;
    }

    bool TerrainGPUAdapter::uploadCaveMeshlets(const std::string& caveKey,
                                                const terrain::TileLODData& caveLOD,
                                                const TerrainLODGeometry& geomLod)
    {
        uint32_t baseVertexOffset = geomLod.vertexOffset;

        std::vector<GPUMeshlet> gpuMeshlets;
        gpuMeshlets.reserve(caveLOD.meshlets.size());

        for (const auto& srcMeshlet : caveLOD.meshlets)
        {
            GPUMeshlet meshlet = convertMeshlet(srcMeshlet, baseVertexOffset);
            meshlet.vertexOffset += geomLod.meshletVertexOffset;
            meshlet.primitiveOffset += geomLod.meshletPrimitiveOffset;
            gpuMeshlets.push_back(meshlet);
        }

        return terrainBuffer_.uploadLODMeshlets(caveKey, 0,
                                                 gpuMeshlets.data(),
                                                 static_cast<uint32_t>(gpuMeshlets.size()),
                                                 caveLOD.meshletVertices.data(),
                                                 static_cast<uint32_t>(caveLOD.meshletVertices.size()),
                                                 caveLOD.meshletPrimitives.data(),
                                                 static_cast<uint32_t>(caveLOD.meshletPrimitives.size()));
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

        // VK-1620: the heightfield element offset rides caveMeshletData.w, which was reserved and
        // unused. Biased by +1 so 0 means "no height data for this tile" — offset 0 is a legal
        // allocation and could not otherwise be told apart from absent, which would make the first
        // tile in the arena the fallback for every tile that has none.
        const uint32_t heightFieldSlot =
            alloc.heightFieldUploaded ? (alloc.heightFieldOffset + 1u) : 0u;

        // Cave meshlet data
        if (alloc.caveAlloc.isAllocated)
        {
            gpuTile.caveMeshletData = glm::uvec4(
                alloc.caveAlloc.meshletOffset,
                alloc.caveAlloc.meshletCount,
                alloc.caveAlloc.vertexOffset,
                heightFieldSlot);
        }
        else
        {
            // .w must survive the no-cave case: a tile without a cave still has heights, and
            // zeroing the whole vector here would silently disable blending on flat terrain.
            gpuTile.caveMeshletData = glm::uvec4(0u, 0u, 0u, heightFieldSlot);
        }
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
