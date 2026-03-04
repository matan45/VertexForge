#include "TerrainGrid.hpp"
#include "../threading/JobSystem.hpp"
#include <algorithm>

namespace terrain
{
    TerrainGrid::TerrainGrid(const TerrainTileConfig& config)
        : config(config)
          , generator(std::make_unique<TerrainTileGenerator>(config))
    {
    }

    void TerrainGrid::setHeightSampler(HeightSampler sampler)
    {
        generator->setHeightSampler(std::move(sampler));
    }

    TerrainTile* TerrainGrid::getTile(const TileCoord& coord)
    {
        auto it = tiles.find(coord);
        return (it != tiles.end()) ? it->second.get() : nullptr;
    }

    const TerrainTile* TerrainGrid::getTile(const TileCoord& coord) const
    {
        auto it = tiles.find(coord);
        return (it != tiles.end()) ? it->second.get() : nullptr;
    }

    TerrainTile* TerrainGrid::getOrCreateTile(const TileCoord& coord)
    {
        auto it = tiles.find(coord);
        if (it != tiles.end())
        {
            return it->second.get();
        }

        auto tile = generator->generateTile(coord);
        TerrainTile* tilePtr = tile.get();
        tiles.emplace(coord, std::move(tile));

        updateNeighborReferences(*tilePtr);

        return tilePtr;
    }

    void TerrainGrid::regenerateDirtyTiles(const glm::vec3& cameraPosition)
    {
        constexpr uint32_t MAX_TILE_REGEN = 8;
        uint32_t tileRegenCount = 0;

        auto getTile = [this](const TileCoord& coord) -> const TerrainTile* {
            return this->getTile(coord);
        };

        // Pre-load neighbor heights so overrideBoundaryNormals can access
        // cross-tile height data (neighbors may not have heights during streaming)
        if (fileCache)
        {
            for (auto& [coord, tile] : tiles)
            {
                if (!tile->edgeSyncDirty && !(tile->isDirty && tile->dirtyLODMask != 0))
                    continue;

                if (!tile->hasHeightData())
                    fileCache->ensureHeightsLoaded(*tile);

                for (uint8_t i = 0; i < 4; ++i)
                {
                    TileCoord nc = coord + TileCoord::getNeighborOffset(static_cast<TileEdge>(i));
                    auto nit = tiles.find(nc);
                    if (nit != tiles.end() && !nit->second->hasHeightData())
                        fileCache->ensureHeightsLoaded(*nit->second);
                }
            }
        }

        for (auto& [coord, tile] : tiles)
        {
            if (!tile->edgeSyncDirty)
                continue;

            if (fileCache && !tile->hasHeightData())
                fileCache->ensureHeightsLoaded(*tile);

            for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod)
            {
                if (tile->isLODDirty(lod))
                {
                    generator->regenerateLOD(*tile, lod, getTile);
                }
            }

            // Clear topologyDirty after all LODs have been regenerated
            if (tile->dirtyLODMask == 0)
                tile->topologyDirty = false;

            tile->edgeSyncDirty = false;
        }

