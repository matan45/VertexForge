#include "TerrainService.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "terrain/TerrainGrid.hpp"
#include "terrain/TerrainTile.hpp"
#include "terrain/TerrainTypes.hpp"
#include "terrain/TerrainFileCache.hpp"
#include "terrain/TerrainFileAccess.hpp"
#include "terrain/TileHeightSampler.hpp"
#include "terrain/TerrainRVTBudget.hpp" // terrain::terrainQuadHeight
#include "terrain/HeightmapLoader.hpp"
#include "../../data/EntityConversion.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <limits>
#include <memory>
#include <string>
#include <vector>

// VK-1653 terrain authoring queries (MCP P4): which terrain an authoring command addresses, what
// terrains the scene holds, heights at arbitrary points, and whether a heightmap would load.
//
// MAIN THREAD ONLY with the render thread idle, the same contract as the authoring strokes. Not
// because these edit anything an artist would see, but because getTerrainHeights pages tiles in:
// streamInTile inserts into TerrainGrid::tiles and creates a tile entity, and getRawVisibleTiles
// walks both on the render thread.
//
// Every message here is tool-agnostic; the MCP layer adds the hints that name its own tools.

namespace services
{
    namespace
    {
        // TerrainService.cpp's resolveTerrainSamplePosition, copied rather than shared because it
        // lives in that file's anonymous namespace. Floor division in double, and anything
        // non-finite or outside int32 tile space is rejected rather than wrapped.
        bool resolveSamplePosition(float worldX, float worldZ, float worldTileSize,
                                   terrain::TileCoord& coord, float& localX, float& localZ)
        {
            if (!std::isfinite(worldX) || !std::isfinite(worldZ)
                || !std::isfinite(worldTileSize) || worldTileSize <= 0.0f)
                return false;

            const double tileX = std::floor(static_cast<double>(worldX) / worldTileSize);
            const double tileZ = std::floor(static_cast<double>(worldZ) / worldTileSize);
            constexpr double minCoord = static_cast<double>(std::numeric_limits<int32_t>::min());
            constexpr double maxCoord = static_cast<double>(std::numeric_limits<int32_t>::max());
            if (!std::isfinite(tileX) || !std::isfinite(tileZ)
                || tileX < minCoord || tileX > maxCoord || tileZ < minCoord || tileZ > maxCoord)
                return false;

            coord = {static_cast<int32_t>(tileX), static_cast<int32_t>(tileZ)};
            const double localXd = static_cast<double>(worldX) - tileX * worldTileSize;
            const double localZd = static_cast<double>(worldZ) - tileZ * worldTileSize;
            if (!std::isfinite(localXd) || !std::isfinite(localZd))
                return false;

            localX = static_cast<float>(localXd);
            localZ = static_cast<float>(localZd);
            return std::isfinite(localX) && std::isfinite(localZ);
        }

        std::string joinEntityIds(const std::vector<uint64_t>& ids)
        {
            std::string joined;
            for (size_t i = 0; i < ids.size(); ++i)
            {
                if (i != 0)
                    joined += ", ";
                joined += std::to_string(ids[i]);
            }
            return joined;
        }

        std::string lowerExtension(const std::string& path)
        {
            std::string extension = std::filesystem::path(path).extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return extension;
        }
    }

