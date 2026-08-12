#include "WorldDefinitionSerialization.hpp"
#include "../print/Log.hpp"
#include "../serialization/SerializationFileAccess.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>

namespace world
{
    using json = nlohmann::json;

    bool WorldDefinitionSerialization::save(const WorldDefinition& definition, const std::string& filePath)
    {
        try
        {
            json worldJson;
            worldJson["version"] = "1.0";
            worldJson["name"] = definition.name;
            worldJson["terrainPath"] = definition.terrainPath;
            worldJson["waterDefinitionPath"] = definition.waterDefinitionPath;

            json sectorConfigJson;
            sectorConfigJson["sectorWorldSize"] = definition.sectorConfig.sectorWorldSize;
            sectorConfigJson["tilesPerSector"] = definition.sectorConfig.tilesPerSector;
            sectorConfigJson["alignedToTerrain"] = definition.sectorConfig.alignedToTerrain;
            worldJson["sectorConfig"] = sectorConfigJson;

            json streamingJson;
            streamingJson["loadRadius"] = definition.streamingConfig.loadRadius;
            // VK-1591: 0 means "same as loadRadius" (no prefetch ring). Written verbatim so the
            // sentinel round-trips - resolving it here would bake loadRadius in and make a later
            // loadRadius edit silently stop widening the prefetch ring with it.
            streamingJson["prefetchRadius"] = definition.streamingConfig.prefetchRadius;
            streamingJson["unloadRadius"] = definition.streamingConfig.unloadRadius;
            streamingJson["maxLoadsPerFrame"] = definition.streamingConfig.maxLoadsPerFrame;
            streamingJson["maxPrefetchesPerFrame"] = definition.streamingConfig.maxPrefetchesPerFrame;
            streamingJson["maxUnloadsPerFrame"] = definition.streamingConfig.maxUnloadsPerFrame;
            streamingJson["maxPrefetchBytes"] = definition.streamingConfig.maxPrefetchBytes;
            // VK-1593: predictive streaming. Like prefetchRadius above, the 0 sentinels are
            // written VERBATIM - resolving teleportThresholdSectors or the burst budgets here
            // would bake in the value they happen to derive from today and stop them tracking a
            // later edit of maxLoadsPerFrame / maxEntitiesPerFrame.
            streamingJson["lookaheadSeconds"] = definition.streamingConfig.lookaheadSeconds;
            streamingJson["viewBiasStrength"] = definition.streamingConfig.viewBiasStrength;
            streamingJson["teleportThresholdSectors"] = definition.streamingConfig.teleportThresholdSectors;
            streamingJson["burstFrames"] = definition.streamingConfig.burstFrames;
            streamingJson["maxLoadsPerFrameBurst"] = definition.streamingConfig.maxLoadsPerFrameBurst;
            streamingJson["maxEntitiesPerFrameBurst"] = definition.streamingConfig.maxEntitiesPerFrameBurst;
            streamingJson["maxEntitiesPerFrame"] = definition.streamingConfig.maxEntitiesPerFrame;
            streamingJson["maxTerrainLoadsPerFrame"] = definition.streamingConfig.maxTerrainLoadsPerFrame;
            streamingJson["maxTerrainUnloadsPerFrame"] = definition.streamingConfig.maxTerrainUnloadsPerFrame;
            streamingJson["enableGPUObjectStreaming"] = definition.streamingConfig.enableGPUObjectStreaming;
            streamingJson["editModeStreaming"] = definition.streamingConfig.editModeStreaming;
            streamingJson["hlodTier0Radius"] = definition.streamingConfig.hlodTier0Radius;
            streamingJson["hlodTier1Radius"] = definition.streamingConfig.hlodTier1Radius;
            streamingJson["hlodTier2Radius"] = definition.streamingConfig.hlodTier2Radius;
            worldJson["streamingConfig"] = streamingJson;

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

            json sectorsJson = json::array();
            for (const auto& [coord, path] : definition.sectorFilePaths)
            {
                json sectorEntry;
                sectorEntry["x"] = coord.x;
                sectorEntry["z"] = coord.z;
                sectorEntry["path"] = path;
                sectorsJson.push_back(sectorEntry);
            }
            worldJson["sectors"] = sectorsJson;

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

            // VK-1588: the structs are the single source of truth for defaults - an absent key
            // resolves to its in-class initializer, never to a literal repeated here. The old
            // literals had drifted: loadRadius 512 / unloadRadius 640 are WORLD UNITS copy-pasted
            // from TerrainWorldStreamer, but SectorStreamingConfig means SECTOR COUNTS, so a
            // .vfworld missing those keys produced a 512-sector load ring (512 * 128 = 65536
            // units) instead of a 4-sector one.
            const SectorConfig sectorDefaults;
            const SectorStreamingConfig streamingDefaults;

            if (worldJson.contains("sectorConfig"))
            {
                const auto& sc = worldJson["sectorConfig"];
                auto& out = outDefinition.sectorConfig;
                out.sectorWorldSize = sc.value("sectorWorldSize", sectorDefaults.sectorWorldSize);
                out.tilesPerSector = sc.value("tilesPerSector", sectorDefaults.tilesPerSector);
                out.alignedToTerrain = sc.value("alignedToTerrain", sectorDefaults.alignedToTerrain);
            }
            else
            {
                outDefinition.sectorConfig = sectorDefaults;
            }

            if (worldJson.contains("streamingConfig"))
            {
                const auto& stc = worldJson["streamingConfig"];
                auto& out = outDefinition.streamingConfig;
                out.loadRadius = stc.value("loadRadius", streamingDefaults.loadRadius);
                // VK-1591: absent -> the struct's 0 sentinel -> effectivePrefetchRadius() ==
                // loadRadius, so a pre-VK-1591 .vfworld streams byte-for-byte as it did before.
                out.prefetchRadius = stc.value("prefetchRadius", streamingDefaults.prefetchRadius);
                out.unloadRadius = stc.value("unloadRadius", streamingDefaults.unloadRadius);
                out.maxLoadsPerFrame = stc.value("maxLoadsPerFrame", streamingDefaults.maxLoadsPerFrame);
                out.maxPrefetchesPerFrame = stc.value("maxPrefetchesPerFrame", streamingDefaults.maxPrefetchesPerFrame);
                out.maxUnloadsPerFrame = stc.value("maxUnloadsPerFrame", streamingDefaults.maxUnloadsPerFrame);
                out.maxPrefetchBytes = stc.value("maxPrefetchBytes", streamingDefaults.maxPrefetchBytes);
                // VK-1593: absent -> the struct's "off" defaults -> no lookahead, no view bias,
                // no burst window. A pre-VK-1593 .vfworld therefore streams byte-for-byte as it
                // did, which is what the teleport guard being inert at those defaults buys us.
                out.lookaheadSeconds = stc.value("lookaheadSeconds", streamingDefaults.lookaheadSeconds);
                out.viewBiasStrength = stc.value("viewBiasStrength", streamingDefaults.viewBiasStrength);
                out.teleportThresholdSectors = stc.value("teleportThresholdSectors", streamingDefaults.teleportThresholdSectors);
                out.burstFrames = stc.value("burstFrames", streamingDefaults.burstFrames);
                out.maxLoadsPerFrameBurst = stc.value("maxLoadsPerFrameBurst", streamingDefaults.maxLoadsPerFrameBurst);
                out.maxEntitiesPerFrameBurst = stc.value("maxEntitiesPerFrameBurst", streamingDefaults.maxEntitiesPerFrameBurst);
                out.maxEntitiesPerFrame = stc.value("maxEntitiesPerFrame", streamingDefaults.maxEntitiesPerFrame);
                out.maxTerrainLoadsPerFrame = stc.value("maxTerrainLoadsPerFrame", streamingDefaults.maxTerrainLoadsPerFrame);
                out.maxTerrainUnloadsPerFrame = stc.value("maxTerrainUnloadsPerFrame", streamingDefaults.maxTerrainUnloadsPerFrame);
                out.enableGPUObjectStreaming = stc.value("enableGPUObjectStreaming", streamingDefaults.enableGPUObjectStreaming);
                out.editModeStreaming = stc.value("editModeStreaming", streamingDefaults.editModeStreaming);
                out.hlodTier0Radius = stc.value("hlodTier0Radius", streamingDefaults.hlodTier0Radius);
                out.hlodTier1Radius = stc.value("hlodTier1Radius", streamingDefaults.hlodTier1Radius);
                out.hlodTier2Radius = stc.value("hlodTier2Radius", streamingDefaults.hlodTier2Radius);
            }
            else
            {
                outDefinition.streamingConfig = streamingDefaults;
            }

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

            outDefinition.sectorFilePaths.clear();
            if (worldJson.contains("sectors") && worldJson["sectors"].is_array())
            {
                for (const auto& entry : worldJson["sectors"])
                {
                    SectorCoord coord(entry.value("x", 0), entry.value("z", 0));
                    outDefinition.sectorFilePaths[coord] = entry.value("path", "");
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
