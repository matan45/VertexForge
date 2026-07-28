#include "ProjectSerialization.hpp"
#include "../print/Log.hpp"
#include "../config/Config.hpp"
#include "../resource/VFSHelpers.hpp"
#include <fstream>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>

namespace serialization
{
    std::optional<config::ProjectConfig> ProjectSerialization::loadProject(std::string_view filename)
    {
        try
        {
            std::string filePath{filename};
            json projectJson;
            try
            {
                projectJson = resource::readJsonFile(filePath);
            }
            catch (const json::parse_error& e)
            {
                vfLogError("JSON parse error in project file {}: {}", filename, e.what());
                return std::nullopt;
            }
            if (projectJson.is_null())
            {
                vfLogError("Failed to open project file: {}", filename);
                return std::nullopt;
            }

            if (!validateStructure(projectJson))
            {
                return std::nullopt;
            }

            config::ProjectFileVersion fileVersion;
            std::string versionStr = projectJson["schemaVersion"].get<std::string>();
            if (!parseSchemaVersion(versionStr, fileVersion))
            {
                vfLogError("Invalid schema version format in project file: {}", versionStr);
                return std::nullopt;
            }

            if (!fileVersion.isCompatible())
            {
                vfLogError("Project schema version {} is not compatible with engine version {}",
                           fileVersion.toString(), config::ProjectSchemaVersion::toString());
                return std::nullopt;
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
                    vfLogWarning("Ignoring invalid 'fontFallbackChain' in project file: expected an array");
                }
                else
                {
                    for (const auto& entry : fallbackJson)
                    {
                        if (!entry.is_string())
                        {
                            vfLogWarning("Ignoring non-string entry in project 'fontFallbackChain'");
                            continue;
                        }
                        if (project.fontFallbackChain.size() >= config::ProjectConfig::maxFontFallbacks)
                        {
                            vfLogWarning("Ignoring project font fallback beyond the maximum of {}",
                                         config::ProjectConfig::maxFontFallbacks);
                            break;
                        }
                        project.fontFallbackChain.push_back(entry.get<std::string>());
                    }
                }
            }

            vfLogInfo("Project loaded successfully: {}", project.projectName);
            return project;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to load project file '{}': {}", filename, e.what());
            return std::nullopt;
        }
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

            projectJson["engineVersion"] = getEngineVersionString();
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
            json projectJson = resource::readJsonFile(filePath);
            if (projectJson.is_null())
            {
                return false;
            }

            return validateStructure(projectJson);
        }
        catch (...)
        {
            return false;
        }
    }

    bool ProjectSerialization::validateStructure(const json& projectJson)
    {
        if (!projectJson.is_object())
        {
            vfLogError("Invalid project file: root is not a JSON object");
            return false;
        }

        if (!projectJson.contains("schemaVersion") || !projectJson["schemaVersion"].is_string())
        {
            vfLogError("Invalid project file: missing or invalid 'schemaVersion' field");
            return false;
        }

        if (!projectJson.contains("projectName") || !projectJson["projectName"].is_string())
        {
            vfLogError("Invalid project file: missing or invalid 'projectName' field");
            return false;
        }

        if (!projectJson.contains("version") || !projectJson["version"].is_string())
        {
            vfLogError("Invalid project file: missing or invalid 'version' field");
            return false;
        }

        if (!projectJson.contains("workingDirectory") || !projectJson["workingDirectory"].is_string())
        {
            vfLogError("Invalid project file: missing or invalid 'workingDirectory' field");
            return false;
        }

        if (!projectJson.contains("startupScene") || !projectJson["startupScene"].is_string())
        {
            vfLogError("Invalid project file: missing or invalid 'startupScene' field");
            return false;
        }

        if (projectJson["projectName"].get<std::string>().empty())
        {
            vfLogError("Invalid project file: 'projectName' cannot be empty");
            return false;
        }

        if (projectJson["version"].get<std::string>().empty())
        {
            vfLogError("Invalid project file: 'version' cannot be empty");
            return false;
        }

        if (projectJson["workingDirectory"].get<std::string>().empty())
        {
            vfLogError("Invalid project file: 'workingDirectory' cannot be empty");
            return false;
        }

        if (projectJson["startupScene"].get<std::string>().empty())
        {
            vfLogError("Invalid project file: 'startupScene' cannot be empty");
            return false;
        }

        return true;
    }

    bool ProjectSerialization::parseSchemaVersion(const std::string& versionStr, config::ProjectFileVersion& outVersion)
    {
        size_t dotPos = versionStr.find('.');
        if (dotPos == std::string::npos)
        {
            return false;
        }

        try
        {
            outVersion.major = static_cast<uint32_t>(std::stoul(versionStr.substr(0, dotPos)));
            outVersion.minor = static_cast<uint32_t>(std::stoul(versionStr.substr(dotPos + 1)));
            return true;
        }
        catch (...)
        {
            return false;
        }
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

    std::string ProjectSerialization::getEngineVersionString()
    {
        std::ostringstream oss;
        oss << "VertexForge " << Version::major << "." << Version::minor << "." << Version::patch;
        return oss.str();
    }
}
