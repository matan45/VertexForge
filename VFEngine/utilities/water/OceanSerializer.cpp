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

        // Physics
        auto& phys = j["physics"];
        phys["density"] = data.density;
        phys["drag"] = data.drag;
        phys["buoyancyStrength"] = data.buoyancyStrength;

        // Ocean FFT
        auto& fft = j["oceanFFT"];
        fft["resolution"] = data.resolution;
        fft["patchSize"] = data.patchSize;
        fft["windSpeed"] = data.windSpeed;
        fft["windDirection"] = data.windDirection;
        fft["amplitude"] = data.amplitude;
        fft["choppiness"] = data.choppiness;
        fft["foamThreshold"] = data.foamThreshold;
        fft["displacementScale"] = data.displacementScale;

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
        catch (const nlohmann::json::parse_error& e)
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
        }

        // Physics
        if (j.contains("physics"))
        {
            const auto& phys = j["physics"];
            outData.density = phys.value("density", 1000.0f);
            outData.drag = phys.value("drag", 0.5f);
            outData.buoyancyStrength = phys.value("buoyancyStrength", 2.0f);
        }

        // Ocean FFT
        if (j.contains("oceanFFT"))
        {
            const auto& fft = j["oceanFFT"];
            outData.resolution = fft.value("resolution", 256u);
            outData.patchSize = fft.value("patchSize", 100.0f);
            outData.windSpeed = fft.value("windSpeed", 8.0f);
            outData.windDirection = fft.value("windDirection", 45.0f);
            outData.amplitude = fft.value("amplitude", 0.00003f);
            outData.choppiness = fft.value("choppiness", 1.2f);
            outData.foamThreshold = fft.value("foamThreshold", -0.1f);
            outData.displacementScale = fft.value("displacementScale", 4.0f);
        }

        return true;
    }
}
