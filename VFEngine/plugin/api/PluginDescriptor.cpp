#include "PluginDescriptor.hpp"
#include <nlohmann/json.hpp>
#include <fstream>

namespace plugin
{
    std::optional<PluginDescriptor> PluginDescriptor::loadFromFile(const std::filesystem::path& path)
    {
        std::ifstream file(path);
        if (!file.is_open())
            return std::nullopt;

        nlohmann::json json;
        try
        {
            file >> json;
        }
        catch (const nlohmann::json::parse_error&)
        {
            return std::nullopt;
        }

        // Validate required fields
        if (!json.contains("name") || !json["name"].is_string() ||
            !json.contains("version") || !json["version"].is_string() ||
            !json.contains("apiVersion") || !json["apiVersion"].is_number_integer() ||
            !json.contains("library") || !json["library"].is_string())
        {
            return std::nullopt;
        }

        PluginDescriptor desc;
        desc.name = json["name"].get<std::string>();
        desc.version = json["version"].get<std::string>();
        desc.apiVersion = json["apiVersion"].get<uint32_t>();
        desc.library = json["library"].get<std::string>();
        desc.basePath = path.parent_path();
        desc.descriptorPath = path;

        // Optional fields
        if (json.contains("author") && json["author"].is_string())
            desc.author = json["author"].get<std::string>();
        if (json.contains("description") && json["description"].is_string())
            desc.description = json["description"].get<std::string>();
        if (json.contains("loadOrder") && json["loadOrder"].is_number_integer())
            desc.loadOrder = json["loadOrder"].get<int>();
        if (json.contains("enabled") && json["enabled"].is_boolean())
            desc.enabled = json["enabled"].get<bool>();

        if (json.contains("capabilities") && json["capabilities"].is_array())
        {
            for (const auto& cap : json["capabilities"])
            {
                if (cap.is_string())
                    desc.capabilities.push_back(cap.get<std::string>());
            }
        }

        if (json.contains("dependencies") && json["dependencies"].is_array())
        {
            for (const auto& dep : json["dependencies"])
            {
                if (dep.is_string())
                    desc.dependencies.push_back(dep.get<std::string>());
            }
        }

        // Parse version string "major.minor.patch"
        {
            auto vStr = desc.version;
            size_t pos = 0;
            try {
                desc.versionMajor = static_cast<uint32_t>(std::stoul(vStr, &pos));
                if (pos < vStr.size() && vStr[pos] == '.') {
                    vStr = vStr.substr(pos + 1);
                    desc.versionMinor = static_cast<uint32_t>(std::stoul(vStr, &pos));
                    if (pos < vStr.size() && vStr[pos] == '.') {
                        vStr = vStr.substr(pos + 1);
                        desc.versionPatch = static_cast<uint32_t>(std::stoul(vStr));
                    }
                }
            } catch (...) {
                desc.versionMajor = 0;
                desc.versionMinor = 0;
                desc.versionPatch = 0;
            }
        }

        return desc;
    }
}
