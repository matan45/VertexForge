#include "../print/Log.hpp"
#include "TerrainGrid.hpp"
#include "../threading/JobSystem.hpp"
#include <algorithm>
#include <chrono>
#include <unordered_set>

namespace terrain
{
    TerrainGrid::TerrainGrid(const TerrainTileConfig& config)
        : config(config)
          , generator(std::make_unique<TerrainTileGenerator>(config))
          , heightLayers(std::make_unique<TerrainHeightLayerStore>())
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

    TerrainTile* TerrainGrid::addTile(const TileCoord& coord)
    {
        auto it = tiles.find(coord);
        if (it != tiles.end())
        {
            return it->second.get();
        }

        auto tile = generator->generateTile(coord);
        TerrainTile* tilePtr = tile.get();
        tiles.emplace(coord, std::move(tile));
        quadtree.insert(tilePtr);

        updateNeighborReferences(*tilePtr);

        // Mark new tile and its existing neighbors dirty so boundary normals regenerate
        tilePtr->edgeSyncDirty = true;
        tilePtr->setAllLODsDirty();
        for (uint8_t i = 0; i < 4; ++i)
        {
            if (tilePtr->neighbors[i].exists)
            {
                TerrainTile* neighbor = getTile(tilePtr->neighbors[i].coord);
                if (neighbor)
                {
                    neighbor->edgeSyncDirty = true;
                    neighbor->setAllLODsDirty();
                }
            }
        }

        return tilePtr;
    }

    TerrainTile* TerrainGrid::addTileFromFile(const TileCoord& coord)
    {
        auto it = tiles.find(coord);
        if (it != tiles.end())
        {
            return it->second.get();
        }

        auto tile = std::make_unique<TerrainTile>(coord, config);
        tile->initializeMetadataOnly();
        TerrainTile* tilePtr = tile.get();
        tiles.emplace(coord, std::move(tile));
        quadtree.insert(tilePtr);

        updateNeighborReferences(*tilePtr);

        // VK-1645: a tile streaming back in re-attaches to its base block automatically, because
        // the store is keyed by coord. Whatever heights arrive from disk are the quantized
        // composite, so the derived plane owes a recompose.
        if (heightLayers->isCovered(coord))
            heightLayers->markDerivedStale(coord);

        tilePtr->edgeSyncDirty = true;
        tilePtr->setAllLODsDirty();
        for (uint8_t i = 0; i < 4; ++i)
        {
            if (tilePtr->neighbors[i].exists)
            {
                TerrainTile* neighbor = getTile(tilePtr->neighbors[i].coord);
                if (neighbor)
                {
                    neighbor->edgeSyncDirty = true;
                    neighbor->setAllLODsDirty();
                }
            }
        }

        return tilePtr;
    }

    bool TerrainGrid::removeTile(const TileCoord& coord)
    {
        auto it = tiles.find(coord);
        if (it == tiles.end())
        {
            return false;
        }

        TerrainTile* tile = it->second.get();

        // Clear neighbor references on adjacent tiles and mark them dirty
        for (uint8_t i = 0; i < 4; ++i)
        {
            if (tile->neighbors[i].exists)
            {
                TerrainTile* neighbor = getTile(tile->neighbors[i].coord);
                if (neighbor)
                {
                    TileEdge oppositeEdge = TileCoord::getOppositeEdge(static_cast<TileEdge>(i));
                    neighbor->clearNeighbor(oppositeEdge);
                    neighbor->edgeSyncDirty = true;
                    neighbor->setAllLODsDirty();
                }
            }
        }

        quadtree.remove(coord);
        // VK-1645: the authoritative base block for this coord deliberately SURVIVES. This is the
        // stream-out path, and the base has no persistence until VK-1646 -- dropping it here
        // would silently destroy artist edits the moment the camera walked away. Only the
        // deliberate tile deletion in TerrainService::removeTile erases it.
        tiles.erase(it);
        return true;
    }

