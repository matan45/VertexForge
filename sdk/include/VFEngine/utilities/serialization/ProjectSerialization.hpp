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

    // VK-1690: what stampEngineVersions did to a .vfproj.
    enum class ProjectStampStatus
    {
        Unchanged,  // both members already name this engine: nothing was written
        Stamped,    // engineVersion and assetFormatVersion now name this engine
        Failed      // the file is as it was; message says why
    };

    struct ProjectStamp
    {
        ProjectStampStatus status = ProjectStampStatus::Unchanged;
        std::string message;  // why it failed; empty otherwise; never the path
        // What the file records once the call succeeded (Unchanged or Stamped): this engine's values,
        // or a NEWER engine's that it kept (a stamp never lowers the record). Empty after Failed.
        std::string engineVersion;
        std::string assetFormatVersion;
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
        // VK-1690: records in a .vfproj that this engine opened it. Sets engineVersion to
        // Version::displayName and assetFormatVersion to AssetFormatVersion::toString(), each unless
        // the file already names a NEWER version there (never lowered: the launcher must keep older
        // engines away from content a newer one may have written), and keeps every other member, the
        // member order, the line endings and a UTF-8 BOM. A missing assetFormatVersion goes right
        // after engineVersion; a missing engineVersion goes at the end. Writes nothing when both
        // already hold what they should. Otherwise the file is replaced atomically through
        // the sibling <file>.stamp.tmp (up to 3 tries), which is never left behind. Never throws; the
        // caller reports the result (only resource::replaceFileAtomically logs its own errors).
        static ProjectStamp stampEngineVersions(std::string_view filename);

    private:
        // Empty when the structure is valid, otherwise why not.
        static std::string structureError(const json& projectJson);
        static bool parseSchemaVersion(const std::string& versionStr, config::ProjectFileVersion& outVersion);
        static std::string getCurrentTimestamp();
    };

}
