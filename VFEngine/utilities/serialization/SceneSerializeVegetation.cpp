#include "SceneSerialization.hpp"

namespace serialization
{
    json SceneSerialization::serializeVegetationSpecies(
        const std::unordered_map<uint32_t, vegetation::VegetationSpeciesConfig>& species)
    {
        json speciesArray = json::array();

        for (const auto& [id, config] : species)
        {
            json j;
            j["id"] = id;
            j["name"] = config.name;
            j["meshPath"] = config.meshPath;
            j["materialPath"] = config.materialPath;
            j["imposterAtlasPath"] = config.imposterAtlasPath;
            j["lod1Distance"] = config.lod1Distance;
            j["lod2Distance"] = config.lod2Distance;
            j["imposterDistance"] = config.imposterDistance;
            j["maxRenderDistance"] = config.maxRenderDistance;
            j["minScale"] = config.minScale;
            j["maxScale"] = config.maxScale;
            j["windStrength"] = config.windStrength;
            j["useGPUBillboardImposters"] = config.useGPUBillboardImposters;
            j["hasCollision"] = config.hasCollision;
            j["collisionRadius"] = config.collisionRadius;
            j["collisionHeight"] = config.collisionHeight;
            speciesArray.push_back(j);
        }

        return speciesArray;
    }

    std::vector<vegetation::VegetationSpeciesConfig> SceneSerialization::deserializeVegetationSpecies(const json& j)
    {
        std::vector<vegetation::VegetationSpeciesConfig> result;
        if (!j.is_array()) return result;

        for (const auto& speciesJson : j)
        {
            vegetation::VegetationSpeciesConfig config;

            if (auto it = speciesJson.find("name"); it != speciesJson.end() && it->is_string())
                config.name = it->get<std::string>();
            if (auto it = speciesJson.find("meshPath"); it != speciesJson.end() && it->is_string())
                config.meshPath = it->get<std::string>();
            if (auto it = speciesJson.find("materialPath"); it != speciesJson.end() && it->is_string())
                config.materialPath = it->get<std::string>();
            if (auto it = speciesJson.find("imposterAtlasPath"); it != speciesJson.end() && it->is_string())
                config.imposterAtlasPath = it->get<std::string>();
            if (auto it = speciesJson.find("lod1Distance"); it != speciesJson.end() && it->is_number())
                config.lod1Distance = it->get<float>();
            if (auto it = speciesJson.find("lod2Distance"); it != speciesJson.end() && it->is_number())
                config.lod2Distance = it->get<float>();
            if (auto it = speciesJson.find("imposterDistance"); it != speciesJson.end() && it->is_number())
                config.imposterDistance = it->get<float>();
            if (auto it = speciesJson.find("maxRenderDistance"); it != speciesJson.end() && it->is_number())
                config.maxRenderDistance = it->get<float>();
            if (auto it = speciesJson.find("minScale"); it != speciesJson.end() && it->is_number())
                config.minScale = it->get<float>();
            if (auto it = speciesJson.find("maxScale"); it != speciesJson.end() && it->is_number())
                config.maxScale = it->get<float>();
            if (auto it = speciesJson.find("windStrength"); it != speciesJson.end() && it->is_number())
                config.windStrength = it->get<float>();
            if (auto it = speciesJson.find("useGPUBillboardImposters"); it != speciesJson.end() && it->is_boolean())
                config.useGPUBillboardImposters = it->get<bool>();
            if (auto it = speciesJson.find("hasCollision"); it != speciesJson.end() && it->is_boolean())
                config.hasCollision = it->get<bool>();
            if (auto it = speciesJson.find("collisionRadius"); it != speciesJson.end() && it->is_number())
                config.collisionRadius = it->get<float>();
            if (auto it = speciesJson.find("collisionHeight"); it != speciesJson.end() && it->is_number())
                config.collisionHeight = it->get<float>();

            result.push_back(config);
        }

        return result;
    }
}
