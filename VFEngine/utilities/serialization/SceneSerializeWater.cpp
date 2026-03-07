#include "SceneSerialization.hpp"
#include "JsonConverters.hpp"
#include "../components/Components.hpp"

namespace serialization
{
    json SceneSerialization::serializeWater(const components::WaterComponent& water)
    {
        json j;

        // Always store scene reference fields
        j["worldTileSize"] = water.worldTileSize;
        j["gridMinX"] = water.gridMinX;
        j["gridMinZ"] = water.gridMinZ;
        j["gridMaxX"] = water.gridMaxX;
        j["gridMaxZ"] = water.gridMaxZ;
        j["isActive"] = water.isActive;

        if (!water.savePath.empty())
        {
            // When a .vfWater file exists, store only the reference
            std::string cleanPath = water.savePath;
            std::replace(cleanPath.begin(), cleanPath.end(), '\\', '/');
            j["savePath"] = cleanPath;
        }
        else
        {
            // Inline fallback when no .vfWater file has been saved
            j["globalDensity"] = water.globalDensity;
            j["globalDrag"] = water.globalDrag;
            j["globalBuoyancyStrength"] = water.globalBuoyancyStrength;
            j["defaultWaterHeight"] = water.defaultWaterHeight;
            j["defaultWaveIntensity"] = water.defaultWaveIntensity;
            j["waveSpeed"] = water.waveSpeed;
            j["waveAmplitude"] = water.waveAmplitude;
            j["waveFrequency"] = water.waveFrequency;
            j["shallowColor"] = json::array({water.shallowColor.x, water.shallowColor.y,
                                              water.shallowColor.z, water.shallowColor.w});
            j["deepColor"] = json::array({water.deepColor.x, water.deepColor.y,
                                           water.deepColor.z, water.deepColor.w});
            j["maxVisibleDepth"] = water.maxVisibleDepth;
            j["fresnelPower"] = water.fresnelPower;
            j["dudvTiling"] = water.dudvTiling;
            j["dudvStrength"] = water.dudvStrength;
            j["waveDirectionDegrees"] = water.waveDirectionDegrees;
            j["physicsEnabled"] = water.physicsEnabled;

            // Ocean FFT
            j["oceanFFTEnabled"] = water.oceanFFTEnabled;
            j["oceanResolution"] = water.oceanResolution;
            j["oceanPatchSize"] = water.oceanPatchSize;
            j["oceanWindSpeed"] = water.oceanWindSpeed;
            j["oceanWindDirection"] = water.oceanWindDirection;
            j["oceanAmplitude"] = water.oceanAmplitude;
            j["oceanChoppiness"] = water.oceanChoppiness;
            j["oceanGravity"] = water.oceanGravity;
            j["oceanFoamThreshold"] = water.oceanFoamThreshold;
            j["oceanDisplacementScale"] = water.oceanDisplacementScale;
        }

        return j;
    }

