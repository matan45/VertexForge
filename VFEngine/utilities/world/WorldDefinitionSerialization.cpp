#include "WorldDefinitionSerialization.hpp"
#include "../print/Log.hpp"
#include "../serialization/SerializationFileAccess.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>

namespace world
{
    using json = nlohmann::json;

    namespace
    {
        // VK-1599: the per-grid blocks, factored out so the primary grid can keep writing into the
        // top-level keys byte-identically while grids 1..N-1 write the same shapes inside "grids".
        json writeSectorConfig(const SectorConfig& config)
        {
            json out;
            out["sectorWorldSize"] = config.sectorWorldSize;
            out["tilesPerSector"] = config.tilesPerSector;
            out["alignedToTerrain"] = config.alignedToTerrain;
            return out;
        }

        void readSectorConfig(const json& in, SectorConfig& out)
        {
            const SectorConfig defaults;
            out.sectorWorldSize = in.value("sectorWorldSize", defaults.sectorWorldSize);
            out.tilesPerSector = in.value("tilesPerSector", defaults.tilesPerSector);
            out.alignedToTerrain = in.value("alignedToTerrain", defaults.alignedToTerrain);
        }

        json writeStreamingConfig(const SectorStreamingConfig& config)
        {
            json out;
            out["loadRadius"] = config.loadRadius;
            // VK-1591: 0 means "same as loadRadius" (no prefetch ring). Written verbatim so the
            // sentinel round-trips - resolving it here would bake loadRadius in and make a later
            // loadRadius edit silently stop widening the prefetch ring with it.
            out["prefetchRadius"] = config.prefetchRadius;
            out["unloadRadius"] = config.unloadRadius;
            out["maxLoadsPerFrame"] = config.maxLoadsPerFrame;
            out["maxPrefetchesPerFrame"] = config.maxPrefetchesPerFrame;
            out["maxUnloadsPerFrame"] = config.maxUnloadsPerFrame;
            out["maxPrefetchBytes"] = config.maxPrefetchBytes;
            // VK-1600: the other two eviction-pool budgets. Absent -> the struct's 0 =
            // unlimited default, so a pre-VK-1600 .vfworld streams byte-for-byte as it did.
            out["maxHLODProxyBytes"] = config.maxHLODProxyBytes;
            out["maxLoadedSectors"] = config.maxLoadedSectors;
            // VK-1593: predictive streaming. Like prefetchRadius above, the 0 sentinels are
            // written VERBATIM - resolving teleportThresholdSectors or the burst budgets here
            // would bake in the value they happen to derive from today and stop them tracking a
            // later edit of maxLoadsPerFrame / maxEntitiesPerFrame.
            out["lookaheadSeconds"] = config.lookaheadSeconds;
            out["viewBiasStrength"] = config.viewBiasStrength;
            out["teleportThresholdSectors"] = config.teleportThresholdSectors;
            out["burstFrames"] = config.burstFrames;
            out["maxLoadsPerFrameBurst"] = config.maxLoadsPerFrameBurst;
            out["maxEntitiesPerFrameBurst"] = config.maxEntitiesPerFrameBurst;
            out["maxEntitiesPerFrame"] = config.maxEntitiesPerFrame;
            out["maxTerrainLoadsPerFrame"] = config.maxTerrainLoadsPerFrame;
            out["maxTerrainUnloadsPerFrame"] = config.maxTerrainUnloadsPerFrame;
            out["enableGPUObjectStreaming"] = config.enableGPUObjectStreaming;
            out["editModeStreaming"] = config.editModeStreaming;
            out["hlodTier0Radius"] = config.hlodTier0Radius;
            out["hlodTier1Radius"] = config.hlodTier1Radius;
            out["hlodTier2Radius"] = config.hlodTier2Radius;
            return out;
        }

