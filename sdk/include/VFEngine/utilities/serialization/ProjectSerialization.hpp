#pragma once
#include "SerializationExport.hpp"
#include <string>
#include <string_view>
#include <optional>
#include <vector>
#include <nlohmann/json.hpp>
#include "../config/ProjectConfig.hpp"

namespace serialization
{
    using json = nlohmann::json;

    // VK-1682: why a .vfproj can or cannot be opened. SchemaTooNew is its own status because the
    // editor reports it with its own exit code: the project needs a newer engine, it is not broken.
    enum class ProjectInspectionStatus
    {
        Ok,
        FileUnreadable,        // missing, empty or unreadable
        ParseError,            // not valid JSON
        InvalidStructure,      // a required field is missing, has the wrong type or is empty
        InvalidSchemaVersion,  // schemaVersion is not "MAJOR.MINOR" with decimal digits
        SchemaTooOld,          // schema major older than config::ProjectSchemaVersion::major
        SchemaTooNew           // schema major newer than config::ProjectSchemaVersion::major
    };

    struct ProjectInspection
    {
        ProjectInspectionStatus status = ProjectInspectionStatus::FileUnreadable;
        std::optional<config::ProjectConfig> config;  // set exactly when status == Ok
        std::string message;                          // why it failed; empty when Ok; never the path
        std::vector<std::string> warnings;            // ignored values; the project still opens

        bool ok() const noexcept { return status == ProjectInspectionStatus::Ok; }
    };

    class VF_SERIALIZATION_API ProjectSerialization
    {
    public:
        // Reads and validates a .vfproj without logging. A newer schema MINOR is accepted.
        static ProjectInspection inspectProject(std::string_view filename);
        // inspectProject plus logging: errors, warnings and the success line.
        static std::optional<config::ProjectConfig> loadProject(std::string_view filename);
        static bool saveProject(const config::ProjectConfig& project, std::string_view filename);
        static bool validateProject(std::string_view filename);

    private:
        // Empty when the structure is valid, otherwise why not.
        static std::string structureError(const json& projectJson);
        static bool parseSchemaVersion(const std::string& versionStr, config::ProjectFileVersion& outVersion);
        static std::string getCurrentTimestamp();
    };

}
