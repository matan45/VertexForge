#include "SceneSerialization.hpp"
#include "../components/Components.hpp"

namespace {
    template<typename T>
    void readField(const nlohmann::json& j, const char* key, T& out)
    {
        if (auto it = j.find(key); it != j.end())
            out = it->get<T>();
    }

    void deserializeColor4(const nlohmann::json& j, const char* key, glm::vec4& out)
    {
        if (auto it = j.find(key); it != j.end() && it->is_array() && it->size() >= 4)
            out = glm::vec4((*it)[0].get<float>(), (*it)[1].get<float>(),
                            (*it)[2].get<float>(), (*it)[3].get<float>());
    }

    void deserializeWind(const nlohmann::json& j, vegetation::GrassRenderConfig& cfg)
    {
        readField(j, "windStrength", cfg.windStrength);
        if (auto it = j.find("windDirection"); it != j.end() && it->is_array() && it->size() >= 3)
            cfg.windDirection = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
        readField(j, "windSpeed", cfg.windSpeed);
        readField(j, "gustStrength", cfg.gustStrength);
        readField(j, "gustFrequency", cfg.gustFrequency);
    }

    void deserializeBillboardPalette(const nlohmann::json& j,
                                     std::vector<vegetation::BillboardPaletteEntry>& palette)
    {
        if (auto it = j.find("billboardPalette"); it != j.end() && it->is_array())
        {
            palette.clear();
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
                if (e.contains("heightMin") && e["heightMin"].is_number())
                    entry.heightRange.x = e["heightMin"].get<float>();
                if (e.contains("heightMax") && e["heightMax"].is_number())
                    entry.heightRange.y = e["heightMax"].get<float>();
                if (e.contains("tintJitter") && e["tintJitter"].is_number())
                    entry.tintJitter = e["tintJitter"].get<float>();
                if (e.contains("mode") && e["mode"].is_number())
                    entry.mode = static_cast<vegetation::BillboardMode>(e["mode"].get<int>());
                if (e.contains("visible") && e["visible"].is_boolean())
                    entry.visible = e["visible"].get<bool>();
                if (e.contains("paintEnabled") && e["paintEnabled"].is_boolean())
                    entry.paintEnabled = e["paintEnabled"].get<bool>();
                palette.push_back(entry);
            }
        }
    }
}

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
            e["heightMin"] = entry.heightRange.x;
            e["heightMax"] = entry.heightRange.y;
            e["tintJitter"] = entry.tintJitter;
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

        readField(j, "enabled", grass.enabled);

        // Colors
        deserializeColor4(j, "baseColor", cfg.baseColor);
        deserializeColor4(j, "tipColor", cfg.tipColor);

        // Blade dimensions
        readField(j, "heightMin", cfg.heightMin);
        readField(j, "heightMax", cfg.heightMax);
        readField(j, "widthMin", cfg.widthMin);
        readField(j, "widthMax", cfg.widthMax);

        // Terrain limits
        readField(j, "slopeLimit", cfg.slopeLimit);
        readField(j, "densityMultiplier", cfg.densityMultiplier);

        // Fade distances
        readField(j, "fadeStartDistance", cfg.fadeStartDistance);
        readField(j, "fadeEndDistance", cfg.fadeEndDistance);

        // Wind
        deserializeWind(j, cfg);

        // Distance-based density fadeout
        readField(j, "densityFadeStartFactor", cfg.densityFadeStartFactor);
        readField(j, "minDensityScale", cfg.minDensityScale);
        readField(j, "terrainLODIntegration", cfg.terrainLODIntegration);

        // Subsurface scattering
        readField(j, "sssDistortion", cfg.sssDistortion);
        readField(j, "sssPower", cfg.sssPower);
        readField(j, "sssScale", cfg.sssScale);

        // Billboard palette
        deserializeBillboardPalette(j, grass.billboardPalette);
    }
}
