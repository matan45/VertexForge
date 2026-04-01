#include "SceneSerialization.hpp"
#include "JsonConverters.hpp"
#include "../components/Components.hpp"
#include "../weather/WeatherTypes.hpp"
#include <algorithm>
#include <cmath>

namespace serialization
{
    namespace
    {
        json serializeAtmosphere(const render::atmosphere::AtmosphereSettings& s)
        {
            return {
                {"enabled", s.enabled},
                {"planetRadius", s.planetRadius},
                {"atmosphereRadius", s.atmosphereRadius},
                {"rayleighScattering", {s.rayleighScattering.x, s.rayleighScattering.y, s.rayleighScattering.z}},
                {"rayleighDensityExpScale", s.rayleighDensityExpScale},
                {"mieScattering", s.mieScattering},
                {"mieAbsorption", s.mieAbsorption},
                {"mieAnisotropy", s.mieAnisotropy},
                {"mieDensityExpScale", s.mieDensityExpScale},
                {"ozoneAbsorption", {s.ozoneAbsorption.x, s.ozoneAbsorption.y, s.ozoneAbsorption.z}},
                {"ozoneCenterAlt", s.ozoneCenterAlt},
                {"ozoneWidth", s.ozoneWidth},
                {"sunIrradiance", {s.sunIrradiance.x, s.sunIrradiance.y, s.sunIrradiance.z}},
                {"sunAngularRadius", s.sunAngularRadius},
                {"sunAzimuth", s.sunAzimuth},
                {"sunElevation", s.sunElevation},
                {"groundAlbedo", {s.groundAlbedo.x, s.groundAlbedo.y, s.groundAlbedo.z}},
                {"aerialMaxDist", s.aerialMaxDist},
                {"aerialIntensity", s.aerialIntensity},
                // Moon
                {"moonAzimuth", s.moonAzimuth},
                {"moonElevation", s.moonElevation},
                {"moonAngularRadius", s.moonAngularRadius},
                {"moonBrightness", s.moonBrightness},
                // Stars
                {"starDensity", s.starDensity},
                {"starBrightness", s.starBrightness},
                {"starTwinkleSpeed", s.starTwinkleSpeed},
                {"nightSkyBrightness", s.nightSkyBrightness},
                // Day-Night Cycle
                {"dayNightEnabled", s.dayNightEnabled},
                {"timeOfDay", s.timeOfDay},
                {"cycleSpeed", s.cycleSpeed},
                {"moonPhaseOffset", s.moonPhaseOffset}
            };
        }