        void readStreamingConfig(const json& in, SectorStreamingConfig& out)
        {
            // VK-1588: the structs are the single source of truth for defaults - an absent key
            // resolves to its in-class initializer, never to a literal repeated here.
            const SectorStreamingConfig defaults;
            out.loadRadius = in.value("loadRadius", defaults.loadRadius);
            // VK-1591: absent -> the struct's 0 sentinel -> effectivePrefetchRadius() ==
            // loadRadius, so a pre-VK-1591 .vfworld streams byte-for-byte as it did before.
            out.prefetchRadius = in.value("prefetchRadius", defaults.prefetchRadius);
            out.unloadRadius = in.value("unloadRadius", defaults.unloadRadius);
            out.maxLoadsPerFrame = in.value("maxLoadsPerFrame", defaults.maxLoadsPerFrame);
            out.maxPrefetchesPerFrame = in.value("maxPrefetchesPerFrame", defaults.maxPrefetchesPerFrame);
            out.maxUnloadsPerFrame = in.value("maxUnloadsPerFrame", defaults.maxUnloadsPerFrame);
            out.maxPrefetchBytes = in.value("maxPrefetchBytes", defaults.maxPrefetchBytes);
            // VK-1600: absent -> 0 = unlimited -> every pool is inert, which is exactly the
            // pre-VK-1600 behaviour.
            out.maxHLODProxyBytes = in.value("maxHLODProxyBytes", defaults.maxHLODProxyBytes);
            out.maxLoadedSectors = in.value("maxLoadedSectors", defaults.maxLoadedSectors);
            // VK-1593: absent -> the struct's "off" defaults -> no lookahead, no view bias,
            // no burst window. A pre-VK-1593 .vfworld therefore streams byte-for-byte as it
            // did, which is what the teleport guard being inert at those defaults buys us.
            out.lookaheadSeconds = in.value("lookaheadSeconds", defaults.lookaheadSeconds);
            out.viewBiasStrength = in.value("viewBiasStrength", defaults.viewBiasStrength);
            out.teleportThresholdSectors = in.value("teleportThresholdSectors", defaults.teleportThresholdSectors);
            out.burstFrames = in.value("burstFrames", defaults.burstFrames);
            out.maxLoadsPerFrameBurst = in.value("maxLoadsPerFrameBurst", defaults.maxLoadsPerFrameBurst);
            out.maxEntitiesPerFrameBurst = in.value("maxEntitiesPerFrameBurst", defaults.maxEntitiesPerFrameBurst);
            out.maxEntitiesPerFrame = in.value("maxEntitiesPerFrame", defaults.maxEntitiesPerFrame);
            out.maxTerrainLoadsPerFrame = in.value("maxTerrainLoadsPerFrame", defaults.maxTerrainLoadsPerFrame);
            out.maxTerrainUnloadsPerFrame = in.value("maxTerrainUnloadsPerFrame", defaults.maxTerrainUnloadsPerFrame);
            out.enableGPUObjectStreaming = in.value("enableGPUObjectStreaming", defaults.enableGPUObjectStreaming);
            out.editModeStreaming = in.value("editModeStreaming", defaults.editModeStreaming);
            out.hlodTier0Radius = in.value("hlodTier0Radius", defaults.hlodTier0Radius);
            out.hlodTier1Radius = in.value("hlodTier1Radius", defaults.hlodTier1Radius);
            out.hlodTier2Radius = in.value("hlodTier2Radius", defaults.hlodTier2Radius);
        }

        json writeSectorPaths(const GridDefinition& grid)
        {
            json out = json::array();
            for (const auto& [coord, path] : grid.sectorFilePaths)
            {
                json entry;
                entry["x"] = coord.x;
                entry["z"] = coord.z;
                entry["path"] = path;
                out.push_back(entry);
            }
            return out;
        }

        void readSectorPaths(const json& in, GridDefinition& grid)
        {
            grid.sectorFilePaths.clear();
            if (!in.is_array())
                return;

            for (const auto& entry : in)
            {
                SectorCoord coord(entry.value("x", 0), entry.value("z", 0));
                grid.sectorFilePaths[coord] = entry.value("path", "");
            }
        }
    }