        for (auto& [coord, tile] : tiles)
        {
            if (!tile->isDirty)
                continue;

            if (tileRegenCount >= MAX_TILE_REGEN)
                continue;

            if (fileCache && !tile->hasHeightData())
                fileCache->ensureHeightsLoaded(*tile);

            bool anyRegenerated = false;
            for (uint32_t lod = 0; lod < TERRAIN_LOD_COUNT; ++lod)
            {
                if (tile->isLODDirty(lod))
                {
                    generator->regenerateLOD(*tile, lod, getTile);
                    anyRegenerated = true;
                }
            }

            // Clear topologyDirty after all LODs have been regenerated
            if (anyRegenerated && tile->dirtyLODMask == 0)
                tile->topologyDirty = false;

            if (anyRegenerated)
                ++tileRegenCount;
        }
    }

    std::vector<TerrainTile*> TerrainGrid::getVisibleTiles(const math::Frustum& frustum)
    {
        std::vector<TerrainTile*> result;
        result.reserve(tiles.size());

        for (auto& [coord, tile] : tiles)
        {
            if (frustum.intersectsAABB(tile->worldBounds))
            {
                tile->isVisible = true;
                result.push_back(tile.get());
            }
            else
            {
                tile->isVisible = false;
            }
        }

        return result;
    }

    void TerrainGrid::updateNeighborReferences(TerrainTile& tile)
    {
        for (uint8_t i = 0; i < 4; ++i)
        {
            TileEdge edge = static_cast<TileEdge>(i);
            TileCoord neighborCoord = tile.coord + TileCoord::getNeighborOffset(edge);

            TerrainTile* neighbor = getTile(neighborCoord);
            if (neighbor)
            {
                tile.setNeighbor(edge, neighborCoord, neighbor->currentLOD);
                neighbor->setNeighbor(TileCoord::getOppositeEdge(edge), tile.coord, tile.currentLOD);
            }
            else
            {
                tile.clearNeighbor(edge);
            }
        }
    }

    void TerrainGrid::updateAllNeighborReferences()
    {
        for (auto& [coord, tile] : tiles)
        {
            updateNeighborReferences(*tile);
        }
    }

    std::vector<TileCoord> TerrainGrid::updateLODs(const glm::vec3& cameraPosition)
    {
        std::vector<TileCoord> changedTiles;
        changedTiles.reserve(tiles.size() / 4);

        for (auto& [coord, tile] : tiles)
        {
            uint32_t newLOD = generator->calculateLOD(cameraPosition, *tile);
            if (newLOD != tile->currentLOD)
            {
                tile->currentLOD = static_cast<uint8_t>(newLOD);
                changedTiles.push_back(coord);
            }
        }

        updateAllNeighborReferences();

        for (auto& [coord, tile] : tiles)
        {
            generator->updateEdgeStitching(*tile);

            if (tile->stitchingChanged())
            {
                tile->setAllLODsDirty();
                tile->edgeSyncDirty = true;
                tile->saveStitchState();

                if (std::find(changedTiles.begin(), changedTiles.end(), coord) == changedTiles.end())
                {
                    changedTiles.push_back(coord);
                }
            }
        }

        return changedTiles;
    }

    std::vector<TerrainTile*> TerrainGrid::getAllTiles()
    {
        std::vector<TerrainTile*> result;
        result.reserve(tiles.size());

        for (auto& [coord, tile] : tiles)
        {
            result.push_back(tile.get());
        }

        return result;
    }

    std::vector<const TerrainTile*> TerrainGrid::getAllTiles() const
    {
        std::vector<const TerrainTile*> result;
        result.reserve(tiles.size());

        for (const auto& [coord, tile] : tiles)
        {
            result.push_back(tile.get());
        }

        return result;
    }

    void TerrainGrid::createGrid(int32_t minX, int32_t minZ, int32_t maxX, int32_t maxZ,
                                 ProgressCallback progress)
    {
        int32_t totalTiles = (maxX - minX + 1) * (maxZ - minZ + 1);

        struct TileGenResult
        {
            TileCoord coord;
            std::future<std::unique_ptr<TerrainTile>> future;
        };

        std::vector<TileGenResult> genResults;
        genResults.reserve(totalTiles);

        if (progress)
        {
            progress(0.0f, "Generating tiles...");
        }

        // Phase 1: generate tiles in parallel (pure computation, no shared state)
        for (int32_t z = minZ; z <= maxZ; ++z)
        {
            for (int32_t x = minX; x <= maxX; ++x)
            {
                TileCoord coord(x, z);
                if (tiles.find(coord) != tiles.end())
                    continue;

                auto* gen = generator.get();
                genResults.push_back({coord,
                    threading::JobSystem::instance().submit(
                        [gen, coord]() -> std::unique_ptr<TerrainTile>
                        {
                            return gen->generateTile(coord);
                        }, threading::JobPriority::NORMAL
                    )
                });
            }
        }

        // Phase 2: insert generated tiles and update neighbors sequentially
        int32_t currentTile = 0;
        for (auto& gr : genResults)
        {
            if (progress)
            {
                progress(static_cast<float>(currentTile) / static_cast<float>(totalTiles),
                         "Inserting tile (" + std::to_string(gr.coord.x) + ", " + std::to_string(gr.coord.z) + ")");
            }

            auto tile = gr.future.get();
            TerrainTile* tilePtr = tile.get();
            tiles.emplace(gr.coord, std::move(tile));
            updateNeighborReferences(*tilePtr);
            ++currentTile;
        }

        if (progress)
        {
            progress(1.0f, "Complete");
        }
    }

    bool TerrainGrid::loadFromSerialized(const std::vector<TileLoadResult>& loadedTiles,
                                          ProgressCallback progress)
    {
        tiles.clear();
        uint32_t total = static_cast<uint32_t>(loadedTiles.size());
        uint32_t current = 0;

        for (const auto& loaded : loadedTiles)
        {
            if (!loaded.success)
                continue;

            if (progress)
            {
                progress(static_cast<float>(current) / static_cast<float>(total),
                         "Loading tile (" + std::to_string(loaded.coord.x) + ", " +
                         std::to_string(loaded.coord.z) + ")");
            }

            auto tile = std::make_unique<TerrainTile>(loaded.coord, config);
            tile->initializeFromHeights(loaded.heightData);

            // Restore holeMask if present in serialized data
            if (!loaded.holeMask.empty())
            {
                tile->holeMask = loaded.holeMask;
            }

            if (loaded.weightMap.isInitialized())
            {
                tile->weightMap = loaded.weightMap;
                tile->weightMapGPUDirty = true;
            }

            if (loaded.hasLODCache)
            {
                tile->lodLevels = loaded.lodData;
                tile->isDirty = false;
                tile->dirtyLODMask = 0;
                tile->updateWorldBounds();
                vfLogInfo("TerrainGrid: Loaded cached LODs for tile ({}, {})",
                          loaded.coord.x, loaded.coord.z);
            }
            else
            {
                generator->generateAllLODs(*tile, nullptr);
                vfLogInfo("TerrainGrid: Regenerated LODs for tile ({}, {})",
                          loaded.coord.x, loaded.coord.z);
            }

            tiles.emplace(loaded.coord, std::move(tile));
            ++current;
        }

        updateAllNeighborReferences();

        if (progress)
        {
            progress(1.0f, "Complete");
        }

        vfLogInfo("TerrainGrid: Loaded {} tiles from serialized data", tiles.size());
        return !tiles.empty();
    }

    bool TerrainGrid::loadMetadataOnly(const TerrainFileHeader& header,
                                       const std::vector<TileIndexEntry>& index)
    {
        tiles.clear();

        for (const auto& entry : index)
        {
            TileCoord coord{entry.coordX, entry.coordZ};
            auto tile = std::make_unique<TerrainTile>(coord, config);
            tile->initializeMetadataOnly();
            tiles.emplace(coord, std::move(tile));
        }

        updateAllNeighborReferences();

        vfLogInfo("TerrainGrid: Created {} metadata-only tiles for streaming", tiles.size());
        return !tiles.empty();
    }

    void TerrainGrid::initializeWeightMaps(uint8_t layerCount)
    {
        for (auto& [coord, tile] : tiles)
        {
            if (!tile->hasWeightMap())
            {
                tile->initializeWeightMap(layerCount);
            }
        }
    }

    void TerrainGrid::updateWeightMapLayerCount(uint8_t newLayerCount)
    {
        for (auto& [coord, tile] : tiles)
        {
            if (tile->hasWeightMap())
            {
                tile->weightMap.setLayerCount(newLayerCount);
                tile->weightMapDirty = true;
                tile->weightMapGPUDirty = true;
            }
            else
            {
                tile->initializeWeightMap(newLayerCount);
            }
        }
    }

    std::vector<TerrainTile*> TerrainGrid::getWeightMapDirtyTiles()
    {
        std::vector<TerrainTile*> result;
        for (auto& [coord, tile] : tiles)
        {
            if (tile->weightMapDirty)
            {
                result.push_back(tile.get());
            }
        }
        return result;
    }
}
