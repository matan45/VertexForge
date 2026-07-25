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
        j["causticStrength"] = ocean.causticStrength;
        j["causticDepthFalloff"] = ocean.causticDepthFalloff;
        j["shoreFoamRange"] = ocean.shoreFoamRange;
        j["shoreFoamIntensity"] = ocean.shoreFoamIntensity;
        j["shoreBreakingStrength"] = ocean.shoreBreakingStrength;
        j["shoreWetRange"] = ocean.shoreWetRange;
        j["shoreWetDarkening"] = ocean.shoreWetDarkening;
        j["shoreWetRoughness"] = ocean.shoreWetRoughness;

        // VK-1604
        j["ssrEnabled"] = ocean.ssrEnabled;
        j["ssrIntensity"] = ocean.ssrIntensity;
        j["ssrMaxDistance"] = ocean.ssrMaxDistance;
        j["ssrThickness"] = ocean.ssrThickness;
        j["ssrMaxSteps"] = ocean.ssrMaxSteps;
        j["ssrDebugView"] = ocean.ssrDebugView;
        j["beerLambertEnabled"] = ocean.beerLambertEnabled;
        j["absorptionCoeff"] = json::array({ocean.absorptionCoeff.x, ocean.absorptionCoeff.y,
                                             ocean.absorptionCoeff.z});
        j["scatteringColor"] = json::array({ocean.scatteringColor.x, ocean.scatteringColor.y,
                                             ocean.scatteringColor.z});
        j["scatterCoeff"] = ocean.scatterCoeff;
        j["absorptionMaxDistance"] = ocean.absorptionMaxDistance;
        j["hexTilingEnabled"] = ocean.hexTilingEnabled;
        j["hexBandMask"] = ocean.hexBandMask;
        j["hexCellScale"] = ocean.hexCellScale;
        j["hexBlendContrast"] = ocean.hexBlendContrast;
        // VK-1605
        j["shoalingEnabled"] = ocean.shoalingEnabled;
        j["shoalingStrength"] = ocean.shoalingStrength;
        j["shoalingMinDepth"] = ocean.shoalingMinDepth;
        j["shoalingWavelengthScale"] = ocean.shoalingWavelengthScale;
        j["shoalingGamma"] = ocean.shoalingGamma;
        j["shoreEdgeFadeStart"] = ocean.shoreEdgeFadeStart;
        j["shoreWavesEnabled"] = ocean.shoreWavesEnabled;
        j["shoreWaveAmplitude"] = ocean.shoreWaveAmplitude;
        j["shoreWaveLength"] = ocean.shoreWaveLength;
        j["shoreWaveSpeed"] = ocean.shoreWaveSpeed;
        j["shoreWaveBreakDepth"] = ocean.shoreWaveBreakDepth;
        j["shoreWaveBreakRange"] = ocean.shoreWaveBreakRange;
        j["shoreWaveCrestFoam"] = ocean.shoreWaveCrestFoam;
        j["shoreWaveCrestFoamThreshold"] = ocean.shoreWaveCrestFoamThreshold;
        j["shoreWaveLean"] = ocean.shoreWaveLean;

        // VK-1606
        j["rippleSimEnabled"] = ocean.rippleSimEnabled;
        j["ripplePatchSize"] = ocean.ripplePatchSize;
        j["rippleWaveSpeed"] = ocean.rippleWaveSpeed;
        j["rippleDamping"] = ocean.rippleDamping;
        j["rippleHeightScale"] = ocean.rippleHeightScale;
        j["rippleNormalScale"] = ocean.rippleNormalScale;
        j["rippleFoamGain"] = ocean.rippleFoamGain;
        j["rippleFoamScale"] = ocean.rippleFoamScale;
        j["rippleFoamDecay"] = ocean.rippleFoamDecay;
        j["rippleEdgeFadeStart"] = ocean.rippleEdgeFadeStart;

        // Ocean FFT bands
        auto bandsArray = nlohmann::json::array();
        for (uint32_t i = 0; i < components::MAX_OCEAN_BANDS; ++i)
        {
            const auto& band = ocean.oceanBands[i];
            nlohmann::json bandJson;
            bandJson["resolution"] = band.resolution;
            bandJson["patchSize"] = band.patchSize;
            bandJson["windSpeed"] = band.windSpeed;
            bandJson["windDirection"] = band.windDirection;
            bandJson["amplitude"] = band.amplitude;
            bandJson["choppiness"] = band.choppiness;
            bandJson["foamThreshold"] = band.foamThreshold;
            bandJson["displacementScale"] = band.displacementScale;
            bandJson["enabled"] = band.enabled;
            bandJson["foamPersistence"] = band.foamPersistence;
            bandJson["foamDecay"] = band.foamDecay;
            bandsArray.push_back(bandJson);
        }
        j["oceanBands"] = bandsArray;
        j["oceanGravity"] = ocean.oceanGravity;

        // Weather-driven sea state
        j["weatherDriven"] = ocean.weatherDriven;
        j["weatherResponse"] = ocean.weatherResponse;
        j["currentBeaufort"] = ocean.currentBeaufort;

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
        if (auto it = j.find("causticStrength"); it != j.end() && it->is_number())
            ocean.causticStrength = it->get<float>();
        if (auto it = j.find("causticDepthFalloff"); it != j.end() && it->is_number())
            ocean.causticDepthFalloff = it->get<float>();
        if (auto it = j.find("shoreFoamRange"); it != j.end() && it->is_number())
            ocean.shoreFoamRange = it->get<float>();
        if (auto it = j.find("shoreFoamIntensity"); it != j.end() && it->is_number())
            ocean.shoreFoamIntensity = it->get<float>();
        if (auto it = j.find("shoreBreakingStrength"); it != j.end() && it->is_number())
            ocean.shoreBreakingStrength = it->get<float>();
        if (auto it = j.find("shoreWetRange"); it != j.end() && it->is_number())
            ocean.shoreWetRange = it->get<float>();
        if (auto it = j.find("shoreWetDarkening"); it != j.end() && it->is_number())
            ocean.shoreWetDarkening = it->get<float>();
        if (auto it = j.find("shoreWetRoughness"); it != j.end() && it->is_number())
            ocean.shoreWetRoughness = it->get<float>();

        // VK-1604 — absent keys leave the component defaults (all features off), so scenes
        // saved before this story load unchanged.
        if (auto it = j.find("ssrEnabled"); it != j.end() && it->is_boolean())
            ocean.ssrEnabled = it->get<bool>();
        if (auto it = j.find("ssrIntensity"); it != j.end() && it->is_number())
            ocean.ssrIntensity = it->get<float>();
        if (auto it = j.find("ssrMaxDistance"); it != j.end() && it->is_number())
            ocean.ssrMaxDistance = it->get<float>();
        if (auto it = j.find("ssrThickness"); it != j.end() && it->is_number())
            ocean.ssrThickness = it->get<float>();
        if (auto it = j.find("ssrMaxSteps"); it != j.end() && it->is_number_unsigned())
            ocean.ssrMaxSteps = it->get<uint32_t>();
        if (auto it = j.find("ssrDebugView"); it != j.end() && it->is_boolean())
            ocean.ssrDebugView = it->get<bool>();
        if (auto it = j.find("beerLambertEnabled"); it != j.end() && it->is_boolean())
            ocean.beerLambertEnabled = it->get<bool>();
        if (auto it = j.find("absorptionCoeff"); it != j.end() && it->is_array() && it->size() >= 3)
            ocean.absorptionCoeff = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(),
                                               (*it)[2].get<float>());
        if (auto it = j.find("scatteringColor"); it != j.end() && it->is_array() && it->size() >= 3)
            ocean.scatteringColor = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(),
                                               (*it)[2].get<float>());
        if (auto it = j.find("scatterCoeff"); it != j.end() && it->is_number())
            ocean.scatterCoeff = it->get<float>();
        if (auto it = j.find("absorptionMaxDistance"); it != j.end() && it->is_number())
            ocean.absorptionMaxDistance = it->get<float>();
        if (auto it = j.find("hexTilingEnabled"); it != j.end() && it->is_boolean())
            ocean.hexTilingEnabled = it->get<bool>();
        if (auto it = j.find("hexBandMask"); it != j.end() && it->is_number_unsigned())
            ocean.hexBandMask = it->get<uint32_t>();
        if (auto it = j.find("hexCellScale"); it != j.end() && it->is_number())
            ocean.hexCellScale = it->get<float>();
        if (auto it = j.find("hexBlendContrast"); it != j.end() && it->is_number())
            ocean.hexBlendContrast = it->get<float>();
        // VK-1605 — type-guarded like the rest; a scene saved before this story keeps the defaults.
        if (auto it = j.find("shoalingEnabled"); it != j.end() && it->is_boolean())
            ocean.shoalingEnabled = it->get<bool>();
        if (auto it = j.find("shoalingStrength"); it != j.end() && it->is_number())
            ocean.shoalingStrength = it->get<float>();
        if (auto it = j.find("shoalingMinDepth"); it != j.end() && it->is_number())
            ocean.shoalingMinDepth = it->get<float>();
        if (auto it = j.find("shoalingWavelengthScale"); it != j.end() && it->is_number())
            ocean.shoalingWavelengthScale = it->get<float>();
        if (auto it = j.find("shoalingGamma"); it != j.end() && it->is_number())
            ocean.shoalingGamma = it->get<float>();
        if (auto it = j.find("shoreEdgeFadeStart"); it != j.end() && it->is_number())
            ocean.shoreEdgeFadeStart = it->get<float>();
        if (auto it = j.find("shoreWavesEnabled"); it != j.end() && it->is_boolean())
            ocean.shoreWavesEnabled = it->get<bool>();
        if (auto it = j.find("shoreWaveAmplitude"); it != j.end() && it->is_number())
            ocean.shoreWaveAmplitude = it->get<float>();
        if (auto it = j.find("shoreWaveLength"); it != j.end() && it->is_number())
            ocean.shoreWaveLength = it->get<float>();
        if (auto it = j.find("shoreWaveSpeed"); it != j.end() && it->is_number())
            ocean.shoreWaveSpeed = it->get<float>();
        if (auto it = j.find("shoreWaveBreakDepth"); it != j.end() && it->is_number())
            ocean.shoreWaveBreakDepth = it->get<float>();
        if (auto it = j.find("shoreWaveBreakRange"); it != j.end() && it->is_number())
            ocean.shoreWaveBreakRange = it->get<float>();
        if (auto it = j.find("shoreWaveCrestFoam"); it != j.end() && it->is_number())
            ocean.shoreWaveCrestFoam = it->get<float>();
        if (auto it = j.find("shoreWaveCrestFoamThreshold"); it != j.end() && it->is_number())
            ocean.shoreWaveCrestFoamThreshold = it->get<float>();
        if (auto it = j.find("shoreWaveLean"); it != j.end() && it->is_number())
            ocean.shoreWaveLean = it->get<float>();

        // VK-1606 — absent keys keep the component defaults, so pre-VK-1606 scenes load unchanged.
        if (auto it = j.find("rippleSimEnabled"); it != j.end() && it->is_boolean())
            ocean.rippleSimEnabled = it->get<bool>();
        if (auto it = j.find("ripplePatchSize"); it != j.end() && it->is_number())
            ocean.ripplePatchSize = it->get<float>();
        if (auto it = j.find("rippleWaveSpeed"); it != j.end() && it->is_number())
            ocean.rippleWaveSpeed = it->get<float>();
        if (auto it = j.find("rippleDamping"); it != j.end() && it->is_number())
            ocean.rippleDamping = it->get<float>();
        if (auto it = j.find("rippleHeightScale"); it != j.end() && it->is_number())
            ocean.rippleHeightScale = it->get<float>();
        if (auto it = j.find("rippleNormalScale"); it != j.end() && it->is_number())
            ocean.rippleNormalScale = it->get<float>();
        if (auto it = j.find("rippleFoamGain"); it != j.end() && it->is_number())
            ocean.rippleFoamGain = it->get<float>();
        if (auto it = j.find("rippleFoamScale"); it != j.end() && it->is_number())
            ocean.rippleFoamScale = it->get<float>();
        if (auto it = j.find("rippleFoamDecay"); it != j.end() && it->is_number())
            ocean.rippleFoamDecay = it->get<float>();
        if (auto it = j.find("rippleEdgeFadeStart"); it != j.end() && it->is_number())
            ocean.rippleEdgeFadeStart = it->get<float>();

        // Ocean FFT bands
        if (j.contains("oceanBands") && j["oceanBands"].is_array())
        {
            const auto& bandsArr = j["oceanBands"];
            for (uint32_t i = 0; i < std::min(static_cast<uint32_t>(bandsArr.size()), components::MAX_OCEAN_BANDS); ++i)
            {
                const auto& b = bandsArr[i];
                ocean.oceanBands[i].resolution = b.value("resolution", 256u);
                ocean.oceanBands[i].patchSize = b.value("patchSize", 100.0f);
                ocean.oceanBands[i].windSpeed = b.value("windSpeed", 8.0f);
                ocean.oceanBands[i].windDirection = b.value("windDirection", 45.0f);
                ocean.oceanBands[i].amplitude = b.value("amplitude", 0.00003f);
                ocean.oceanBands[i].choppiness = b.value("choppiness", 1.2f);
                ocean.oceanBands[i].foamThreshold = b.value("foamThreshold", -0.1f);
                ocean.oceanBands[i].displacementScale = b.value("displacementScale", 4.0f);
                ocean.oceanBands[i].enabled = b.value("enabled", true);
                ocean.oceanBands[i].foamPersistence = b.value("foamPersistence", ocean.oceanBands[i].foamPersistence);
                ocean.oceanBands[i].foamDecay = b.value("foamDecay", ocean.oceanBands[i].foamDecay);
            }
        }
        else
        {
            // Backward compat: old flat fields -> band[0]
            if (auto it = j.find("oceanResolution"); it != j.end() && it->is_number_unsigned())
                ocean.oceanBands[0].resolution = it->get<uint32_t>();
            if (auto it = j.find("oceanPatchSize"); it != j.end() && it->is_number())
                ocean.oceanBands[0].patchSize = it->get<float>();
            if (auto it = j.find("oceanWindSpeed"); it != j.end() && it->is_number())
                ocean.oceanBands[0].windSpeed = it->get<float>();
            if (auto it = j.find("oceanWindDirection"); it != j.end() && it->is_number())
                ocean.oceanBands[0].windDirection = it->get<float>();
            if (auto it = j.find("oceanAmplitude"); it != j.end() && it->is_number())
                ocean.oceanBands[0].amplitude = it->get<float>();
            if (auto it = j.find("oceanChoppiness"); it != j.end() && it->is_number())
                ocean.oceanBands[0].choppiness = it->get<float>();
            if (auto it = j.find("oceanFoamThreshold"); it != j.end() && it->is_number())
                ocean.oceanBands[0].foamThreshold = it->get<float>();
            if (auto it = j.find("oceanDisplacementScale"); it != j.end() && it->is_number())
                ocean.oceanBands[0].displacementScale = it->get<float>();
        }
        if (auto it = j.find("oceanGravity"); it != j.end() && it->is_number())
            ocean.oceanGravity = it->get<float>();

        // Weather-driven sea state (absent in older scenes -> defaults)
        if (auto it = j.find("weatherDriven"); it != j.end() && it->is_boolean())
            ocean.weatherDriven = it->get<bool>();
        if (auto it = j.find("weatherResponse"); it != j.end() && it->is_number())
            ocean.weatherResponse = it->get<float>();
        if (auto it = j.find("currentBeaufort"); it != j.end() && it->is_number())
            ocean.currentBeaufort = it->get<float>();

        // Runtime
        if (auto it = j.find("waterHeight"); it != j.end() && it->is_number())
            ocean.waterHeight = it->get<float>();
        if (auto it = j.find("isActive"); it != j.end() && it->is_boolean())
            ocean.isActive = it->get<bool>();
    }
}