    bool WorldDefinitionSerialization::save(const WorldDefinition& definition, const std::string& filePath)
    {
        try
        {
            json worldJson;
            worldJson["version"] = "1.0";
            worldJson["name"] = definition.name;
            worldJson["terrainPath"] = definition.terrainPath;
            worldJson["waterDefinitionPath"] = definition.waterDefinitionPath;

            // VK-1599: the primary grid stays in the historical top-level keys. A world with one
            // grid named "Default" therefore writes exactly the bytes it wrote before this change,
            // and an engine build without multi-grid support could still read it.
            const GridDefinition& primary = definition.primaryGrid();

            worldJson["sectorConfig"] = writeSectorConfig(primary.sectorConfig);
            worldJson["streamingConfig"] = writeStreamingConfig(primary.streamingConfig);

            // HLOD config
            json hlodJson;
            hlodJson["enabled"] = definition.hlodConfig.enabled;
            json tiersJson = json::array();
            for (const auto& tier : definition.hlodConfig.tiers)
            {
                json t;
                t["tier"] = tier.tier;
                t["cellSize"] = tier.cellSize;
                t["displayRadius"] = tier.displayRadius;
                t["simplificationRatio"] = tier.simplificationRatio;
                tiersJson.push_back(t);
            }
            hlodJson["tiers"] = tiersJson;
            worldJson["hlodConfig"] = hlodJson;

            worldJson["sectors"] = writeSectorPaths(primary);

            // VK-1599: grids 1..N-1, plus the primary grid's name when it has been renamed away
            // from the default. Written ONLY when there is something to say - exactly the trick
            // VK-1594 used for hlodCells below - so a plain one-grid world has no "grids" key at
            // all and stays byte-identical to a pre-VK-1599 save.
            if (definition.grids.size() > 1 || primary.name != kDefaultGridName)
            {
                json gridsJson = json::array();
                for (size_t i = 0; i < definition.grids.size(); ++i)
                {
                    const GridDefinition& grid = definition.grids[i];

                    json gridEntry;
                    gridEntry["index"] = static_cast<int>(i);
                    gridEntry["name"] = grid.name;

                    // The primary grid's config and sector inventory already live in the
                    // top-level keys; repeating them here would be a second source of truth for
                    // the same values, and the two could disagree after a hand edit.
                    if (i != kPrimaryGridIndex)
                    {
                        gridEntry["sectorConfig"] = writeSectorConfig(grid.sectorConfig);
                        gridEntry["streamingConfig"] = writeStreamingConfig(grid.streamingConfig);
                        gridEntry["sectors"] = writeSectorPaths(grid);
                    }

                    gridsJson.push_back(gridEntry);
                }
                worldJson["grids"] = gridsJson;
            }

            // VK-1594: per-cell HLOD bake inventory. Written only when non-empty so a world with
            // no bakes stays byte-identical to what pre-VK-1594 produced.
            if (!definition.hlodCells.empty())
            {
                json cellsJson = json::array();
                for (const auto& [cell, path] : definition.hlodCells)
                {
                    if (path.empty())
                        continue;

                    json cellEntry;
                    cellEntry["x"] = cell.x;
                    cellEntry["z"] = cell.z;
                    cellEntry["tier"] = cell.tier;
                    cellEntry["path"] = path;
                    cellsJson.push_back(cellEntry);
                }

                // Sorted so the file does not churn between saves - hlodCells is an unordered_map,
                // and an unstable key order would show up as a spurious diff on every save.
                std::sort(cellsJson.begin(), cellsJson.end(), [](const json& a, const json& b)
                {
                    const auto at = a["tier"].get<int>();
                    const auto bt = b["tier"].get<int>();
                    if (at != bt) return at < bt;
                    const auto az = a["z"].get<int32_t>();
                    const auto bz = b["z"].get<int32_t>();
                    if (az != bz) return az < bz;
                    return a["x"].get<int32_t>() < b["x"].get<int32_t>();
                });

                worldJson["hlodCells"] = cellsJson;
            }

            std::ofstream file{filePath};
            if (!file.is_open())
            {
                vfLogError("Failed to open world file for writing: {}", filePath);
                return false;
            }

            file << worldJson.dump(2);
            file.close();

            vfLogInfo("World saved to: {}", filePath);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to save world: {}", e.what());
            return false;
        }
    }

