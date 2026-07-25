#include "OceanSerializer.hpp"
#include "../resource/VFSHelpers.hpp"
#include <nlohmann/json.hpp>
#include <fstream>

namespace ocean
{
    bool OceanSerializer::save(const std::string& path, const OceanFileData& data)
    {
        nlohmann::json j;

        // VK-1604 bumped this to 2 (adds the SSR / Beer-Lambert / hex-tiling visual keys).
        // load() does not branch on it — every key is read with a self-defaulting value(), so a
        // v1 file simply keeps the struct defaults. The field is informational.
        j["version"] = 2;
        j["waterHeight"] = data.waterHeight;
        j["physicsEnabled"] = data.physicsEnabled;

        // Visual
        auto& vis = j["visual"];
        vis["shallowColor"] = {data.shallowColor.r, data.shallowColor.g,
                               data.shallowColor.b, data.shallowColor.a};
        vis["deepColor"] = {data.deepColor.r, data.deepColor.g,
                            data.deepColor.b, data.deepColor.a};
        vis["maxVisibleDepth"] = data.maxVisibleDepth;
        vis["fresnelPower"] = data.fresnelPower;
        vis["refractionStrength"] = data.refractionStrength;
        vis["refractionChromatic"] = data.refractionChromatic;
        vis["refractionDepthScale"] = data.refractionDepthScale;
        vis["causticStrength"] = data.causticStrength;
        vis["causticDepthFalloff"] = data.causticDepthFalloff;
        vis["shoreFoamRange"] = data.shoreFoamRange;
        vis["shoreFoamIntensity"] = data.shoreFoamIntensity;
        vis["shoreBreakingStrength"] = data.shoreBreakingStrength;
        vis["shoreWetRange"] = data.shoreWetRange;
        vis["shoreWetDarkening"] = data.shoreWetDarkening;
        vis["shoreWetRoughness"] = data.shoreWetRoughness;

        // VK-1604
        vis["ssrEnabled"] = data.ssrEnabled;
        vis["ssrIntensity"] = data.ssrIntensity;
        vis["ssrMaxDistance"] = data.ssrMaxDistance;
        vis["ssrThickness"] = data.ssrThickness;
        vis["ssrMaxSteps"] = data.ssrMaxSteps;
        vis["ssrDebugView"] = data.ssrDebugView;
        vis["beerLambertEnabled"] = data.beerLambertEnabled;
        vis["absorptionCoeff"] = {data.absorptionCoeff.r, data.absorptionCoeff.g, data.absorptionCoeff.b};
        vis["scatteringColor"] = {data.scatteringColor.r, data.scatteringColor.g, data.scatteringColor.b};
        vis["scatterCoeff"] = data.scatterCoeff;
        vis["absorptionMaxDistance"] = data.absorptionMaxDistance;
        vis["hexTilingEnabled"] = data.hexTilingEnabled;
        vis["hexBandMask"] = data.hexBandMask;
        vis["hexCellScale"] = data.hexCellScale;
        vis["hexBlendContrast"] = data.hexBlendContrast;
        // VK-1605
        vis["shoalingEnabled"] = data.shoalingEnabled;
        vis["shoalingStrength"] = data.shoalingStrength;
        vis["shoalingMinDepth"] = data.shoalingMinDepth;
        vis["shoalingWavelengthScale"] = data.shoalingWavelengthScale;
        vis["shoalingGamma"] = data.shoalingGamma;
        vis["shoreEdgeFadeStart"] = data.shoreEdgeFadeStart;
        vis["shoreWavesEnabled"] = data.shoreWavesEnabled;
        vis["shoreWaveAmplitude"] = data.shoreWaveAmplitude;
        vis["shoreWaveLength"] = data.shoreWaveLength;
        vis["shoreWaveSpeed"] = data.shoreWaveSpeed;
        vis["shoreWaveBreakDepth"] = data.shoreWaveBreakDepth;
        vis["shoreWaveBreakRange"] = data.shoreWaveBreakRange;
        vis["shoreWaveCrestFoam"] = data.shoreWaveCrestFoam;
        vis["shoreWaveCrestFoamThreshold"] = data.shoreWaveCrestFoamThreshold;
        vis["shoreWaveLean"] = data.shoreWaveLean;

        // Physics
        auto& phys = j["physics"];
        phys["density"] = data.density;
        phys["drag"] = data.drag;
        phys["buoyancyStrength"] = data.buoyancyStrength;

        // Weather-driven sea state
        auto& sea = j["seaState"];
        sea["weatherDriven"] = data.weatherDriven;
        sea["weatherResponse"] = data.weatherResponse;
        sea["currentBeaufort"] = data.currentBeaufort;

        // Ocean FFT bands
        auto& fft = j["oceanFFT"];
        fft["gravity"] = data.gravity;
        auto bandsArr = nlohmann::json::array();
        for (uint32_t i = 0; i < OceanFileData::MAX_BANDS; ++i)
        {
            nlohmann::json b;
            b["resolution"] = data.bands[i].resolution;
            b["patchSize"] = data.bands[i].patchSize;
            b["windSpeed"] = data.bands[i].windSpeed;
            b["windDirection"] = data.bands[i].windDirection;
            b["amplitude"] = data.bands[i].amplitude;
            b["choppiness"] = data.bands[i].choppiness;
            b["foamThreshold"] = data.bands[i].foamThreshold;
            b["displacementScale"] = data.bands[i].displacementScale;
            b["enabled"] = data.bands[i].enabled;
            b["foamPersistence"] = data.bands[i].foamPersistence;
            b["foamDecay"] = data.bands[i].foamDecay;
            bandsArr.push_back(b);
        }
        fft["bands"] = bandsArr;

        std::ofstream file(path);
        if (!file.is_open())
        {
            return false;
        }

        file << j.dump(4);
        return true;
    }