        void deserializeAtmosphere(const json& j, render::atmosphere::AtmosphereSettings& s)
        {
            if (!j.contains("atmosphere") || !j["atmosphere"].is_object())
                return;
            const auto& a = j["atmosphere"];

            if (a.contains("enabled") && a["enabled"].is_boolean())
                s.enabled = a["enabled"].get<bool>();
            if (a.contains("planetRadius") && a["planetRadius"].is_number())
                s.planetRadius = a["planetRadius"].get<float>();
            if (a.contains("atmosphereRadius") && a["atmosphereRadius"].is_number())
                s.atmosphereRadius = a["atmosphereRadius"].get<float>();
            if (a.contains("rayleighScattering") && a["rayleighScattering"].is_array() && a["rayleighScattering"].size() == 3)
            {
                s.rayleighScattering.x = a["rayleighScattering"][0].get<float>();
                s.rayleighScattering.y = a["rayleighScattering"][1].get<float>();
                s.rayleighScattering.z = a["rayleighScattering"][2].get<float>();
            }
            if (a.contains("rayleighDensityExpScale") && a["rayleighDensityExpScale"].is_number())
            {
                float v = a["rayleighDensityExpScale"].get<float>();
                s.rayleighDensityExpScale = (v == 0.0f) ? -1.0f / 8000.0f : v; // guard against zero from corrupted files
            }
            if (a.contains("mieScattering") && a["mieScattering"].is_number())
                s.mieScattering = a["mieScattering"].get<float>();
            if (a.contains("mieAbsorption") && a["mieAbsorption"].is_number())
                s.mieAbsorption = a["mieAbsorption"].get<float>();
            if (a.contains("mieAnisotropy") && a["mieAnisotropy"].is_number())
                s.mieAnisotropy = std::clamp(a["mieAnisotropy"].get<float>(), -1.0f, 1.0f);
            if (a.contains("mieDensityExpScale") && a["mieDensityExpScale"].is_number())
            {
                float v = a["mieDensityExpScale"].get<float>();
                s.mieDensityExpScale = (v == 0.0f) ? -1.0f / 1200.0f : v;
            }
            if (a.contains("ozoneAbsorption") && a["ozoneAbsorption"].is_array() && a["ozoneAbsorption"].size() == 3)
            {
                s.ozoneAbsorption.x = a["ozoneAbsorption"][0].get<float>();
                s.ozoneAbsorption.y = a["ozoneAbsorption"][1].get<float>();
                s.ozoneAbsorption.z = a["ozoneAbsorption"][2].get<float>();
            }
            if (a.contains("ozoneCenterAlt") && a["ozoneCenterAlt"].is_number())
                s.ozoneCenterAlt = a["ozoneCenterAlt"].get<float>();
            if (a.contains("ozoneWidth") && a["ozoneWidth"].is_number())
                s.ozoneWidth = a["ozoneWidth"].get<float>();
            if (a.contains("sunIrradiance") && a["sunIrradiance"].is_array() && a["sunIrradiance"].size() == 3)
            {
                s.sunIrradiance.x = a["sunIrradiance"][0].get<float>();
                s.sunIrradiance.y = a["sunIrradiance"][1].get<float>();
                s.sunIrradiance.z = a["sunIrradiance"][2].get<float>();
            }
            if (a.contains("sunAngularRadius") && a["sunAngularRadius"].is_number())
                s.sunAngularRadius = a["sunAngularRadius"].get<float>();
            if (a.contains("sunAzimuth") && a["sunAzimuth"].is_number())
                s.sunAzimuth = a["sunAzimuth"].get<float>();
            if (a.contains("sunElevation") && a["sunElevation"].is_number())
                s.sunElevation = std::clamp(a["sunElevation"].get<float>(), -90.0f, 90.0f);
            if (a.contains("groundAlbedo") && a["groundAlbedo"].is_array() && a["groundAlbedo"].size() == 3)
            {
                s.groundAlbedo.x = std::clamp(a["groundAlbedo"][0].get<float>(), 0.0f, 1.0f);
                s.groundAlbedo.y = std::clamp(a["groundAlbedo"][1].get<float>(), 0.0f, 1.0f);
                s.groundAlbedo.z = std::clamp(a["groundAlbedo"][2].get<float>(), 0.0f, 1.0f);
            }
            if (a.contains("aerialMaxDist") && a["aerialMaxDist"].is_number())
                s.aerialMaxDist = std::max(a["aerialMaxDist"].get<float>(), 1000.0f);
            if (a.contains("aerialIntensity") && a["aerialIntensity"].is_number())
                s.aerialIntensity = std::clamp(a["aerialIntensity"].get<float>(), 0.0f, 5.0f);

            // Moon
            if (a.contains("moonAzimuth") && a["moonAzimuth"].is_number())
                s.moonAzimuth = std::fmod(a["moonAzimuth"].get<float>(), 360.0f);
            if (a.contains("moonElevation") && a["moonElevation"].is_number())
                s.moonElevation = std::clamp(a["moonElevation"].get<float>(), -90.0f, 90.0f);
            if (a.contains("moonAngularRadius") && a["moonAngularRadius"].is_number())
                s.moonAngularRadius = std::clamp(a["moonAngularRadius"].get<float>(), 0.001f, 0.1f);
            if (a.contains("moonBrightness") && a["moonBrightness"].is_number())
                s.moonBrightness = std::clamp(a["moonBrightness"].get<float>(), 0.0f, 1.0f);

            // Stars
            if (a.contains("starDensity") && a["starDensity"].is_number())
                s.starDensity = std::clamp(a["starDensity"].get<float>(), 0.0f, 0.05f);
            if (a.contains("starBrightness") && a["starBrightness"].is_number())
                s.starBrightness = std::clamp(a["starBrightness"].get<float>(), 0.0f, 10.0f);
            if (a.contains("starTwinkleSpeed") && a["starTwinkleSpeed"].is_number())
                s.starTwinkleSpeed = std::clamp(a["starTwinkleSpeed"].get<float>(), 0.0f, 5.0f);
            if (a.contains("nightSkyBrightness") && a["nightSkyBrightness"].is_number())
                s.nightSkyBrightness = std::clamp(a["nightSkyBrightness"].get<float>(), 0.0f, 0.05f);

            // Day-Night Cycle
            if (a.contains("dayNightEnabled") && a["dayNightEnabled"].is_boolean())
                s.dayNightEnabled = a["dayNightEnabled"].get<bool>();
            if (a.contains("timeOfDay") && a["timeOfDay"].is_number())
                s.timeOfDay = std::clamp(a["timeOfDay"].get<float>(), 0.0f, 24.0f);
            if (a.contains("cycleSpeed") && a["cycleSpeed"].is_number())
                s.cycleSpeed = std::clamp(a["cycleSpeed"].get<float>(), 0.0f, 100.0f);
            if (a.contains("moonPhaseOffset") && a["moonPhaseOffset"].is_number())
                s.moonPhaseOffset = std::clamp(a["moonPhaseOffset"].get<float>(), 0.0f, 1.0f);
        }

