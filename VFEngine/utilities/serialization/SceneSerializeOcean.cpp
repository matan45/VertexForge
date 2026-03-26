#include "SceneSerialization.hpp"
#include "JsonConverters.hpp"
#include "../components/Components.hpp"

namespace serialization
{
    json SceneSerialization::serializeOcean(const components::OceanComponent& ocean)
    {
        json j;

        // Physics
        j["density"] = ocean.density;
        j["drag"] = ocean.drag;
        j["buoyancyStrength"] = ocean.buoyancyStrength;
        j["physicsEnabled"] = ocean.physicsEnabled;

        // Visual
        j["shallowColor"] = json::array({ocean.shallowColor.x, ocean.shallowColor.y,
                                          ocean.shallowColor.z, ocean.shallowColor.w});
        j["deepColor"] = json::array({ocean.deepColor.x, ocean.deepColor.y,
                                       ocean.deepColor.z, ocean.deepColor.w});
        j["maxVisibleDepth"] = ocean.maxVisibleDepth;
        j["fresnelPower"] = ocean.fresnelPower;
        j["refractionStrength"] = ocean.refractionStrength;
        j["refractionChromatic"] = ocean.refractionChromatic;
        j["refractionDepthScale"] = ocean.refractionDepthScale;

        // Ocean FFT
        j["oceanResolution"] = ocean.oceanResolution;
        j["oceanPatchSize"] = ocean.oceanPatchSize;
        j["oceanWindSpeed"] = ocean.oceanWindSpeed;
        j["oceanWindDirection"] = ocean.oceanWindDirection;
        j["oceanAmplitude"] = ocean.oceanAmplitude;
        j["oceanChoppiness"] = ocean.oceanChoppiness;
        j["oceanFoamThreshold"] = ocean.oceanFoamThreshold;
        j["oceanDisplacementScale"] = ocean.oceanDisplacementScale;

        // Runtime
        j["waterHeight"] = ocean.waterHeight;
        j["isActive"] = ocean.isActive;

        return j;
    }

    void SceneSerialization::deserializeOcean(const json& j, components::OceanComponent& ocean)
    {
        // Physics
        if (auto it = j.find("density"); it != j.end() && it->is_number())
            ocean.density = it->get<float>();
        if (auto it = j.find("drag"); it != j.end() && it->is_number())
            ocean.drag = it->get<float>();
        if (auto it = j.find("buoyancyStrength"); it != j.end() && it->is_number())
            ocean.buoyancyStrength = it->get<float>();
        if (auto it = j.find("physicsEnabled"); it != j.end() && it->is_boolean())
            ocean.physicsEnabled = it->get<bool>();

        // Visual
        if (auto it = j.find("shallowColor"); it != j.end() && it->is_array() && it->size() >= 4)
            ocean.shallowColor = glm::vec4((*it)[0].get<float>(), (*it)[1].get<float>(),
                                            (*it)[2].get<float>(), (*it)[3].get<float>());
        if (auto it = j.find("deepColor"); it != j.end() && it->is_array() && it->size() >= 4)
            ocean.deepColor = glm::vec4((*it)[0].get<float>(), (*it)[1].get<float>(),
                                         (*it)[2].get<float>(), (*it)[3].get<float>());
        if (auto it = j.find("maxVisibleDepth"); it != j.end() && it->is_number())
            ocean.maxVisibleDepth = it->get<float>();
        if (auto it = j.find("fresnelPower"); it != j.end() && it->is_number())
            ocean.fresnelPower = it->get<float>();
        if (auto it = j.find("refractionStrength"); it != j.end() && it->is_number())
            ocean.refractionStrength = it->get<float>();
        if (auto it = j.find("refractionChromatic"); it != j.end() && it->is_number())
            ocean.refractionChromatic = it->get<float>();
        if (auto it = j.find("refractionDepthScale"); it != j.end() && it->is_number())
            ocean.refractionDepthScale = it->get<float>();

        // Ocean FFT
        if (auto it = j.find("oceanResolution"); it != j.end() && it->is_number_unsigned())
            ocean.oceanResolution = it->get<uint32_t>();
        if (auto it = j.find("oceanPatchSize"); it != j.end() && it->is_number())
            ocean.oceanPatchSize = it->get<float>();
        if (auto it = j.find("oceanWindSpeed"); it != j.end() && it->is_number())
            ocean.oceanWindSpeed = it->get<float>();
        if (auto it = j.find("oceanWindDirection"); it != j.end() && it->is_number())
            ocean.oceanWindDirection = it->get<float>();
        if (auto it = j.find("oceanAmplitude"); it != j.end() && it->is_number())
            ocean.oceanAmplitude = it->get<float>();
        if (auto it = j.find("oceanChoppiness"); it != j.end() && it->is_number())
            ocean.oceanChoppiness = it->get<float>();
        if (auto it = j.find("oceanFoamThreshold"); it != j.end() && it->is_number())
            ocean.oceanFoamThreshold = it->get<float>();
        if (auto it = j.find("oceanDisplacementScale"); it != j.end() && it->is_number())
            ocean.oceanDisplacementScale = it->get<float>();

        // Runtime
        if (auto it = j.find("waterHeight"); it != j.end() && it->is_number())
            ocean.waterHeight = it->get<float>();
        if (auto it = j.find("isActive"); it != j.end() && it->is_boolean())
            ocean.isActive = it->get<bool>();
    }
}