    terrain::TerrainGrid* TerrainService::resolveAuthoringGrid(EntityHandle requested,
                                                               EntityHandle& terrainOut,
                                                               std::string& errorOut)
    {
        terrainOut = EntityHandle::invalid();
        errorOut.clear();

        auto& registry = scene::EntityRegistry::getRegistry();

        // A grid counts only while its entity is still a terrain. onEntityDeleted erases the grid
        // of a terrain deleted through the scene, but an entity destroyed behind the service's back
        // would otherwise hand out a grid that nothing renders, saves or can be named again.
        const auto liveGrid = [&](uint64_t id) -> terrain::TerrainGrid*
        {
            const auto it = terrainGrids.find(id);
            if (it == terrainGrids.end() || !it->second)
                return nullptr;

            const entt::entity ent = internal::fromHandle(EntityHandle{id});
            if (!registry.valid(ent) || !registry.all_of<components::TerrainComponent>(ent))
                return nullptr;

            return it->second.get();
        };

        if (!requested.isValid())
        {
            std::vector<uint64_t> live;
            for (const auto& entry : terrainGrids)
            {
                if (liveGrid(entry.first))
                    live.push_back(entry.first);
            }
            // terrainGrids is unordered; sorting keeps the listed ids stable between calls.
            std::sort(live.begin(), live.end());

            if (live.empty())
            {
                errorOut = "no terrain in the scene";
                return nullptr;
            }

            if (live.size() > 1)
            {
                errorOut = std::format("{} terrains in the scene (ids {}); name one",
                                       live.size(), joinEntityIds(live));
                return nullptr;
            }

            terrainOut = EntityHandle{live.front()};
            return liveGrid(live.front());
        }

        // entt handles are 32-bit (EntityConversion.hpp). fromHandle would truncate a wider id, and
        // the truncated value could alias a live entity, so it is refused before the lookup.
        if (requested.id > std::numeric_limits<uint32_t>::max())
        {
            errorOut = std::format("entity {} is not a terrain (no such entity)", requested.id);
            return nullptr;
        }

        const entt::entity ent = internal::fromHandle(requested);
        if (!registry.valid(ent))
        {
            errorOut = std::format("entity {} is not a terrain (no such entity)", requested.id);
            return nullptr;
        }

        if (registry.all_of<components::TerrainComponent>(ent))
        {
            if (terrain::TerrainGrid* grid = liveGrid(requested.id))
            {
                terrainOut = requested;
                return grid;
            }

            // What a scene reload leaves when the .vfTerrain is missing or was never saved: the
            // component came back with the scene, the heights did not.
            errorOut = std::format("entity {} is a terrain with no data loaded (an empty shell)",
                                   requested.id);
            return nullptr;
        }

        // TerrainTileComponent carries only the tile's coord. The terrain it belongs to is its
        // scene-graph parent: createTileEntity adds every tile as a child of the terrain entity.
        if (registry.all_of<components::TerrainTileComponent>(ent))
        {
            entt::entity parent = entt::null;
            if (registry.all_of<components::ParentComponent>(ent))
                parent = registry.get<components::ParentComponent>(ent).parent;

            if (!registry.valid(parent) || !registry.all_of<components::TerrainComponent>(parent))
            {
                errorOut = std::format("entity {} is a terrain tile with no parent terrain", requested.id);
                return nullptr;
            }

            const EntityHandle parentHandle = internal::toHandle(parent);
            if (terrain::TerrainGrid* grid = liveGrid(parentHandle.id))
            {
                terrainOut = parentHandle;
                return grid;
            }

            errorOut = std::format("entity {} is a tile of terrain {}, which has no data loaded "
                                   "(an empty shell)", requested.id, parentHandle.id);
            return nullptr;
        }

        errorOut = std::format("entity {} is not a terrain", requested.id);
        return nullptr;
    }

    std::vector<TerrainSummary> TerrainService::listTerrains() const
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        // Every TerrainComponent, not every grid: an empty shell (a scene reload whose .vfTerrain did
        // not load) is exactly what a caller deciding what to save or delete needs to see.
        std::vector<uint64_t> ids;
        for (const entt::entity entity : registry.view<components::TerrainComponent>())
            ids.push_back(internal::toHandle(entity).id);
        std::sort(ids.begin(), ids.end());

        std::vector<TerrainSummary> summaries;
        summaries.reserve(ids.size());

        for (const uint64_t id : ids)
        {
            const EntityHandle handle{id};
            const entt::entity ent = internal::fromHandle(handle);

            TerrainSummary summary;
            summary.entity = handle;
            if (registry.all_of<components::NameComponent>(ent))
                summary.name = registry.get<components::NameComponent>(ent).name;
            if (auto data = getTerrainData(handle))
                summary.data = std::move(*data);
            summary.hasCollider = hasTerrainCollider(handle);

            // The same answer IsTerrainStreamingEnabledQuery gives.
            const auto streamerIt = worldStreamers.find(id);
            summary.streamingEnabled = streamerIt != worldStreamers.end() && streamerIt->second &&
                                       streamerIt->second->isEnabled();

            const auto gridIt = terrainGrids.find(id);
            const terrain::TerrainGrid* grid = gridIt != terrainGrids.end() ? gridIt->second.get() : nullptr;
            summary.live = grid != nullptr;

            if (grid)
            {
                grid->forEachTile([&summary](const terrain::TerrainTile& tile)
                {
                    if (tile.hasHeightData())
                        ++summary.residentHeightTiles;

                    // Exactly what TerrainGrid::regenerateDirtyTiles still owes: it rebuilds the LODs
                    // whose dirtyLODMask bit is set, on tiles flagged isDirty or edgeSyncDirty (the
                    // latter is only ever raised together with setAllLODsDirty, which sets isDirty),
                    // and the generator clears isDirty with the last bit. isDirty with an empty mask
                    // (fresh heights off disk, nothing to remesh) is not waiting on anything, so it
                    // is not counted. Same test as the VK-1648 layerMeshPending drain.
                    if (tile.isDirty && tile.dirtyLODMask != 0)
                        ++summary.pendingMeshTiles;
                });
            }

            summaries.push_back(std::move(summary));
        }

