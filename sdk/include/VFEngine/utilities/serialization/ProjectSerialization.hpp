#pragma once
#include "SerializationExport.hpp"
#include <string_view>
#include <optional>
#include <nlohmann/json.hpp>
#include "../config/ProjectConfig.hpp"

namespace serialization
{
    using json = nlohmann::json;

    class VF_SERIALIZATION_API ProjectSerialization
    {
    public:
        static std::optional<config::ProjectConfig> loadProject(std::string_view filename);
        static bool saveProject(const config::ProjectConfig& project, std::string_view filename);
        static bool validateProject(std::string_view filename);

    private:
        static bool validateStructure(const json& projectJson);
        static bool parseSchemaVersion(const std::string& versionStr, config::ProjectFileVersion& outVersion);
        static std::string getCurrentTimestamp();
        static std::string getEngineVersionString();
    };

}
