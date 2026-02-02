#include "TerrainGrid.hpp"
#include <algorithm>
#include <cmath>

namespace terrain
{
    TerrainGrid::TerrainGrid(const TerrainTileConfig& config)
        : config_(config)
        , generator_(std::make_unique<TerrainTileGenerator>(config))
    {
    }

    void TerrainGrid::setConfig(const TerrainTileConfig& config)
    {
        config_ = config;
        generator_->setConfig(config);
    }

    void TerrainGrid::setHeightSampler(HeightSampler sampler)
    {
        generator_->setHeightSampler(std::move(sampler));
    }

    TerrainTile* TerrainGrid::getTile(const TileCoord& coord)
    {
        auto it = tiles_.find(coord);
        return (it != tiles_.end()) ? it->second.get() : nullptr;
    }

    const TerrainTile* TerrainGrid::getTile(const TileCoord& coord) const
    {
        auto it = tiles_.find(coord);
        return (it != tiles_.end()) ? it->second.get() : nullptr;
    }

    TerrainTile* TerrainGrid::getOrCreateTile(const TileCoord& coord)
    {
        auto it = tiles_.find(coord);
        if (it != tiles_.end())
        {
            return it->second.get();
        }

        // Create new tile
        auto tile = generator_->generateTile(coord);
        TerrainTile* tilePtr = tile.get();
        tiles_.emplace(coord, std::move(tile));

        // Update neighbor references
        updateNeighborReferences(*tilePtr);

        invalidateBoundsCache();
        return tilePtr;
    }

    void TerrainGrid::removeTile(const TileCoord& coord)
    {
        auto it = tiles_.find(coord);
        if (it == tiles_.end())
            return;

        // Clear neighbor references in adjacent tiles
        for (uint8_t i = 0; i < 4; ++i)
        {
            TileEdge edge = static_cast<TileEdge>(i);
            TileCoord neighborCoord = coord + TileCoord::getNeighborOffset(edge);

            TerrainTile* neighbor = getTile(neighborCoord);
            if (neighbor)
            {
                neighbor->clearNeighbor(TileCoord::getOppositeEdge(edge));
            }
        }

        tiles_.erase(it);
        invalidateBoundsCache();
    }

    void TerrainGrid::clear()
    {
        tiles_.clear();
        invalidateBoundsCache();
    }

    TileCoord TerrainGrid::worldToTileCoord(float worldX, float worldZ) const
    {
        return TileCoord(
            static_cast<int32_t>(std::floor(worldX / config_.worldTileSize)),
            static_cast<int32_t>(std::floor(worldZ / config_.worldTileSize))
        );
    }

    glm::vec2 TerrainGrid::tileCoordToWorld(const TileCoord& coord) const
    {
        return glm::vec2(
            static_cast<float>(coord.x) * config_.worldTileSize,
            static_cast<float>(coord.z) * config_.worldTileSize
        );
    }

    glm::vec3 TerrainGrid::tileCoordToWorldCenter(const TileCoord& coord) const
    {
        float halfSize = config_.worldTileSize * 0.5f;
        return glm::vec3(
            static_cast<float>(coord.x) * config_.worldTileSize + halfSize,
            0.0f,
            static_cast<float>(coord.z) * config_.worldTileSize + halfSize
        );
    }

    std::vector<TerrainTile*> TerrainGrid::getTilesInRadius(
        const glm::vec3& center,
        float radius)
    {
        std::vector<TerrainTile*> result;

        // Calculate tile coordinate range
        TileCoord minCoord = worldToTileCoord(center.x - radius, center.z - radius);
        TileCoord maxCoord = worldToTileCoord(center.x + radius, center.z + radius);

        float radiusSq = radius * radius;

        for (int32_t z = minCoord.z; z <= maxCoord.z; ++z)
        {
            for (int32_t x = minCoord.x; x <= maxCoord.x; ++x)
            {
                TileCoord coord(x, z);
                TerrainTile* tile = getTile(coord);

                if (tile)
                {
                    // Check if tile bounds intersect with sphere
                    glm::vec3 tileCenter = tile->worldBounds.getCenter();
                    float distSq = glm::dot(tileCenter - center, tileCenter - center);

                    // Rough check using tile diagonal
                    float tileRadius = config_.worldTileSize * 0.707f;  // sqrt(2)/2
                    if (distSq <= (radius + tileRadius) * (radius + tileRadius))
                    {
                        result.push_back(tile);
                    }
                }
            }
        }

        return result;
    }

