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
            worldJson["streamingConfig"] = streamingJson;

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
