#include "SceneSerialization.hpp"
#include "AssetRefSerializationHelper.hpp"
#include "JsonConverters.hpp"
#include "../components/Components.hpp"

namespace serialization
{
    // Helper to clean null terminators from strings
    static void cleanNullTerminators(std::string& str)
    {
        if (auto pos = str.find('\0'); pos != std::string::npos)
            str.resize(pos);
    }

    json SceneSerialization::serializeDirectionalLight(const components::DirectionalLightComponent& light)
    {
        json j;
        j["color"] = json::array({light.color.r, light.color.g, light.color.b});
        j["intensity"] = light.intensity;
        j["lightSize"] = light.lightSize;
        j["showGizmo"] = light.showGizmo;
        return j;
    }

    void SceneSerialization::deserializeDirectionalLight(const json& j, components::DirectionalLightComponent& light)
    {
        if (auto it = j.find("color"); it != j.end() && it->is_array() && it->size() >= 3)
        {
            light.color = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
        }
        if (auto it = j.find("intensity"); it != j.end() && it->is_number())
        {
            light.intensity = it->get<float>();
        }
        if (auto it = j.find("lightSize"); it != j.end() && it->is_number())
        {
            light.lightSize = it->get<float>();
        }
        if (auto it = j.find("showGizmo"); it != j.end() && it->is_boolean())
        {
            light.showGizmo = it->get<bool>();
        }
    }

    json SceneSerialization::serializePointLight(const components::PointLightComponent& light)
    {
        json j;
        j["color"] = json::array({light.color.r, light.color.g, light.color.b});
        j["intensity"] = light.intensity;
        j["radius"] = light.radius;
        j["lightSize"] = light.lightSize;
        j["castsShadow"] = light.castsShadow;
        j["showGizmo"] = light.showGizmo;
        return j;
    }

    void SceneSerialization::deserializePointLight(const json& j, components::PointLightComponent& light)
    {
        if (auto it = j.find("color"); it != j.end() && it->is_array() && it->size() >= 3)
        {
            light.color = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
        }
        if (auto it = j.find("intensity"); it != j.end() && it->is_number())
        {
            light.intensity = it->get<float>();
        }
        if (auto it = j.find("radius"); it != j.end() && it->is_number())
        {
            light.radius = it->get<float>();
        }
        if (auto it = j.find("lightSize"); it != j.end() && it->is_number())
        {
            light.lightSize = it->get<float>();
        }
        if (auto it = j.find("castsShadow"); it != j.end() && it->is_boolean())
        {
            light.castsShadow = it->get<bool>();
        }
        if (auto it = j.find("showGizmo"); it != j.end() && it->is_boolean())
        {
            light.showGizmo = it->get<bool>();
        }
    }

    json SceneSerialization::serializeSpotLight(const components::SpotLightComponent& light)
    {
        json j;
        j["color"] = json::array({light.color.r, light.color.g, light.color.b});
        j["intensity"] = light.intensity;
        j["innerAngle"] = light.innerAngle;
        j["outerAngle"] = light.outerAngle;
        j["range"] = light.range;
        j["lightSize"] = light.lightSize;
        j["castsShadow"] = light.castsShadow;
        j["showGizmo"] = light.showGizmo;
        return j;
    }

    void SceneSerialization::deserializeSpotLight(const json& j, components::SpotLightComponent& light)
    {
        if (auto it = j.find("color"); it != j.end() && it->is_array() && it->size() >= 3)
        {
            light.color = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
        }
        if (auto it = j.find("intensity"); it != j.end() && it->is_number())
        {
            light.intensity = it->get<float>();
        }
        if (auto it = j.find("innerAngle"); it != j.end() && it->is_number())
        {
            light.innerAngle = it->get<float>();
        }
        if (auto it = j.find("outerAngle"); it != j.end() && it->is_number())
        {
            light.outerAngle = it->get<float>();
        }
        if (auto it = j.find("range"); it != j.end() && it->is_number())
        {
            light.range = it->get<float>();
        }
        if (auto it = j.find("lightSize"); it != j.end() && it->is_number())
        {
            light.lightSize = it->get<float>();
        }
        if (auto it = j.find("castsShadow"); it != j.end() && it->is_boolean())
        {
            light.castsShadow = it->get<bool>();
        }
        if (auto it = j.find("showGizmo"); it != j.end() && it->is_boolean())
        {
            light.showGizmo = it->get<bool>();
        }
    }

    json SceneSerialization::serializeTerrain(const components::TerrainComponent& terrain)
    {
        json j;
        j["resolution"] = terrain.resolution;
        j["worldTileSize"] = terrain.worldTileSize;
        j["maxHeight"] = terrain.maxHeight;
        j["minHeight"] = terrain.minHeight;
        j["gridMinX"] = terrain.gridMinX;
        j["gridMinZ"] = terrain.gridMinZ;
        j["gridMaxX"] = terrain.gridMaxX;
        j["gridMaxZ"] = terrain.gridMaxZ;
        std::string cleanPath = terrain.heightmapPath;
        cleanNullTerminators(cleanPath);
        j["heightmapPath"] = cleanPath;
        if (!terrain.heightmapRegions.empty())
        {
            json regionsArray = json::array();
            for (const auto& region : terrain.heightmapRegions)
            {
                json rj;
                std::string cleanRegionPath = region.filePath;
                cleanNullTerminators(cleanRegionPath);
                rj["filePath"] = cleanRegionPath;
                rj["tileMinX"] = region.tileMinX;
                rj["tileMinZ"] = region.tileMinZ;
                rj["tileMaxX"] = region.tileMaxX;
                rj["tileMaxZ"] = region.tileMaxZ;
                regionsArray.push_back(rj);
            }
            j["heightmapRegions"] = regionsArray;
        }
        if (terrain.terrainMaterialRef.isValid())
        {
            writeAssetRef(j, "terrainMaterialRef", terrain.terrainMaterialRef);
        }
        if (!terrain.weightMapPath.empty())
        {
            std::string cleanWeightPath = terrain.weightMapPath;
            cleanNullTerminators(cleanWeightPath);
            j["weightMapPath"] = cleanWeightPath;
        }
        if (!terrain.savePath.empty())
        {
            std::string cleanSavePath = terrain.savePath;
            cleanNullTerminators(cleanSavePath);
            j["savePath"] = cleanSavePath;
        }
        // State flags
        j["isActive"] = terrain.isActive;
        return j;
    }

