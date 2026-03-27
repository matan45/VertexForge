#include "OceanSerializer.hpp"
#include <nlohmann/json.hpp>
#include <fstream>

namespace ocean
{
    bool OceanSerializer::save(const std::string& path, const OceanFileData& data)
    {
        nlohmann::json j;

        j["version"] = 1;
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

        // Physics
        auto& phys = j["physics"];
        phys["density"] = data.density;
        phys["drag"] = data.drag;
        phys["buoyancyStrength"] = data.buoyancyStrength;

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
        std::ifstream file(path);
        if (!file.is_open())
        {
            return false;
        }

        nlohmann::json j;
        try
        {
            file >> j;
        }
        catch (const nlohmann::json::parse_error&)
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
        }

        // Physics
        if (j.contains("physics"))
        {
            const auto& phys = j["physics"];
            outData.density = phys.value("density", 1000.0f);
            outData.drag = phys.value("drag", 0.5f);
            outData.buoyancyStrength = phys.value("buoyancyStrength", 2.0f);
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