    void SceneSerialization::deserializeWater(const json& j, components::WaterComponent& water)
    {
        // Scene reference fields (always present)
        if (auto it = j.find("worldTileSize"); it != j.end() && it->is_number())
            water.worldTileSize = it->get<float>();
        if (auto it = j.find("gridMinX"); it != j.end() && it->is_number_integer())
            water.gridMinX = it->get<int32_t>();
        if (auto it = j.find("gridMinZ"); it != j.end() && it->is_number_integer())
            water.gridMinZ = it->get<int32_t>();
        if (auto it = j.find("gridMaxX"); it != j.end() && it->is_number_integer())
            water.gridMaxX = it->get<int32_t>();
        if (auto it = j.find("gridMaxZ"); it != j.end() && it->is_number_integer())
            water.gridMaxZ = it->get<int32_t>();
        if (auto it = j.find("isActive"); it != j.end() && it->is_boolean())
            water.isActive = it->get<bool>();

        if (auto it = j.find("savePath"); it != j.end() && it->is_string())
        {
            water.savePath = it->get<std::string>();
            // Rest of data will be loaded from .vfWater file
            return;
        }

        // Inline fallback — read all settings from JSON
        if (auto it = j.find("globalDensity"); it != j.end() && it->is_number())
            water.globalDensity = it->get<float>();
        if (auto it = j.find("globalDrag"); it != j.end() && it->is_number())
            water.globalDrag = it->get<float>();
        if (auto it = j.find("globalBuoyancyStrength"); it != j.end() && it->is_number())
            water.globalBuoyancyStrength = it->get<float>();
        if (auto it = j.find("defaultWaterHeight"); it != j.end() && it->is_number())
            water.defaultWaterHeight = it->get<float>();
        if (auto it = j.find("defaultWaveIntensity"); it != j.end() && it->is_number())
            water.defaultWaveIntensity = it->get<float>();
        if (auto it = j.find("waveSpeed"); it != j.end() && it->is_number())
            water.waveSpeed = it->get<float>();
        if (auto it = j.find("waveAmplitude"); it != j.end() && it->is_number())
            water.waveAmplitude = it->get<float>();
        if (auto it = j.find("waveFrequency"); it != j.end() && it->is_number())
            water.waveFrequency = it->get<float>();
        if (auto it = j.find("shallowColor"); it != j.end() && it->is_array() && it->size() >= 4)
            water.shallowColor = glm::vec4((*it)[0].get<float>(), (*it)[1].get<float>(),
                                            (*it)[2].get<float>(), (*it)[3].get<float>());
        if (auto it = j.find("deepColor"); it != j.end() && it->is_array() && it->size() >= 4)
            water.deepColor = glm::vec4((*it)[0].get<float>(), (*it)[1].get<float>(),
                                         (*it)[2].get<float>(), (*it)[3].get<float>());
        if (auto it = j.find("maxVisibleDepth"); it != j.end() && it->is_number())
            water.maxVisibleDepth = it->get<float>();
        if (auto it = j.find("fresnelPower"); it != j.end() && it->is_number())
            water.fresnelPower = it->get<float>();
        if (auto it = j.find("dudvTiling"); it != j.end() && it->is_number())
            water.dudvTiling = it->get<float>();
        if (auto it = j.find("dudvStrength"); it != j.end() && it->is_number())
            water.dudvStrength = it->get<float>();
        if (auto it = j.find("waveDirectionDegrees"); it != j.end() && it->is_number())
            water.waveDirectionDegrees = it->get<float>();
        if (auto it = j.find("physicsEnabled"); it != j.end() && it->is_boolean())
            water.physicsEnabled = it->get<bool>();

        // Ocean FFT
        if (auto it = j.find("oceanFFTEnabled"); it != j.end() && it->is_boolean())
            water.oceanFFTEnabled = it->get<bool>();
        if (auto it = j.find("oceanResolution"); it != j.end() && it->is_number_unsigned())
            water.oceanResolution = it->get<uint32_t>();
        if (auto it = j.find("oceanPatchSize"); it != j.end() && it->is_number())
            water.oceanPatchSize = it->get<float>();
        if (auto it = j.find("oceanWindSpeed"); it != j.end() && it->is_number())
            water.oceanWindSpeed = it->get<float>();
        if (auto it = j.find("oceanWindDirection"); it != j.end() && it->is_number())
            water.oceanWindDirection = it->get<float>();
        if (auto it = j.find("oceanAmplitude"); it != j.end() && it->is_number())
            water.oceanAmplitude = it->get<float>();
        if (auto it = j.find("oceanChoppiness"); it != j.end() && it->is_number())
            water.oceanChoppiness = it->get<float>();
        if (auto it = j.find("oceanGravity"); it != j.end() && it->is_number())
            water.oceanGravity = it->get<float>();
        if (auto it = j.find("oceanFoamThreshold"); it != j.end() && it->is_number())
            water.oceanFoamThreshold = it->get<float>();
        if (auto it = j.find("oceanDisplacementScale"); it != j.end() && it->is_number())
            water.oceanDisplacementScale = it->get<float>();
    }

    json SceneSerialization::serializeWaterTile(const components::WaterTileComponent& tile)
    {
        json j;
        j["tileX"] = tile.tileX;
        j["tileZ"] = tile.tileZ;
        j["waterHeight"] = tile.waterHeight;
        j["waveIntensity"] = tile.waveIntensity;
        j["physicsEnabled"] = tile.physicsEnabled;
        j["isVisible"] = tile.isVisible;
        return j;
    }

    void SceneSerialization::deserializeWaterTile(const json& j, components::WaterTileComponent& tile)
    {
        if (auto it = j.find("tileX"); it != j.end() && it->is_number_integer())
            tile.tileX = it->get<int32_t>();
        if (auto it = j.find("tileZ"); it != j.end() && it->is_number_integer())
            tile.tileZ = it->get<int32_t>();
        if (auto it = j.find("waterHeight"); it != j.end() && it->is_number())
            tile.waterHeight = it->get<float>();
        if (auto it = j.find("waveIntensity"); it != j.end() && it->is_number())
            tile.waveIntensity = it->get<float>();
        if (auto it = j.find("physicsEnabled"); it != j.end() && it->is_boolean())
            tile.physicsEnabled = it->get<bool>();
        if (auto it = j.find("isVisible"); it != j.end() && it->is_boolean())
            tile.isVisible = it->get<bool>();
    }
}