    bool WorldDefinitionSerialization::load(const std::string& filePath, WorldDefinition& outDefinition)
    {
        try
        {
            json worldJson = serialization::readSerializationJsonFile(filePath);
            if (worldJson.is_null())
            {
                vfLogError("Failed to open world file for reading: {}", filePath);
                return false;
            }

            outDefinition.name = worldJson.value("name", "Untitled World");
            outDefinition.terrainPath = worldJson.value("terrainPath", "");
            outDefinition.waterDefinitionPath = worldJson.value("waterDefinitionPath", "");

            // VK-1599: rebuild the grid list from scratch, primary grid first. VK-1588's rule still
            // holds throughout: an absent key resolves to the struct's in-class initializer, never
            // to a literal repeated here. (The old literals had drifted - loadRadius 512 /
            // unloadRadius 640 are WORLD UNITS copy-pasted from TerrainWorldStreamer, while
            // SectorStreamingConfig means SECTOR COUNTS.)
            outDefinition.grids.assign(1, GridDefinition{});
            GridDefinition& primary = outDefinition.primaryGrid();

            if (worldJson.contains("sectorConfig"))
                readSectorConfig(worldJson["sectorConfig"], primary.sectorConfig);

            if (worldJson.contains("streamingConfig"))
                readStreamingConfig(worldJson["streamingConfig"], primary.streamingConfig);

            // HLOD config
            if (worldJson.contains("hlodConfig"))
            {
                const auto& hc = worldJson["hlodConfig"];
                outDefinition.hlodConfig.enabled = hc.value("enabled", false);
                outDefinition.hlodConfig.tiers.clear();
                if (hc.contains("tiers") && hc["tiers"].is_array())
                {
                    const HLODTierConfig tierDefaults;
                    for (const auto& t : hc["tiers"])
                    {
                        HLODTierConfig tier;
                        tier.tier = t.value("tier", tierDefaults.tier);
                        tier.cellSize = t.value("cellSize", tierDefaults.cellSize);
                        tier.displayRadius = t.value("displayRadius", tierDefaults.displayRadius);
                        tier.simplificationRatio = t.value("simplificationRatio", tierDefaults.simplificationRatio);
                        outDefinition.hlodConfig.tiers.push_back(tier);
                    }
                }
            }
            else
            {
                outDefinition.hlodConfig = HLODConfig::defaultConfig();
            }

            if (worldJson.contains("sectors"))
                readSectorPaths(worldJson["sectors"], primary);

            // VK-1599: an absent "grids" key is the normal state for a world saved before this
            // change, and for any world that only ever used one grid - the primary grid is already
            // fully populated from the top-level keys above, so there is nothing more to do.
            //
            // Entries are keyed by an explicit "index" rather than by array position so a
            // hand-edited file with a gap or a reordering still lands each grid where it belongs;
            // the vector is grown to fit and any grid the file never names keeps its defaults.
            if (worldJson.contains("grids") && worldJson["grids"].is_array())
            {
                for (const auto& entry : worldJson["grids"])
                {
                    const int rawIndex = entry.value("index", 0);
                    if (rawIndex < 0 || rawIndex >= static_cast<int>(kMaxGrids))
                    {
                        vfLogError("World file names grid index {} - outside [0, {}). Ignored.",
                                   rawIndex, static_cast<int>(kMaxGrids));
                        continue;
                    }

                    const auto index = static_cast<size_t>(rawIndex);
                    if (index >= outDefinition.grids.size())
                        outDefinition.grids.resize(index + 1);

                    GridDefinition& grid = outDefinition.grids[index];
                    grid.name = entry.value("name", grid.name);

                    // The primary grid's config and sector inventory are NOT repeated inside
                    // "grids" - they are the top-level keys, already read above. Only its name
                    // travels here.
                    if (index == kPrimaryGridIndex)
                        continue;

                    if (entry.contains("sectorConfig"))
                        readSectorConfig(entry["sectorConfig"], grid.sectorConfig);
                    if (entry.contains("streamingConfig"))
                        readStreamingConfig(entry["streamingConfig"], grid.streamingConfig);
                    if (entry.contains("sectors"))
                        readSectorPaths(entry["sectors"], grid);
                }
            }

            // VK-1594: absent key is the normal state for a world saved before this change. The
            // map stays empty and the streamer falls back to the tier-0 filename convention that
            // WorldSectorPersistenceOps probes on load, so nothing breaks.
            outDefinition.hlodCells.clear();
            if (worldJson.contains("hlodCells") && worldJson["hlodCells"].is_array())
            {
                for (const auto& entry : worldJson["hlodCells"])
                {
                    const std::string path = entry.value("path", "");
                    if (path.empty())
                        continue;

                    HLODCellCoord cell(entry.value("x", 0),
                                       entry.value("z", 0),
                                       static_cast<uint8_t>(entry.value("tier", 0)));
                    outDefinition.hlodCells[cell] = path;
                }
            }

            vfLogInfo("World loaded from: {}", filePath);
            return true;
        }
        catch (const json::parse_error& e)
        {
            vfLogError("JSON parse error in world file: {}", e.what());
            return false;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to load world file: {}", e.what());
            return false;
        }
    }

} // namespace world
