#include "ToonProfileAsset.hpp"
#include "../print/Log.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

namespace material
{
    using json = nlohmann::json;
    namespace fs = std::filesystem;

    namespace
    {
        json vec3ToJson(const glm::vec3& v) { return json::array({v.x, v.y, v.z}); }

        glm::vec3 jsonToVec3(const json& j, const glm::vec3& fallback)
        {
            if (j.is_array() && j.size() >= 3 &&
                j[0].is_number() && j[1].is_number() && j[2].is_number())
            {
                return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
            }
            return fallback;
        }
    }

    bool ToonProfileAsset::save(std::string_view path, const ToonProfile& profile)
    {
        json j;
        j["version"] = FORMAT_VERSION;

        j["shadeColor"] = vec3ToJson(profile.shadeColor);
        j["midColor"] = vec3ToJson(profile.midColor);
        j["shadowThreshold"] = profile.shadowThreshold;
        j["midThreshold"] = profile.midThreshold;
        j["bandSmoothness"] = profile.bandSmoothness;
        j["giScale"] = profile.giScale;

        j["specColor"] = vec3ToJson(profile.specColor);
        j["specThreshold"] = profile.specThreshold;
        j["specSmoothness"] = profile.specSmoothness;
        j["specIntensity"] = profile.specIntensity;
        j["specShininess"] = profile.specShininess;

        j["rimColor"] = vec3ToJson(profile.rimColor);
        j["rimPower"] = profile.rimPower;
        j["rimIntensity"] = profile.rimIntensity;

        try
        {
            fs::path filePath(path);
            if (filePath.has_parent_path())
                fs::create_directories(filePath.parent_path());

            std::ofstream file(filePath);
            if (!file.is_open())
            {
                vfLogError("Failed to create toon profile file: {}", path);
                return false;
            }
            file << j.dump(4);
            file.flush();
            if (!file.good())
            {
                vfLogError("Failed to write toon profile file: {}", path);
                return false;
            }
            vfLogInfo("Saved toon profile: {}", path);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to save toon profile {}: {}", path, e.what());
            return false;
        }
    }

    std::optional<ToonProfile> ToonProfileAsset::load(std::string_view path)
    {
        fs::path filePath(path);
        if (!fs::exists(filePath))
        {
            vfLogError("Toon profile file not found: {}", path);
            return std::nullopt;
        }

        json j;
        try
        {
            std::ifstream file(filePath);
            if (!file.is_open())
            {
                vfLogError("Failed to open toon profile file: {}", path);
                return std::nullopt;
            }
            file >> j;
        }
        catch (const json::exception& e)
        {
            vfLogError("Toon profile file '{}' contains invalid JSON: {}", path, e.what());
            return std::nullopt;
        }

        if (!j.is_object())
        {
            vfLogError("Toon profile file '{}' must contain a JSON object at root", path);
            return std::nullopt;
        }

        ToonProfile p; // defaults from the struct
        p.shadeColor = jsonToVec3(j.value("shadeColor", json()), p.shadeColor);
        p.midColor = jsonToVec3(j.value("midColor", json()), p.midColor);
        p.shadowThreshold = j.value("shadowThreshold", p.shadowThreshold);
        p.midThreshold = j.value("midThreshold", p.midThreshold);
        p.bandSmoothness = j.value("bandSmoothness", p.bandSmoothness);
        p.giScale = j.value("giScale", p.giScale);

        p.specColor = jsonToVec3(j.value("specColor", json()), p.specColor);
        p.specThreshold = j.value("specThreshold", p.specThreshold);
        p.specSmoothness = j.value("specSmoothness", p.specSmoothness);
        p.specIntensity = j.value("specIntensity", p.specIntensity);
        p.specShininess = j.value("specShininess", p.specShininess);

        p.rimColor = jsonToVec3(j.value("rimColor", json()), p.rimColor);
        p.rimPower = j.value("rimPower", p.rimPower);
        p.rimIntensity = j.value("rimIntensity", p.rimIntensity);

        return p;
    }
}