        return summaries;
    }

    std::vector<TerrainHeightSample> TerrainService::getTerrainHeights(EntityHandle terrainEntity,
                                                                       const std::vector<glm::vec2>& positions,
                                                                       bool pageIn)
    {
        std::vector<TerrainHeightSample> samples(positions.size());
        if (positions.empty())
            return samples;

        EntityHandle resolved;
        std::string unusedError;
        terrain::TerrainGrid* grid = resolveAuthoringGrid(terrainEntity, resolved, unusedError);
        if (!grid)
            return samples;

        const float worldTileSize = grid->getTileConfig().worldTileSize;

        const auto cacheIt = fileCaches.find(resolved.id);
        const std::shared_ptr<terrain::TerrainFileCache> fileCache =
            cacheIt != fileCaches.end() ? cacheIt->second : nullptr;

        for (size_t i = 0; i < positions.size(); ++i)
        {
            terrain::TileCoord coord;
            float localX = 0.0f;
            float localZ = 0.0f;
            if (!resolveSamplePosition(positions[i].x, positions[i].y, worldTileSize, coord, localX, localZ))
                continue;

            // On the terrain means the tile exists, resident or only on disk. Stricter than the
            // bounding rectangle: a sparse grid (tiles removed through RemoveTerrainTileCommand) has
            // holes inside its bounds where there is no ground at all.
            terrain::TerrainTile* tile = grid->getTile(coord);
            const bool fileCached = fileCache && fileCache->hasCoord(coord);
            samples[i].onTerrain = tile != nullptr || fileCached;
            if (!samples[i].onTerrain)
                continue;

            // Streamed out: bring it back the way the sculpt brush does (TerrainBrushOps.cpp).
            if (!tile && pageIn && fileCached)
            {
                streamInTile(resolved, coord.x, coord.z);
                tile = grid->getTile(coord);
            }
            if (!tile)
                continue;

            // Resident without heights: the state every tile of a reloaded, non-streaming terrain is
            // in (TerrainGrid::loadMetadataOnly). ensureTileHeights rather than a bare
            // TerrainFileCache::ensureHeightsLoaded, for the VK-1645 rule it adds: on a covered tile
            // the bytes off disk are the uint16-quantised composite, so the tile is marked
            // derived-stale and the next frame's recompose replaces them with the exact one.
            if (!tile->hasHeightData() && pageIn)
                grid->ensureTileHeights(*tile);
            if (!tile->hasHeightData())
                continue;

            const uint32_t vpt = tile->config.getVertexCount();
            const float vertexSpacing = tile->config.getVertexSpacing();
            if (vpt < 2 || !std::isfinite(vertexSpacing) || vertexSpacing <= 0.0f
                || tile->heightData.size() < static_cast<size_t>(vpt) * vpt)
                continue;

            // The DERIVED plane: what the mesh, the collider and a placed object stand on. Sampled
            // with terrainQuadHeight, NOT bilinear: terrain quads split on the anti-diagonal
            // (TerrainTileGenerator.cpp), and mid-quad on a ridge bilinear is off by the full ridge
            // amplitude -- an object placed from it would float above a crest or sink into a fold.
            // Same indexing as the road conform heightFn (TerrainServiceHandlers.cpp).
            const uint32_t quadCount = vpt - 1u;
            const float gx = std::clamp(localX / vertexSpacing, 0.0f, static_cast<float>(quadCount));
            const float gz = std::clamp(localZ / vertexSpacing, 0.0f, static_cast<float>(quadCount));
            const uint32_t ix = std::min(static_cast<uint32_t>(gx), quadCount - 1u);
            const uint32_t iz = std::min(static_cast<uint32_t>(gz), quadCount - 1u);
            const auto& heights = tile->heightData;
            const size_t row0 = static_cast<size_t>(iz) * vpt;
            const size_t row1 = static_cast<size_t>(iz + 1u) * vpt;
            const float height = terrain::terrainQuadHeight(
                heights[row0 + ix], heights[row0 + ix + 1u],
                heights[row1 + ix], heights[row1 + ix + 1u],
                gx - static_cast<float>(ix), gz - static_cast<float>(iz));
            if (!std::isfinite(height))
                continue;

            samples[i].height = height;
            samples[i].valid = true;
        }

        return samples;
    }

    HeightmapProbeResult TerrainService::probeHeightmap(const std::string& path) const
    {
        HeightmapProbeResult result;

        if (path.empty())
        {
            result.error = "no heightmap path given";
            return result;
        }

        // The same gate HeightmapLoader::load applies, checked first so the reason is precise: a
        // .png is the wrong format, not a file that failed to decode.
        if (lowerExtension(path) != ".vfimage")
        {
            result.error = std::format("{} is not a .vfImage (heightmaps must be imported as .vfImage)", path);
            return result;
        }

        // Through TerrainFileAccess, as the loader reads, so the probe and the creation it guards
        // agree on what exists.
        if (!terrain::terrainFileExists(path))
        {
            result.error = std::format("heightmap not found: {}", path);
            return result;
        }

        std::shared_ptr<terrain::HeightmapData> heightmap;
        try
        {
            heightmap = terrain::HeightmapLoader::load(path);
        }
        catch (const std::exception& e)
        {
            // A corrupt header can name dimensions large enough to fail the pixel allocation.
            result.error = std::format("could not decode {} as a heightmap: {}", path, e.what());
            return result;
        }

        if (!heightmap || !heightmap->isValid())
        {
            result.error = std::format("could not decode {} as a heightmap", path);
            return result;
        }

        result.valid = true;
        result.width = heightmap->width;
        result.height = heightmap->height;
        return result;
    }
}