    std::vector<TerrainTile*> TerrainGrid::getVisibleTiles(const math::Frustum& frustum)
    {
        std::vector<TerrainTile*> result;
        result.reserve(tiles_.size());

        for (auto& [coord, tile] : tiles_)
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

    std::vector<TerrainTile*> TerrainGrid::getTilesInAABB(const math::AABB& bounds)
    {
        std::vector<TerrainTile*> result;

        TileCoord minCoord = worldToTileCoord(bounds.min.x, bounds.min.z);
        TileCoord maxCoord = worldToTileCoord(bounds.max.x, bounds.max.z);

        for (int32_t z = minCoord.z; z <= maxCoord.z; ++z)
        {
            for (int32_t x = minCoord.x; x <= maxCoord.x; ++x)
            {
                TerrainTile* tile = getTile(TileCoord(x, z));
                if (tile)
                {
                    result.push_back(tile);
                }
            }
        }

        return result;
    }

    std::optional<float> TerrainGrid::sampleHeight(float worldX, float worldZ) const
    {
        TileCoord coord = worldToTileCoord(worldX, worldZ);
        const TerrainTile* tile = getTile(coord);

        if (!tile)
        {
            return std::nullopt;
        }

        return tile->sampleHeightWorld(worldX, worldZ);
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
        for (auto& [coord, tile] : tiles_)
        {
            updateNeighborReferences(*tile);
        }
    }

    std::vector<TileCoord> TerrainGrid::updateLODs(const glm::vec3& cameraPosition)
    {
        std::vector<TileCoord> changedTiles;
        changedTiles.reserve(tiles_.size() / 4); // Estimate ~25% tiles change per frame

        for (auto& [coord, tile] : tiles_)
        {
            uint32_t newLOD = generator_->calculateLOD(cameraPosition, *tile);
            if (newLOD != tile->currentLOD)
            {
                tile->currentLOD = static_cast<uint8_t>(newLOD);
                changedTiles.push_back(coord);
            }
        }

        // Update neighbor LOD references after all LODs are calculated
        updateAllNeighborReferences();

        // Update edge stitching for LOD transitions between neighbors
        // and mark tiles dirty if stitching requirements changed
        for (auto& [coord, tile] : tiles_)
        {
            generator_->updateEdgeStitching(*tile);

            // Check if stitching changed - if so, mark tile for regeneration
            if (tile->stitchingChanged())
            {
                tile->isDirty = true;
                tile->saveStitchState();

                // Add to changed list if not already there (LOD didn't change but stitching did)
                if (std::find(changedTiles.begin(), changedTiles.end(), coord) == changedTiles.end())
                {
                    changedTiles.push_back(coord);
                }
            }
        }

        return changedTiles;
    }

    void TerrainGrid::regenerateDirtyTiles(ProgressCallback progress)
    {
        std::vector<TerrainTile*> dirtyTiles;
        for (auto& [coord, tile] : tiles_)
        {
            if (tile->isDirty)
            {
                dirtyTiles.push_back(tile.get());
            }
        }

        if (dirtyTiles.empty())
            return;

        for (size_t i = 0; i < dirtyTiles.size(); ++i)
        {
            if (progress)
            {
                progress(static_cast<float>(i) / static_cast<float>(dirtyTiles.size()),
                         "Regenerating tile " + std::to_string(i + 1) + "/" + std::to_string(dirtyTiles.size()));
            }

            generator_->generateAllLODs(*dirtyTiles[i]);
        }

        invalidateBoundsCache();

        if (progress)
        {
            progress(1.0f, "Complete");
        }
    }

    std::vector<TerrainTile*> TerrainGrid::getAllTiles()
    {
        std::vector<TerrainTile*> result;
        result.reserve(tiles_.size());

        for (auto& [coord, tile] : tiles_)
        {
            result.push_back(tile.get());
        }

        return result;
    }

    std::vector<const TerrainTile*> TerrainGrid::getAllTiles() const
    {
        std::vector<const TerrainTile*> result;
        result.reserve(tiles_.size());

        for (const auto& [coord, tile] : tiles_)
        {
            result.push_back(tile.get());
        }

        return result;
    }

    bool TerrainGrid::hasTile(const TileCoord& coord) const
    {
        return tiles_.find(coord) != tiles_.end();
    }

    math::AABB TerrainGrid::getWorldBounds() const
    {
        if (boundsDirty_)
        {
            if (tiles_.empty())
            {
                cachedWorldBounds_ = math::AABB();
            }
            else
            {
                auto it = tiles_.begin();
                cachedWorldBounds_ = it->second->worldBounds;

                for (++it; it != tiles_.end(); ++it)
                {
                    cachedWorldBounds_.expand(it->second->worldBounds.min);
                    cachedWorldBounds_.expand(it->second->worldBounds.max);
                }
            }
            boundsDirty_ = false;
        }

        return cachedWorldBounds_;
    }

    void TerrainGrid::createGrid(int32_t minX, int32_t minZ, int32_t maxX, int32_t maxZ,
                                  ProgressCallback progress)
    {
        int32_t totalTiles = (maxX - minX + 1) * (maxZ - minZ + 1);
        int32_t currentTile = 0;

        for (int32_t z = minZ; z <= maxZ; ++z)
        {
            for (int32_t x = minX; x <= maxX; ++x)
            {
                if (progress)
                {
                    progress(static_cast<float>(currentTile) / static_cast<float>(totalTiles),
                             "Creating tile (" + std::to_string(x) + ", " + std::to_string(z) + ")");
                }

                (void)getOrCreateTile(TileCoord(x, z));
                ++currentTile;
            }
        }

        if (progress)
        {
            progress(1.0f, "Complete");
        }
    }

    void TerrainGrid::invalidateBoundsCache()
    {
        boundsDirty_ = true;
    }

} // namespace terrain