    void SceneSerialization::deserializeTerrain(const json& j, components::TerrainComponent& terrain)
    {
        if (auto it = j.find("resolution"); it != j.end() && it->is_number_unsigned())
            terrain.resolution = it->get<uint8_t>();
        if (auto it = j.find("worldTileSize"); it != j.end() && it->is_number())
            terrain.worldTileSize = it->get<float>();
        if (auto it = j.find("maxHeight"); it != j.end() && it->is_number())
            terrain.maxHeight = it->get<float>();
        if (auto it = j.find("minHeight"); it != j.end() && it->is_number())
            terrain.minHeight = it->get<float>();
        if (auto it = j.find("gridMinX"); it != j.end() && it->is_number_integer())
            terrain.gridMinX = it->get<int32_t>();
        if (auto it = j.find("gridMinZ"); it != j.end() && it->is_number_integer())
            terrain.gridMinZ = it->get<int32_t>();
        if (auto it = j.find("gridMaxX"); it != j.end() && it->is_number_integer())
            terrain.gridMaxX = it->get<int32_t>();
        if (auto it = j.find("gridMaxZ"); it != j.end() && it->is_number_integer())
            terrain.gridMaxZ = it->get<int32_t>();
        // lodDistances ignored (GPU-only LOD selection)
        if (auto it = j.find("heightmapPath"); it != j.end() && it->is_string())
            terrain.heightmapPath = it->get<std::string>();
        if (auto it = j.find("heightmapRegions"); it != j.end() && it->is_array())
        {
            terrain.heightmapRegions.clear();
            for (const auto& rj : *it)
            {
                terrain::HeightmapRegion region;
                if (auto fp = rj.find("filePath"); fp != rj.end() && fp->is_string())
                    region.filePath = fp->get<std::string>();
                if (auto v = rj.find("tileMinX"); v != rj.end() && v->is_number_integer())
                    region.tileMinX = v->get<int32_t>();
                if (auto v = rj.find("tileMinZ"); v != rj.end() && v->is_number_integer())
                    region.tileMinZ = v->get<int32_t>();
                if (auto v = rj.find("tileMaxX"); v != rj.end() && v->is_number_integer())
                    region.tileMaxX = v->get<int32_t>();
                if (auto v = rj.find("tileMaxZ"); v != rj.end() && v->is_number_integer())
                    region.tileMaxZ = v->get<int32_t>();
                terrain.heightmapRegions.push_back(region);
            }
        }
        terrain.terrainMaterialRef = readAssetRef(j, "terrainMaterialRef");
        if (auto it = j.find("weightMapPath"); it != j.end() && it->is_string())
            terrain.weightMapPath = it->get<std::string>();
        if (auto it = j.find("savePath"); it != j.end() && it->is_string())
            terrain.savePath = it->get<std::string>();
        // State flags (with backward-compatible defaults)
        if (auto it = j.find("isActive"); it != j.end() && it->is_boolean())
            terrain.isActive = it->get<bool>();
    }

    json SceneSerialization::serializeTerrainTile(const components::TerrainTileComponent& tile)
    {
        json j;
        j["tileX"] = tile.tileX;
        j["tileZ"] = tile.tileZ;
        j["isVisible"] = tile.isVisible;
        // State flags
        j["isDirty"] = tile.isDirty;
        j["isGPUResident"] = tile.isGPUResident;
        // Cached bounds
        j["boundingMinY"] = tile.boundingMinY;
        j["boundingMaxY"] = tile.boundingMaxY;
        return j;
    }

    void SceneSerialization::deserializeTerrainTile(const json& j, components::TerrainTileComponent& tile)
    {
        if (auto it = j.find("tileX"); it != j.end() && it->is_number_integer())
            tile.tileX = it->get<int32_t>();
        if (auto it = j.find("tileZ"); it != j.end() && it->is_number_integer())
            tile.tileZ = it->get<int32_t>();
        // currentLOD ignored (GPU-only LOD selection)
        if (auto it = j.find("isVisible"); it != j.end() && it->is_boolean())
            tile.isVisible = it->get<bool>();
        // State flags (with backward-compatible defaults)
        if (auto it = j.find("isDirty"); it != j.end() && it->is_boolean())
            tile.isDirty = it->get<bool>();
        if (auto it = j.find("isGPUResident"); it != j.end() && it->is_boolean())
            tile.isGPUResident = it->get<bool>();
        // Cached bounds
        if (auto it = j.find("boundingMinY"); it != j.end() && it->is_number())
            tile.boundingMinY = it->get<float>();
        if (auto it = j.find("boundingMaxY"); it != j.end() && it->is_number())
            tile.boundingMaxY = it->get<float>();
    }
}