        json serializeCloud(const render::cloud::CloudSettings& s)
        {
            return {
                {"enabled", s.enabled},
                {"cloudMinAltitude", s.cloudMinAltitude},
                {"cloudMaxAltitude", s.cloudMaxAltitude},
                {"globalDensity", s.globalDensity},
                {"globalCoverage", s.globalCoverage},
                {"cloudType", s.cloudType},
                {"shapeScale", s.shapeScale},
                {"detailScale", s.detailScale},
                {"erosionStrength", s.erosionStrength},
                {"curlStrength", s.curlStrength},
                {"windSpeed", s.windSpeed},
                {"windDirectionDeg", s.windDirectionDeg},
                {"lightAbsorption", s.lightAbsorption},
                {"phaseForward", s.phaseForward},
                {"phaseBackward", s.phaseBackward},
                {"phaseBlend", s.phaseBlend},
                {"ambientIntensity", s.ambientIntensity},
                {"silverLiningIntensity", s.silverLiningIntensity},
                {"silverLiningSpread", s.silverLiningSpread},
                {"multiScatterBoost", s.multiScatterBoost},
                {"temporalBlendFactor", s.temporalBlendFactor},
                {"maxMarchSteps", s.maxMarchSteps},
                {"lightMarchSteps", s.lightMarchSteps},
                {"cloudColorTint", {s.cloudColorTint.x, s.cloudColorTint.y, s.cloudColorTint.z}}
            };
        }

        void deserializeCloud(const json& j, render::cloud::CloudSettings& s)
        {
            if (!j.contains("cloud") || !j["cloud"].is_object())
                return;
            const auto& c = j["cloud"];

            if (c.contains("enabled") && c["enabled"].is_boolean())
                s.enabled = c["enabled"].get<bool>();
            if (c.contains("cloudMinAltitude") && c["cloudMinAltitude"].is_number())
                s.cloudMinAltitude = c["cloudMinAltitude"].get<float>();
            if (c.contains("cloudMaxAltitude") && c["cloudMaxAltitude"].is_number())
                s.cloudMaxAltitude = c["cloudMaxAltitude"].get<float>();
            if (c.contains("globalDensity") && c["globalDensity"].is_number())
                s.globalDensity = std::clamp(c["globalDensity"].get<float>(), 0.0f, 1.0f);
            if (c.contains("globalCoverage") && c["globalCoverage"].is_number())
                s.globalCoverage = std::clamp(c["globalCoverage"].get<float>(), 0.0f, 1.0f);
            if (c.contains("cloudType") && c["cloudType"].is_number())
                s.cloudType = std::clamp(c["cloudType"].get<float>(), 0.0f, 1.0f);
            if (c.contains("shapeScale") && c["shapeScale"].is_number())
                s.shapeScale = c["shapeScale"].get<float>();
            if (c.contains("detailScale") && c["detailScale"].is_number())
                s.detailScale = c["detailScale"].get<float>();
            if (c.contains("erosionStrength") && c["erosionStrength"].is_number())
                s.erosionStrength = std::clamp(c["erosionStrength"].get<float>(), 0.0f, 1.0f);
            if (c.contains("curlStrength") && c["curlStrength"].is_number())
                s.curlStrength = std::clamp(c["curlStrength"].get<float>(), 0.0f, 1.0f);
            if (c.contains("windSpeed") && c["windSpeed"].is_number())
                s.windSpeed = c["windSpeed"].get<float>();
            if (c.contains("windDirectionDeg") && c["windDirectionDeg"].is_number())
                s.windDirectionDeg = c["windDirectionDeg"].get<float>();
            if (c.contains("lightAbsorption") && c["lightAbsorption"].is_number())
                s.lightAbsorption = std::clamp(c["lightAbsorption"].get<float>(), 0.0f, 2.0f);
            if (c.contains("phaseForward") && c["phaseForward"].is_number())
                s.phaseForward = std::clamp(c["phaseForward"].get<float>(), 0.0f, 1.0f);
            if (c.contains("phaseBackward") && c["phaseBackward"].is_number())
                s.phaseBackward = std::clamp(c["phaseBackward"].get<float>(), -1.0f, 0.0f);
            if (c.contains("phaseBlend") && c["phaseBlend"].is_number())
                s.phaseBlend = std::clamp(c["phaseBlend"].get<float>(), 0.0f, 1.0f);
            if (c.contains("ambientIntensity") && c["ambientIntensity"].is_number())
                s.ambientIntensity = std::clamp(c["ambientIntensity"].get<float>(), 0.0f, 2.0f);
            if (c.contains("silverLiningIntensity") && c["silverLiningIntensity"].is_number())
                s.silverLiningIntensity = std::clamp(c["silverLiningIntensity"].get<float>(), 0.0f, 2.0f);
            if (c.contains("silverLiningSpread") && c["silverLiningSpread"].is_number())
                s.silverLiningSpread = std::clamp(c["silverLiningSpread"].get<float>(), 1.0f, 20.0f);
            if (c.contains("multiScatterBoost") && c["multiScatterBoost"].is_number())
                s.multiScatterBoost = std::clamp(c["multiScatterBoost"].get<float>(), 0.0f, 2.0f);
            if (c.contains("temporalBlendFactor") && c["temporalBlendFactor"].is_number())
                s.temporalBlendFactor = std::clamp(c["temporalBlendFactor"].get<float>(), 0.0f, 1.0f);
            if (c.contains("maxMarchSteps") && c["maxMarchSteps"].is_number_unsigned())
                s.maxMarchSteps = std::clamp(c["maxMarchSteps"].get<uint32_t>(), 16u, 256u);
            if (c.contains("lightMarchSteps") && c["lightMarchSteps"].is_number_unsigned())
                s.lightMarchSteps = std::clamp(c["lightMarchSteps"].get<uint32_t>(), 2u, 16u);
            if (c.contains("cloudColorTint") && c["cloudColorTint"].is_array() && c["cloudColorTint"].size() == 3)
            {
                s.cloudColorTint.x = std::clamp(c["cloudColorTint"][0].get<float>(), 0.0f, 2.0f);
                s.cloudColorTint.y = std::clamp(c["cloudColorTint"][1].get<float>(), 0.0f, 2.0f);
                s.cloudColorTint.z = std::clamp(c["cloudColorTint"][2].get<float>(), 0.0f, 2.0f);
            }
        }

    } // anonymous namespace

