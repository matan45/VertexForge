#include "ProjectSerialization.hpp"
#include "../print/Log.hpp"
#include "../config/Config.hpp"
#include "SerializationFileAccess.hpp"
#include <fstream>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <system_error>

namespace serialization
{
    ProjectInspection ProjectSerialization::inspectProject(std::string_view filename)
    {
        ProjectInspection inspection;
        auto fail = [&inspection](ProjectInspectionStatus status, std::string message)
        {
            inspection.status = status;
            inspection.message = std::move(message);
            inspection.config.reset();
            return inspection;
        };

        try
        {
            std::string filePath{filename};
            json projectJson;
            try
            {
                projectJson = readSerializationJsonFile(filePath);
            }
            catch (const json::parse_error& e)
            {
                return fail(ProjectInspectionStatus::ParseError, std::string("JSON parse error: ") + e.what());
            }
            if (projectJson.is_null())
            {
                return fail(ProjectInspectionStatus::FileUnreadable, "the file is missing, empty or unreadable");
            }

            if (std::string error = structureError(projectJson); !error.empty())
            {
                return fail(ProjectInspectionStatus::InvalidStructure, std::move(error));
            }

            config::ProjectFileVersion fileVersion;
            std::string versionStr = projectJson["schemaVersion"].get<std::string>();
            if (!parseSchemaVersion(versionStr, fileVersion))
            {
                return fail(ProjectInspectionStatus::InvalidSchemaVersion,
                            "Invalid schema version format: '" + versionStr + "' (expected MAJOR.MINOR)");
            }

            if (fileVersion.major > config::ProjectSchemaVersion::major)
            {
                return fail(ProjectInspectionStatus::SchemaTooNew,
                            "Project schema version " + fileVersion.toString() +
                                " is newer than this engine supports (" + config::ProjectSchemaVersion::toString() +
                                "); open it with a newer VertexForge");
            }
            if (!fileVersion.isCompatible())
            {
                return fail(ProjectInspectionStatus::SchemaTooOld,
                            "Project schema version " + fileVersion.toString() +
                                " is older than this engine supports (" + config::ProjectSchemaVersion::toString() + ")");
            }

            config::ProjectConfig project;
            project.schemaVersion = fileVersion;
            project.projectName = projectJson["projectName"].get<std::string>();
            project.version = projectJson["version"].get<std::string>();
            project.workingDirectory = projectJson["workingDirectory"].get<std::string>();
            project.startupScene = projectJson["startupScene"].get<std::string>();

            if (projectJson.contains("exeIconPath") && projectJson["exeIconPath"].is_string())
            {
                project.exeIconPath = projectJson["exeIconPath"].get<std::string>();
            }

            if (projectJson.contains("inputMapping") && projectJson["inputMapping"].is_string())
            {
                project.inputMapping = projectJson["inputMapping"].get<std::string>();
            }

            if (projectJson.contains("engineVersion") && projectJson["engineVersion"].is_string())
            {
                project.engineVersion = projectJson["engineVersion"].get<std::string>();
            }

            if (projectJson.contains("lastModified") && projectJson["lastModified"].is_string())
            {
                project.lastModified = projectJson["lastModified"].get<std::string>();
            }

            if (projectJson.contains("pluginApiVersion") && projectJson["pluginApiVersion"].is_number_unsigned())
            {
                project.pluginApiVersion = projectJson["pluginApiVersion"].get<uint32_t>();
            }

            if (projectJson.contains("fontFallbackChain"))
            {
                const auto& fallbackJson = projectJson["fontFallbackChain"];
                if (!fallbackJson.is_array())
                {
                    inspection.warnings.push_back(
                        "Ignoring invalid 'fontFallbackChain' in project file: expected an array");
                }
                else
                {
                    for (const auto& entry : fallbackJson)
                    {
                        if (!entry.is_string())
                        {
                            inspection.warnings.push_back("Ignoring non-string entry in project 'fontFallbackChain'");
                            continue;
                        }
                        if (project.fontFallbackChain.size() >= config::ProjectConfig::maxFontFallbacks)
                        {
                            inspection.warnings.push_back("Ignoring project font fallback beyond the maximum of " +
                                                          std::to_string(config::ProjectConfig::maxFontFallbacks));
                            break;
                        }
                        project.fontFallbackChain.push_back(entry.get<std::string>());
                    }
                }
            }

            inspection.status = ProjectInspectionStatus::Ok;
            inspection.message.clear();
            inspection.config = std::move(project);
            return inspection;
        }
        catch (const std::exception& e)
        {
            return fail(ProjectInspectionStatus::FileUnreadable, std::string("the file could not be read: ") + e.what());
        }
    }

