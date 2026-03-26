#include "OceanSerializer.hpp"
#include "print/Log.hpp"
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
        vis["shallowColor"] = {data.visual.shallowColor.r, data.visual.shallowColor.g,
                               data.visual.shallowColor.b, data.visual.shallowColor.a};
        vis["deepColor"] = {data.visual.deepColor.r, data.visual.deepColor.g,
                            data.visual.deepColor.b, data.visual.deepColor.a};
        vis["maxVisibleDepth"] = data.visual.maxVisibleDepth;
        vis["fresnelPower"] = data.visual.fresnelPower;

        // Physics
        auto& phys = j["physics"];
        phys["density"] = data.physics.density;
        phys["drag"] = data.physics.drag;
        phys["buoyancyStrength"] = data.physics.buoyancyStrength;

        // Ocean FFT
        auto& fft = j["oceanFFT"];
        fft["resolution"] = data.fftConfig.resolution;
        fft["patchSize"] = data.fftConfig.patchSize;
        fft["windSpeed"] = data.fftConfig.windSpeed;
        fft["windDirection"] = data.fftConfig.windDirection;
        fft["amplitude"] = data.fftConfig.amplitude;
        fft["choppiness"] = data.fftConfig.choppiness;
        fft["foamThreshold"] = data.fftConfig.foamThreshold;
        fft["displacementScale"] = data.fftConfig.displacementScale;

        std::ofstream file(path);
        if (!file.is_open())
        {
            vfLogError("OceanSerializer: Failed to open file for writing: {}", path);
            return false;
        }

        file << j.dump(4);
        vfLogInfo("OceanSerializer: Saved ocean to {}", path);
        return true;
    }

    bool OceanSerializer::load(const std::string& path, OceanFileData& outData)
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            vfLogError("OceanSerializer: Failed to open file: {}", path);
            return false;
        }

        nlohmann::json j;
        try
        {
            file >> j;
        }
        catch (const nlohmann::json::parse_error& e)
        {
            vfLogError("OceanSerializer: JSON parse error in {}: {}", path, e.what());
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
                outData.visual.shallowColor = glm::vec4(c[0].get<float>(), c[1].get<float>(),
                                                         c[2].get<float>(), c[3].get<float>());
            }
            if (vis.contains("deepColor") && vis["deepColor"].is_array())
            {
                auto& c = vis["deepColor"];
                outData.visual.deepColor = glm::vec4(c[0].get<float>(), c[1].get<float>(),
                                                      c[2].get<float>(), c[3].get<float>());
            }
            outData.visual.maxVisibleDepth = vis.value("maxVisibleDepth", 10.0f);
            outData.visual.fresnelPower = vis.value("fresnelPower", 5.0f);
        }

        // Physics
        if (j.contains("physics"))
        {
            const auto& phys = j["physics"];
            outData.physics.density = phys.value("density", 1000.0f);
            outData.physics.drag = phys.value("drag", 0.5f);
            outData.physics.buoyancyStrength = phys.value("buoyancyStrength", 2.0f);
        }

        // Ocean FFT
        if (j.contains("oceanFFT"))
        {
            const auto& fft = j["oceanFFT"];
            outData.fftConfig.resolution = fft.value("resolution", 256u);
            outData.fftConfig.patchSize = fft.value("patchSize", 100.0f);
            outData.fftConfig.windSpeed = fft.value("windSpeed", 8.0f);
            outData.fftConfig.windDirection = fft.value("windDirection", 45.0f);
            outData.fftConfig.amplitude = fft.value("amplitude", 0.00003f);
            outData.fftConfig.choppiness = fft.value("choppiness", 1.2f);
            outData.fftConfig.foamThreshold = fft.value("foamThreshold", -0.1f);
            outData.fftConfig.displacementScale = fft.value("displacementScale", 4.0f);
            outData.fftConfig.enabled = true;
        }

        vfLogInfo("OceanSerializer: Loaded ocean from {}", path);
        return true;
    }
}