    void TerrainGrid::computeBounds(int32_t& minX, int32_t& minZ, int32_t& maxX, int32_t& maxZ) const
    {
        if (tiles.empty())
        {
            minX = minZ = maxX = maxZ = 0;
            return;
        }

        quadtree.getBounds(minX, minZ, maxX, maxZ);
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
        quadtree.insert(tilePtr);

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
        // cross-tile height data (neighbors may not have heights during streaming).
        //
        // VK-1645: through ensureTileHeights, so a covered tile whose RAM was evicted comes back
        // marked derived-stale -- the bytes it just read are the uint16-quantized composite, and
        // the exact answer is a recompose from the in-RAM base.
        if (fileCache)
        {
            for (auto& [coord, tile] : tiles)
            {
                if (!tile->edgeSyncDirty && !(tile->isDirty && tile->dirtyLODMask != 0))
                    continue;

                ensureTileHeights(*tile);

                for (uint8_t i = 0; i < 4; ++i)
                {
                    TileCoord nc = coord + TileCoord::getNeighborOffset(static_cast<TileEdge>(i));
                    auto nit = tiles.find(nc);
                    if (nit != tiles.end())
                        ensureTileHeights(*nit->second);
                }
            }
        }

        // VK-1645: repair derived heights before anything below meshes them. This is the drain
        // for residency-driven staleness (stream-in, RAM eviction reload); explicit user actions
        // recompose synchronously at their own call site instead.
        //
        // Its own budget, deliberately NOT tileRegenCount: a recompose is a memcpy plus a
        // corridor scan, orders of magnitude cheaper than regenerateLOD, so sharing the counter
        // would let a recompose-heavy frame starve mesh regeneration. And it marks only
        // isDirty/setAllLODsDirty, never edgeSyncDirty -- the loop immediately below is
        // UNBUDGETED, so routing a wide invalidation into it would stall the frame for seconds.
        constexpr uint32_t MAX_TILE_RECOMPOSE = 32;
        recomposeDirtyDerived(MAX_TILE_RECOMPOSE);

        for (auto& [coord, tile] : tiles)
        {
            if (!tile->edgeSyncDirty)
                continue;

            ensureTileHeights(*tile);

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

            ensureTileHeights(*tile);

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

    bool TerrainGrid::ensureTileHeights(TerrainTile& tile)
    {
        if (tile.hasHeightData())
            return true;

        if (!fileCache)
            return false;

        if (!fileCache->ensureHeightsLoaded(tile))
            return false;

        // VK-1645: what just came off disk is the uint16-quantized COMPOSITE (heights round-trip
        // lossily through TerrainCompression). On a covered tile that is a placeholder, not the
        // truth -- the exact answer is a recompose from the in-RAM base.
        if (heightLayers->isCovered(tile.coord))
            heightLayers->markDerivedStale(tile.coord);

        return true;
    }

    bool TerrainGrid::recomposeTile(TerrainTile& tile)
    {
        const BaseHeightBlock* block = heightLayers->base(tile.coord);
        if (!block)
            return false;

        const uint32_t vertCount = tile.config.getVertexCount();
        const size_t expected = static_cast<size_t>(vertCount) * vertCount;

        // The base outlives the tile, so it can outlive the tile's resolution too. Refuse rather
        // than resize the height plane out from under the mesher and the heightfield collider.
        if (block->vertexCount != vertCount || block->heights.size() != expected)
            return false;

        composeTileHeights(tile.coord, tile.config, block->heights, heightLayers->layers(),
                           tile.heightData);
        return true;
    }

    uint32_t TerrainGrid::recomposeDirtyDerived(uint32_t budget, std::vector<TileCoord>* outChanged)
    {
        if (heightLayers->staleCount() == 0)
            return 0;

        // Pick the seeds: stale, covered coords, up to `budget`. Uncovered stale coords have no
        // base -- "composing" one would overwrite authoritative data with nothing -- so they are
        // dropped here and rejoin only as ring members in the seam pass. That asymmetry is the
        // single most important rule in this function.
        std::vector<TileCoord> seeds;
        for (const TileCoord& coord : heightLayers->staleSorted())
        {
            if (!heightLayers->isCovered(coord))
            {
                heightLayers->clearDerivedStale(coord);
                continue;
            }

            if (budget != 0 && seeds.size() >= budget)
                break;

            seeds.push_back(coord);
        }

        if (seeds.empty())
            return 0;

        // The working set is the seeds plus their 4-neighbour ring, for two reasons.
        // normalizeDerivedSeams only walks +X/+Z, so a seed's seam with its -X neighbour is
        // welded by that neighbour's own pass and the neighbour must be present. And every
        // COVERED member of the ring is recomposed below even when it was not stale: welding a
        // freshly composed tile against a covered neighbour still holding last cycle's welded
        // values would drift the shared corner a little on every pass. Recomposing both sides
        // first makes the weld a pure function of the bases and the stack.
        //
        // So the budget bounds the seeds, and the real work is at most 5x that. Compose is a
        // memcpy plus a corridor scan, so that is a cheap price for byte-stability.
        std::unordered_set<TileCoord, TileCoordHash> working(seeds.begin(), seeds.end());
        for (const TileCoord& coord : seeds)
        {
            for (uint8_t i = 0; i < 4; ++i)
                working.insert(coord + TileCoord::getNeighborOffset(static_cast<TileEdge>(i)));
        }

        std::vector<TileCoord> seamList(working.begin(), working.end());
        std::sort(seamList.begin(), seamList.end(),
                  [](const TileCoord& a, const TileCoord& b)
                  { return a.z != b.z ? a.z < b.z : a.x < b.x; });

        // PHASE 1 -- compose every covered, resident member of the working set.
        uint32_t composedCount = 0;
        for (const TileCoord& coord : seamList)
        {
            if (!heightLayers->isCovered(coord))
                continue;

            TerrainTile* tile = getTile(coord);
            if (!tile)
                continue; // not resident -- stays stale so stream-in recomposes it

            // Loaded for its hole mask / weights / cave payload; the heights it brings are
            // immediately overwritten by the compose below.
            if (!tile->hasHeightData() && fileCache)
                fileCache->ensureHeightsLoaded(*tile);

            if (!recomposeTile(*tile))
            {
                // No base, or a resolution mismatch. Nothing this pass can do; drop the flag so
                // it does not spin every frame.
                heightLayers->clearDerivedStale(coord);
                continue;
            }

            heightLayers->clearDerivedStale(coord);
            ++composedCount;

            tile->isDirty = true;
            tile->setAllLODsDirty();

            if (fileCache)
                fileCache->markDirty(coord);

            if (outChanged)
                outChanged->push_back(coord);
        }

        if (composedCount == 0)
            return 0;

        // PHASE 2 -- weld seams, once, over the whole working set. Never interleaved with
        // phase 1: a later compose would overwrite an earlier seam write and leave the boundary
        // asymmetric.
        normalizeDerivedSeams(seamList);

        return composedCount;
    }

    void TerrainGrid::normalizeDerivedSeams(const std::vector<TileCoord>& sorted)
    {
        // Three cases, and only the first two write anything:
        //
        //   covered <-> covered      average both derived planes. Safe because both sides restart
        //                            from base every cycle, so the average cannot accumulate.
        //   covered <-> uncovered    ONE-SIDED: the covered side conforms to the uncovered side.
        //                            Averaging here would drag the uncovered tile's AUTHORITATIVE
        //                            edge halfway toward the covered one on every single
        //                            recompose -- hide/show a layer ten times and ground the user
        //                            never touched has been rewritten. It is also the
        //                            geometrically right answer: the uncovered neighbour is
        //                            ground truth. And it costs nothing visually, because a
        //                            layer's affected set covers corridor + falloff, so a mixed
        //                            seam always sits where the layer contributes ~0.
        //   uncovered <-> uncovered  left alone. Neither side was recomposed, so their seam is
        //                            untouched -- and welding it here would be an authoritative
        //                            mutation with no undo capture. That case belongs to
        //                            TerrainService::syncBrushBoundaryHeights.
        //
        // `sorted` must be ordered: the +X pass writes tile T's NE corner and the +Z pass then
        // reads and rewrites that same vertex, so a shared corner's final value depends on the
        // visit order.
        for (const TileCoord& coord : sorted)
        {
            TerrainTile* tile = getTile(coord);
            if (!tile || !tile->hasHeightData())
                continue;

            const bool tileCovered = heightLayers->isCovered(coord);
            const uint32_t vertCount = tile->config.getVertexCount();
            const uint32_t lastIdx = vertCount - 1;
            bool tileWritten = false;

            // Welds one seam pair. `mine` belongs to `tile`, `theirs` to the neighbour; the
            // caller supplies them per shared vertex.
            const auto weld = [tileCovered](bool nbCovered, float& mine, float& theirs)
            {
                if (tileCovered && nbCovered)
                {
                    const float avg = (mine + theirs) * 0.5f;
                    mine = avg;
                    theirs = avg;
                }
                else if (tileCovered)
                {
                    mine = theirs; // covered side conforms
                }
                else
                {
                    theirs = mine; // covered side conforms
                }
            };

            const auto markWritten = [this](TerrainTile& t)
            {
                t.isDirty = true;
                t.setAllLODsDirty();
                if (fileCache)
                    fileCache->markDirty(t.coord);
            };

            // +X: this tile's last column == the neighbour's first column.
            if (TerrainTile* nbPX = getTile({coord.x + 1, coord.z});
                nbPX && nbPX->hasHeightData() && nbPX->config.getVertexCount() == vertCount)
            {
                const bool nbCovered = heightLayers->isCovered(nbPX->coord);
                if (tileCovered || nbCovered)
                {
                    for (uint32_t z = 0; z < vertCount; ++z)
                    {
                        weld(nbCovered,
                             tile->heightData[static_cast<size_t>(z) * vertCount + lastIdx],
                             nbPX->heightData[static_cast<size_t>(z) * vertCount]);
                    }

                    // Both sides are written when both are covered; otherwise only the covered
                    // one is. Marking whichever moved keeps the mesh, the collider and the save
                    // set honest.
                    tileWritten = tileWritten || tileCovered;
                    if (nbCovered)
                        markWritten(*nbPX);
                }
            }

            // +Z: this tile's last row == the neighbour's first row.
            if (TerrainTile* nbPZ = getTile({coord.x, coord.z + 1});
                nbPZ && nbPZ->hasHeightData() && nbPZ->config.getVertexCount() == vertCount)
            {
                const bool nbCovered = heightLayers->isCovered(nbPZ->coord);
                if (tileCovered || nbCovered)
                {
                    for (uint32_t x = 0; x < vertCount; ++x)
                    {
                        weld(nbCovered,
                             tile->heightData[static_cast<size_t>(lastIdx) * vertCount + x],
                             nbPZ->heightData[x]);
                    }

                    tileWritten = tileWritten || tileCovered;
                    if (nbCovered)
                        markWritten(*nbPZ);
                }
            }

            if (tileWritten)
                markWritten(*tile);
        }
    }

    std::vector<TerrainTile*> TerrainGrid::getVisibleTiles(const math::Frustum& frustum)
    {
        for (auto* prev : lastVisibleTiles)
            prev->isVisible = false;

        std::vector<TerrainTile*> result;
        quadtree.queryFrustum(frustum, config.worldTileSize, result);

        for (auto* tile : result)
            tile->isVisible = true;

        lastVisibleTiles = result;
        return result;
    }

    std::vector<TerrainTile*> TerrainGrid::queryFrustumPure(const math::Frustum& frustum) const
    {
        std::vector<TerrainTile*> result;
        quadtree.queryFrustum(frustum, config.worldTileSize, result);
        return result;
    }

    std::vector<TerrainTile*> TerrainGrid::getTilesInRange(const glm::vec3& center, float radius)
    {
        std::vector<TerrainTile*> result;
        quadtree.queryRange(center, radius, config.worldTileSize, result);
        return result;
    }

    std::vector<TerrainTile*> TerrainGrid::getTilesInCone(const glm::vec3& apex, const glm::vec3& dir,
                                                          float halfAngle, float maxDist)
    {
        std::vector<TerrainTile*> result;
        quadtree.queryCone(apex, dir, halfAngle, maxDist, config.worldTileSize, result);
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
                tile.setNeighbor(edge, neighborCoord);
                neighbor->setNeighbor(TileCoord::getOppositeEdge(edge), tile.coord);
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

        // Phase 3: regenerate LODs with neighbor context for correct boundary normals/stitching
        // During Phase 1, tiles were generated without neighbors (getTile = nullptr),
        // so boundary normals and edge stitching were skipped. Now that all neighbors are
        // established, regenerate all LODs with the TileLookup callback.
        TileLookup lookup = [this](const TileCoord& coord) -> const TerrainTile* {
            return getTile(coord);
        };

        if (progress)
        {
            progress(static_cast<float>(currentTile) / static_cast<float>(totalTiles), "Syncing boundaries...");
        }

        // Phase 3 runs in parallel: the tile map is structurally frozen (no insert/erase past Phase 2),
        // generateAllLODs writes only its own tile, the lookup reads neighbors read-only, the generator
        // is fully const (no shared scratch state), and GPU upload is decoupled to TerrainStreamManager.
        // Per-tile progress is dropped (string churn + ordering) — one message brackets the whole phase.
        std::vector<TerrainTile*> tilePtrs;
        tilePtrs.reserve(tiles.size());
        for (auto& [coord, tile] : tiles)
            tilePtrs.push_back(tile.get());

        const auto lodStart = std::chrono::high_resolution_clock::now();
        threading::JobSystem::instance().parallelFor(static_cast<uint32_t>(tilePtrs.size()),
            [this, &tilePtrs, &lookup](uint32_t begin, uint32_t end)
            {
                for (uint32_t i = begin; i < end; ++i)
                    generator->generateAllLODs(*tilePtrs[i], nullptr, lookup);
            }, 1);
        const auto lodMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - lodStart).count();
        vfLogInfo("TerrainGrid: Regenerated LODs for {} tiles in {} ms (parallel)", tilePtrs.size(), lodMs);

        quadtree.rebuild(tiles);

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
        quadtree.rebuild(tiles);

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
        quadtree.rebuild(tiles);

        vfLogInfo("TerrainGrid: Created {} metadata-only tiles for streaming", tiles.size());
        return !tiles.empty();
    }

    void TerrainGrid::initializeWeightMaps()
    {
        for (auto& [coord, tile] : tiles)
        {
            if (!tile->hasWeightMap())
            {
                tile->initializeWeightMap();
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