    bool OceanSerializer::load(const std::string& path, OceanFileData& outData)
    {
        nlohmann::json j;
        try
        {
            j = resource::readJsonFile(path);
        }
        catch (const nlohmann::json::parse_error&)
        {
            return false;
        }
        if (j.is_null())
        {
            return false;
        }

        outData.waterHeight = j.value("waterHeight", 0.0f);
        outData.physicsEnabled = j.value("physicsEnabled", true);

        // Visual
        if (j.contains("visual"))
        {
            const auto& vis = j["visual"];
            if (vis.contains("shallowColor") && vis["shallowColor"].is_array())
            {
                auto& c = vis["shallowColor"];
                outData.shallowColor = glm::vec4(c[0].get<float>(), c[1].get<float>(),
                                                   c[2].get<float>(), c[3].get<float>());
            }
            if (vis.contains("deepColor") && vis["deepColor"].is_array())
            {
                auto& c = vis["deepColor"];
                outData.deepColor = glm::vec4(c[0].get<float>(), c[1].get<float>(),
                                                c[2].get<float>(), c[3].get<float>());
            }
            outData.maxVisibleDepth = vis.value("maxVisibleDepth", 10.0f);
            outData.fresnelPower = vis.value("fresnelPower", 5.0f);
            outData.refractionStrength = vis.value("refractionStrength", 0.5f);
            outData.refractionChromatic = vis.value("refractionChromatic", 0.0f);
            outData.refractionDepthScale = vis.value("refractionDepthScale", 0.2f);
            outData.causticStrength = vis.value("causticStrength", 1.0f);
            outData.causticDepthFalloff = vis.value("causticDepthFalloff", 0.5f);
            outData.shoreFoamRange = vis.value("shoreFoamRange", 3.0f);
            outData.shoreFoamIntensity = vis.value("shoreFoamIntensity", 0.8f);
            outData.shoreBreakingStrength = vis.value("shoreBreakingStrength", 0.8f);
            outData.shoreWetRange = vis.value("shoreWetRange", 5.0f);
            outData.shoreWetDarkening = vis.value("shoreWetDarkening", 0.3f);
            outData.shoreWetRoughness = vis.value("shoreWetRoughness", 0.15f);

            // VK-1604 — self-defaulting reads (fall back to the struct default), so v1 files
            // that predate these keys load with every new feature off.
            outData.ssrEnabled = vis.value("ssrEnabled", outData.ssrEnabled);
            outData.ssrIntensity = vis.value("ssrIntensity", outData.ssrIntensity);
            outData.ssrMaxDistance = vis.value("ssrMaxDistance", outData.ssrMaxDistance);
            outData.ssrThickness = vis.value("ssrThickness", outData.ssrThickness);
            outData.ssrMaxSteps = vis.value("ssrMaxSteps", outData.ssrMaxSteps);
            outData.ssrDebugView = vis.value("ssrDebugView", outData.ssrDebugView);
            outData.beerLambertEnabled = vis.value("beerLambertEnabled", outData.beerLambertEnabled);
            if (vis.contains("absorptionCoeff") && vis["absorptionCoeff"].is_array()
                && vis["absorptionCoeff"].size() >= 3)
            {
                auto& c = vis["absorptionCoeff"];
                outData.absorptionCoeff = glm::vec3(c[0].get<float>(), c[1].get<float>(), c[2].get<float>());
            }
            if (vis.contains("scatteringColor") && vis["scatteringColor"].is_array()
                && vis["scatteringColor"].size() >= 3)
            {
                auto& c = vis["scatteringColor"];
                outData.scatteringColor = glm::vec3(c[0].get<float>(), c[1].get<float>(), c[2].get<float>());
            }
            outData.scatterCoeff = vis.value("scatterCoeff", outData.scatterCoeff);
            outData.absorptionMaxDistance = vis.value("absorptionMaxDistance", outData.absorptionMaxDistance);
            outData.hexTilingEnabled = vis.value("hexTilingEnabled", outData.hexTilingEnabled);
            outData.hexBandMask = vis.value("hexBandMask", outData.hexBandMask);
            outData.hexCellScale = vis.value("hexCellScale", outData.hexCellScale);
            outData.hexBlendContrast = vis.value("hexBlendContrast", outData.hexBlendContrast);
            // VK-1605 — self-defaulting, so a v2 file written before this story loads unchanged.
            outData.shoalingEnabled = vis.value("shoalingEnabled", outData.shoalingEnabled);
            outData.shoalingStrength = vis.value("shoalingStrength", outData.shoalingStrength);
            outData.shoalingMinDepth = vis.value("shoalingMinDepth", outData.shoalingMinDepth);
            outData.shoalingWavelengthScale = vis.value("shoalingWavelengthScale", outData.shoalingWavelengthScale);
            outData.shoalingGamma = vis.value("shoalingGamma", outData.shoalingGamma);
            outData.shoreEdgeFadeStart = vis.value("shoreEdgeFadeStart", outData.shoreEdgeFadeStart);
            outData.shoreWavesEnabled = vis.value("shoreWavesEnabled", outData.shoreWavesEnabled);
            outData.shoreWaveAmplitude = vis.value("shoreWaveAmplitude", outData.shoreWaveAmplitude);
            outData.shoreWaveLength = vis.value("shoreWaveLength", outData.shoreWaveLength);
            outData.shoreWaveSpeed = vis.value("shoreWaveSpeed", outData.shoreWaveSpeed);
            outData.shoreWaveBreakDepth = vis.value("shoreWaveBreakDepth", outData.shoreWaveBreakDepth);
            outData.shoreWaveBreakRange = vis.value("shoreWaveBreakRange", outData.shoreWaveBreakRange);
            outData.shoreWaveCrestFoam = vis.value("shoreWaveCrestFoam", outData.shoreWaveCrestFoam);
            outData.shoreWaveCrestFoamThreshold = vis.value("shoreWaveCrestFoamThreshold", outData.shoreWaveCrestFoamThreshold);
            outData.shoreWaveLean = vis.value("shoreWaveLean", outData.shoreWaveLean);
        }

        // Physics
        if (j.contains("physics"))
        {
            const auto& phys = j["physics"];
            outData.density = phys.value("density", 1000.0f);
            outData.drag = phys.value("drag", 0.5f);
            outData.buoyancyStrength = phys.value("buoyancyStrength", 2.0f);
        }

        // Weather-driven sea state (absent in v1 files -> defaults)
        if (j.contains("seaState"))
        {
            const auto& sea = j["seaState"];
            outData.weatherDriven = sea.value("weatherDriven", false);
            outData.weatherResponse = sea.value("weatherResponse", 1.0f);
            outData.currentBeaufort = sea.value("currentBeaufort", 3.0f);
        }

        // Ocean FFT bands
        if (j.contains("oceanFFT"))
        {
            const auto& fft = j["oceanFFT"];
            outData.gravity = fft.value("gravity", 9.81f);

            if (fft.contains("bands") && fft["bands"].is_array())
            {
                const auto& bandsArr = fft["bands"];
                for (uint32_t i = 0; i < std::min(static_cast<uint32_t>(bandsArr.size()), OceanFileData::MAX_BANDS); ++i)
                {
                    const auto& b = bandsArr[i];
                    outData.bands[i].resolution = b.value("resolution", 256u);
                    outData.bands[i].patchSize = b.value("patchSize", 100.0f);
                    outData.bands[i].windSpeed = b.value("windSpeed", 8.0f);
                    outData.bands[i].windDirection = b.value("windDirection", 45.0f);
                    outData.bands[i].amplitude = b.value("amplitude", 0.00003f);
                    outData.bands[i].choppiness = b.value("choppiness", 1.2f);
                    outData.bands[i].foamThreshold = b.value("foamThreshold", -0.1f);
                    outData.bands[i].displacementScale = b.value("displacementScale", 4.0f);
                    outData.bands[i].enabled = b.value("enabled", true);
                    outData.bands[i].foamPersistence = b.value("foamPersistence", outData.bands[i].foamPersistence);
                    outData.bands[i].foamDecay = b.value("foamDecay", outData.bands[i].foamDecay);
                }
            }
            else
            {
                // Backward compat: old flat fields -> band[0]
                outData.bands[0].resolution = fft.value("resolution", 256u);
                outData.bands[0].patchSize = fft.value("patchSize", 100.0f);
                outData.bands[0].windSpeed = fft.value("windSpeed", 8.0f);
                outData.bands[0].windDirection = fft.value("windDirection", 45.0f);
                outData.bands[0].amplitude = fft.value("amplitude", 0.00003f);
                outData.bands[0].choppiness = fft.value("choppiness", 1.2f);
                outData.bands[0].foamThreshold = fft.value("foamThreshold", -0.1f);
                outData.bands[0].displacementScale = fft.value("displacementScale", 4.0f);
            }
        }

        return true;
    }
}
