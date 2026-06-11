#include "WorldDefinitionSerialization.hpp"
#include "../print/Log.hpp"
#include "../resource/VFSHelpers.hpp"
#include <nlohmann/json.hpp>
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
            streamingJson["unloadRadius"] = definition.streamingConfig.unloadRadius;
            streamingJson["maxLoadsPerFrame"] = definition.streamingConfig.maxLoadsPerFrame;
            streamingJson["maxUnloadsPerFrame"] = definition.streamingConfig.maxUnloadsPerFrame;
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
            json worldJson = resource::readJsonFile(filePath);
            if (worldJson.is_null())
            {
                vfLogError("Failed to open world file for reading: {}", filePath);
                return false;
            }

            outDefinition.name = worldJson.value("name", "Untitled World");
            outDefinition.terrainPath = worldJson.value("terrainPath", "");
            outDefinition.waterDefinitionPath = worldJson.value("waterDefinitionPath", "");

            if (worldJson.contains("sectorConfig"))
            {
                const auto& sc = worldJson["sectorConfig"];
                outDefinition.sectorConfig.sectorWorldSize = sc.value("sectorWorldSize", 128.0f);
                outDefinition.sectorConfig.tilesPerSector = sc.value("tilesPerSector", 4);
                outDefinition.sectorConfig.alignedToTerrain = sc.value("alignedToTerrain", false);
            }

            if (worldJson.contains("streamingConfig"))
            {
                const auto& stc = worldJson["streamingConfig"];
                outDefinition.streamingConfig.loadRadius = stc.value("loadRadius", 512.0f);
                outDefinition.streamingConfig.unloadRadius = stc.value("unloadRadius", 640.0f);
                outDefinition.streamingConfig.maxLoadsPerFrame = stc.value("maxLoadsPerFrame", 1);
                outDefinition.streamingConfig.maxUnloadsPerFrame = stc.value("maxUnloadsPerFrame", 1);
                outDefinition.streamingConfig.maxEntitiesPerFrame = stc.value("maxEntitiesPerFrame", 8);
                outDefinition.streamingConfig.maxTerrainLoadsPerFrame = stc.value("maxTerrainLoadsPerFrame", 4);
                outDefinition.streamingConfig.maxTerrainUnloadsPerFrame = stc.value("maxTerrainUnloadsPerFrame", 4);
                outDefinition.streamingConfig.enableGPUObjectStreaming = stc.value("enableGPUObjectStreaming", true);
                outDefinition.streamingConfig.editModeStreaming = stc.value("editModeStreaming", false);
                outDefinition.streamingConfig.hlodTier0Radius = stc.value("hlodTier0Radius", 10.0f);
                outDefinition.streamingConfig.hlodTier1Radius = stc.value("hlodTier1Radius", 20.0f);
                outDefinition.streamingConfig.hlodTier2Radius = stc.value("hlodTier2Radius", 40.0f);
            }

            // HLOD config
            if (worldJson.contains("hlodConfig"))
            {
                const auto& hc = worldJson["hlodConfig"];
                outDefinition.hlodConfig.enabled = hc.value("enabled", false);
                outDefinition.hlodConfig.tiers.clear();
                if (hc.contains("tiers") && hc["tiers"].is_array())
                {
                    for (const auto& t : hc["tiers"])
                    {
                        HLODTierConfig tier;
                        tier.tier = t.value("tier", uint8_t(0));
                        tier.cellSize = t.value("cellSize", uint8_t(1));
                        tier.displayRadius = t.value("displayRadius", 10.0f);
                        tier.simplificationRatio = t.value("simplificationRatio", 0.1f);
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