    json SceneSerialization::serializeAtmosphereSettings(const render::atmosphere::AtmosphereSettings& settings)
    {
        return serializeAtmosphere(settings);
    }

    void SceneSerialization::deserializeAtmosphereSettings(const json& j, render::atmosphere::AtmosphereSettings& settings)
    {
        deserializeAtmosphere(j, settings);
    }

    json SceneSerialization::serializeCloudSettings(const render::cloud::CloudSettings& settings)
    {
        return serializeCloud(settings);
    }

    void SceneSerialization::deserializeCloudSettings(const json& j, render::cloud::CloudSettings& settings)
    {
        deserializeCloud(j, settings);
    }

    json SceneSerialization::serializeWeatherState(const weather::WeatherState& state)
    {
        json j;
        nlohmann::json weatherJson;
        weather::to_json(weatherJson, state);
        j["weather"] = weatherJson;
        return j;
    }

    void SceneSerialization::deserializeWeatherState(const json& j, weather::WeatherState& state)
    {
        if (!j.contains("weather") || !j["weather"].is_object())
            return;
        weather::from_json(j["weather"], state);
    }

    json SceneSerialization::serializeWeatherAudioConfig(const weather::WeatherAudioConfig& config)
    {
        json j;
        j["windLoopPath"] = config.windLoopPath;
        j["rainLoopPath"] = config.rainLoopPath;
        j["snowLoopPath"] = config.snowLoopPath;
        j["thunderPaths"] = json::array({
            config.thunderPaths[0],
            config.thunderPaths[1],
            config.thunderPaths[2]
        });
        return j;
    }

    void SceneSerialization::deserializeWeatherAudioConfig(const json& j, weather::WeatherAudioConfig& config)
    {
        if (j.contains("windLoopPath") && j["windLoopPath"].is_string())
            config.windLoopPath = j["windLoopPath"].get<std::string>();
        if (j.contains("rainLoopPath") && j["rainLoopPath"].is_string())
            config.rainLoopPath = j["rainLoopPath"].get<std::string>();
        if (j.contains("snowLoopPath") && j["snowLoopPath"].is_string())
            config.snowLoopPath = j["snowLoopPath"].get<std::string>();
        if (j.contains("thunderPaths") && j["thunderPaths"].is_array() && j["thunderPaths"].size() == 3)
        {
            for (int i = 0; i < 3; i++)
            {
                if (j["thunderPaths"][i].is_string())
                    config.thunderPaths[i] = j["thunderPaths"][i].get<std::string>();
            }
        }
    }
}
