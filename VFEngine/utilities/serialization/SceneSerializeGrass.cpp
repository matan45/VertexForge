#include "SceneSerialization.hpp"
#include "../components/Components.hpp"

namespace serialization
{
    json SceneSerialization::serializeGrass(const components::GrassComponent& grass)
    {
        json j;
        const auto& cfg = grass.config;

        j["enabled"] = grass.enabled;

        // Colors
        j["baseColor"] = json::array({cfg.baseColor.x, cfg.baseColor.y,
                                       cfg.baseColor.z, cfg.baseColor.w});
        j["tipColor"] = json::array({cfg.tipColor.x, cfg.tipColor.y,
                                      cfg.tipColor.z, cfg.tipColor.w});

        // Blade dimensions
        j["heightMin"] = cfg.heightMin;
        j["heightMax"] = cfg.heightMax;
        j["widthMin"] = cfg.widthMin;
        j["widthMax"] = cfg.widthMax;

        // Terrain limits
        j["slopeLimit"] = cfg.slopeLimit;
        j["densityMultiplier"] = cfg.densityMultiplier;

        // Fade distances
        j["fadeStartDistance"] = cfg.fadeStartDistance;
        j["fadeEndDistance"] = cfg.fadeEndDistance;

        // Wind
        j["windStrength"] = cfg.windStrength;
        j["windDirection"] = json::array({cfg.windDirection.x, cfg.windDirection.y, cfg.windDirection.z});
        j["windSpeed"] = cfg.windSpeed;
        j["gustStrength"] = cfg.gustStrength;
        j["gustFrequency"] = cfg.gustFrequency;

        // Distance-based density fadeout
        j["densityFadeStartFactor"] = cfg.densityFadeStartFactor;
        j["minDensityScale"] = cfg.minDensityScale;
        j["terrainLODIntegration"] = cfg.terrainLODIntegration;

        // Subsurface scattering
        j["sssDistortion"] = cfg.sssDistortion;
        j["sssPower"] = cfg.sssPower;
        j["sssScale"] = cfg.sssScale;

        // Billboard palette
        json paletteArr = json::array();
        for (const auto& entry : grass.billboardPalette)
        {
            json e;
            e["texturePath"] = entry.texturePath;
            e["weight"] = entry.weight;
            e["scaleMin"] = entry.scaleRange.x;
            e["scaleMax"] = entry.scaleRange.y;
            e["mode"] = static_cast<int>(entry.mode);
            e["visible"] = entry.visible;
            e["paintEnabled"] = entry.paintEnabled;
            paletteArr.push_back(e);
        }
        j["billboardPalette"] = paletteArr;

        return j;
    }

    void SceneSerialization::deserializeGrass(const json& j, components::GrassComponent& grass)
    {
        auto& cfg = grass.config;

        if (auto it = j.find("enabled"); it != j.end() && it->is_boolean())
            grass.enabled = it->get<bool>();

        // Colors
        if (auto it = j.find("baseColor"); it != j.end() && it->is_array() && it->size() >= 4)
            cfg.baseColor = glm::vec4((*it)[0].get<float>(), (*it)[1].get<float>(),
                                       (*it)[2].get<float>(), (*it)[3].get<float>());
        if (auto it = j.find("tipColor"); it != j.end() && it->is_array() && it->size() >= 4)
            cfg.tipColor = glm::vec4((*it)[0].get<float>(), (*it)[1].get<float>(),
                                      (*it)[2].get<float>(), (*it)[3].get<float>());

        // Blade dimensions
        if (auto it = j.find("heightMin"); it != j.end() && it->is_number())
            cfg.heightMin = it->get<float>();
        if (auto it = j.find("heightMax"); it != j.end() && it->is_number())
            cfg.heightMax = it->get<float>();
        if (auto it = j.find("widthMin"); it != j.end() && it->is_number())
            cfg.widthMin = it->get<float>();
        if (auto it = j.find("widthMax"); it != j.end() && it->is_number())
            cfg.widthMax = it->get<float>();

        // Terrain limits
        if (auto it = j.find("slopeLimit"); it != j.end() && it->is_number())
            cfg.slopeLimit = it->get<float>();
        if (auto it = j.find("densityMultiplier"); it != j.end() && it->is_number())
            cfg.densityMultiplier = it->get<float>();

        // Fade distances
        if (auto it = j.find("fadeStartDistance"); it != j.end() && it->is_number())
            cfg.fadeStartDistance = it->get<float>();
        if (auto it = j.find("fadeEndDistance"); it != j.end() && it->is_number())
            cfg.fadeEndDistance = it->get<float>();

        // Wind
        if (auto it = j.find("windStrength"); it != j.end() && it->is_number())
            cfg.windStrength = it->get<float>();
        if (auto it = j.find("windDirection"); it != j.end() && it->is_array() && it->size() >= 3)
            cfg.windDirection = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
        if (auto it = j.find("windSpeed"); it != j.end() && it->is_number())
            cfg.windSpeed = it->get<float>();
        if (auto it = j.find("gustStrength"); it != j.end() && it->is_number())
            cfg.gustStrength = it->get<float>();
        if (auto it = j.find("gustFrequency"); it != j.end() && it->is_number())
            cfg.gustFrequency = it->get<float>();

        // Distance-based density fadeout
        if (auto it = j.find("densityFadeStartFactor"); it != j.end() && it->is_number())
            cfg.densityFadeStartFactor = it->get<float>();
        if (auto it = j.find("minDensityScale"); it != j.end() && it->is_number())
            cfg.minDensityScale = it->get<float>();
        if (auto it = j.find("terrainLODIntegration"); it != j.end() && it->is_boolean())
            cfg.terrainLODIntegration = it->get<bool>();

        // Subsurface scattering
        if (auto it = j.find("sssDistortion"); it != j.end() && it->is_number())
            cfg.sssDistortion = it->get<float>();
        if (auto it = j.find("sssPower"); it != j.end() && it->is_number())
            cfg.sssPower = it->get<float>();
        if (auto it = j.find("sssScale"); it != j.end() && it->is_number())
            cfg.sssScale = it->get<float>();

        // Billboard palette
        if (auto it = j.find("billboardPalette"); it != j.end() && it->is_array())
        {
            grass.billboardPalette.clear();
            for (const auto& e : *it)
            {
                vegetation::BillboardPaletteEntry entry;
                if (e.contains("texturePath") && e["texturePath"].is_string())
                    entry.texturePath = e["texturePath"].get<std::string>();
                if (e.contains("weight") && e["weight"].is_number())
                    entry.weight = e["weight"].get<float>();
                if (e.contains("scaleMin") && e["scaleMin"].is_number())
                    entry.scaleRange.x = e["scaleMin"].get<float>();
                if (e.contains("scaleMax") && e["scaleMax"].is_number())
                    entry.scaleRange.y = e["scaleMax"].get<float>();
                if (e.contains("mode") && e["mode"].is_number())
                    entry.mode = static_cast<vegetation::BillboardMode>(e["mode"].get<int>());
                if (e.contains("visible") && e["visible"].is_boolean())
                    entry.visible = e["visible"].get<bool>();
                if (e.contains("paintEnabled") && e["paintEnabled"].is_boolean())
                    entry.paintEnabled = e["paintEnabled"].get<bool>();
                grass.billboardPalette.push_back(entry);
            }
        }
    }
}
