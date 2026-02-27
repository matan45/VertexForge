#include "SceneSerialization.hpp"
#include "JsonConverters.hpp"
#include "../components/Components.hpp"
#include "../print/EditorLogger.hpp"

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
        if (auto it = j.find("showGizmo"); it != j.end() && it->is_boolean())
        {
            light.showGizmo = it->get<bool>();
        }
    }

    json SceneSerialization::serializeLightmap(const components::LightmapComponent& lm)
    {
        json j;
        j["lightmapPath"] = lm.lightmapPath;
        j["texelsPerUnit"] = lm.texelsPerUnit;
        j["atlasScaleOffset"] = json::array({
            lm.atlasScaleOffset.x, lm.atlasScaleOffset.y,
            lm.atlasScaleOffset.z, lm.atlasScaleOffset.w
        });
        return j;
    }

    void SceneSerialization::deserializeLightmap(const json& j, components::LightmapComponent& lm)
    {
        if (auto it = j.find("lightmapPath"); it != j.end() && it->is_string())
        {
            lm.lightmapPath = it->get<std::string>();
        }
        if (auto it = j.find("texelsPerUnit"); it != j.end() && it->is_number())
        {
            lm.texelsPerUnit = it->get<float>();
        }
        if (auto it = j.find("atlasScaleOffset"); it != j.end() && it->is_array() && it->size() >= 4)
        {
            lm.atlasScaleOffset = glm::vec4(
                (*it)[0].get<float>(), (*it)[1].get<float>(),
                (*it)[2].get<float>(), (*it)[3].get<float>()
            );
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
        j["lodDistances"] = json::array({
            terrain.lodDistances[0], terrain.lodDistances[1],
            terrain.lodDistances[2], terrain.lodDistances[3]
        });
        std::string cleanPath = terrain.heightmapPath;
        cleanNullTerminators(cleanPath);
        j["heightmapPath"] = cleanPath;
        if (!terrain.terrainMaterialPath.empty())
        {
            std::string cleanMatPath = terrain.terrainMaterialPath;
            cleanNullTerminators(cleanMatPath);
            j["terrainMaterialPath"] = cleanMatPath;
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
        if (auto it = j.find("lodDistances"); it != j.end() && it->is_array() && it->size() >= 4)
        {
            terrain.lodDistances[0] = (*it)[0].get<float>();
            terrain.lodDistances[1] = (*it)[1].get<float>();
            terrain.lodDistances[2] = (*it)[2].get<float>();
            terrain.lodDistances[3] = (*it)[3].get<float>();
        }
        if (auto it = j.find("heightmapPath"); it != j.end() && it->is_string())
            terrain.heightmapPath = it->get<std::string>();
        if (auto it = j.find("terrainMaterialPath"); it != j.end() && it->is_string())
            terrain.terrainMaterialPath = it->get<std::string>();
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
        j["currentLOD"] = tile.currentLOD;
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
        if (auto it = j.find("currentLOD"); it != j.end() && it->is_number_unsigned())
            tile.currentLOD = it->get<uint8_t>();
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