    std::optional<config::ProjectConfig> ProjectSerialization::loadProject(std::string_view filename)
    {
        ProjectInspection inspection = inspectProject(filename);
        for (const std::string& warning : inspection.warnings)
        {
            vfLogWarning("{}", warning);
        }

        if (!inspection.ok())
        {
            vfLogError("Failed to load project file '{}': {}", filename, inspection.message);
            return std::nullopt;
        }

        vfLogInfo("Project loaded successfully: {}", inspection.config->projectName);
        return std::move(inspection.config);
    }

    bool ProjectSerialization::saveProject(const config::ProjectConfig& project, std::string_view filename)
    {
        try
        {
            if (!project.isValid())
            {
                vfLogError("Cannot save project: required fields are missing");
                return false;
            }

            json projectJson;
            projectJson["schemaVersion"] = config::ProjectSchemaVersion::toString();
            projectJson["projectName"] = project.projectName;
            projectJson["version"] = project.version;
            projectJson["workingDirectory"] = project.workingDirectory;
            projectJson["startupScene"] = project.startupScene;

            if (!project.exeIconPath.empty())
            {
                projectJson["exeIconPath"] = project.exeIconPath;
            }

            if (project.inputMapping.has_value() && !project.inputMapping->empty())
            {
                projectJson["inputMapping"] = *project.inputMapping;
            }

            projectJson["engineVersion"] = Version::displayName;
            projectJson["lastModified"] = getCurrentTimestamp();

            if (project.pluginApiVersion.has_value())
            {
                projectJson["pluginApiVersion"] = *project.pluginApiVersion;
            }

            if (!project.fontFallbackChain.empty())
            {
                projectJson["fontFallbackChain"] = json::array();
                const size_t fallbackCount =
                    std::min(project.fontFallbackChain.size(), config::ProjectConfig::maxFontFallbacks);
                for (size_t i = 0; i < fallbackCount; ++i)
                {
                    projectJson["fontFallbackChain"].push_back(project.fontFallbackChain[i]);
                }
                if (project.fontFallbackChain.size() > fallbackCount)
                {
                    vfLogWarning("Saving only the first {} project font fallbacks",
                                 config::ProjectConfig::maxFontFallbacks);
                }
            }

            std::string filePath{filename};
            std::ofstream file{filePath};
            if (!file.is_open())
            {
                vfLogError("Failed to open project file for writing: {}", filename);
                return false;
            }

            file << projectJson.dump(2);
            file.close();

            vfLogInfo("Project saved successfully: {}", filename);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to save project file '{}': {}", filename, e.what());
            return false;
        }
    }

    bool ProjectSerialization::validateProject(std::string_view filename)
    {
        try
        {
            std::string filePath{filename};
            json projectJson = readSerializationJsonFile(filePath);
            if (projectJson.is_null())
            {
                return false;
            }

            return structureError(projectJson).empty();
        }
        catch (...)
        {
            return false;
        }
    }

    std::string ProjectSerialization::structureError(const json& projectJson)
    {
        if (!projectJson.is_object())
        {
            return "Invalid project file: root is not a JSON object";
        }

        for (const char* field : {"schemaVersion", "projectName", "version", "workingDirectory", "startupScene"})
        {
            if (!projectJson.contains(field) || !projectJson[field].is_string())
            {
                return std::string("Invalid project file: missing or invalid '") + field + "' field";
            }
        }

        for (const char* field : {"projectName", "version", "workingDirectory", "startupScene"})
        {
            if (projectJson[field].get<std::string>().empty())
            {
                return std::string("Invalid project file: '") + field + "' cannot be empty";
            }
        }

        return {};
    }

    bool ProjectSerialization::parseSchemaVersion(const std::string& versionStr, config::ProjectFileVersion& outVersion)
    {
        // Decimal digits on both sides of exactly one dot. std::stoul used to accept "-1" (wrapping
        // to 0xFFFFFFFF, i.e. "newer than any engine"), leading blanks and trailing junk. A number
        // too large for 32 bits is still a (very) new version, so it saturates.
        const auto parsePart = [](std::string_view part, uint32_t& out)
        {
            if (part.empty())
            {
                return false;
            }
            for (char c : part)
            {
                if (c < '0' || c > '9')
                {
                    return false;
                }
            }
            const char* last = part.data() + part.size();
            auto [ptr, ec] = std::from_chars(part.data(), last, out);
            if (ec == std::errc::result_out_of_range)
            {
                out = UINT32_MAX;
                return true;
            }
            return ec == std::errc{} && ptr == last;
        };

        const size_t dotPos = versionStr.find('.');
        if (dotPos == std::string::npos)
        {
            return false;
        }

        const std::string_view text(versionStr);
        config::ProjectFileVersion parsed;
        if (!parsePart(text.substr(0, dotPos), parsed.major) || !parsePart(text.substr(dotPos + 1), parsed.minor))
        {
            return false;
        }
        outVersion = parsed;
        return true;
    }

    std::string ProjectSerialization::getCurrentTimestamp()
    {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_s(&tm, &time);
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
        return oss.str();
    }
}
