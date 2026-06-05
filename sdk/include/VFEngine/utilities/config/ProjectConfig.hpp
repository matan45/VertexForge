#pragma once
#include <string>
#include <cstdint>
#include <optional>

namespace config
{
    struct ProjectSchemaVersion
    {
        static constexpr uint32_t major = 1;
        static constexpr uint32_t minor = 0;

        static std::string toString()
        {
            return std::to_string(major) + "." + std::to_string(minor);
        }
    };

    struct ProjectFileVersion
    {
        uint32_t major = ProjectSchemaVersion::major;
        uint32_t minor = ProjectSchemaVersion::minor;

        bool operator==(const ProjectFileVersion& other) const
        {
            return major == other.major && minor == other.minor;
        }

        bool operator<(const ProjectFileVersion& other) const
        {
            if (major != other.major) return major < other.major;
            return minor < other.minor;
        }

        std::string toString() const
        {
            return std::to_string(major) + "." + std::to_string(minor);
        }

        bool isCompatible() const
        {
            return major == ProjectSchemaVersion::major;
        }
    };

    struct ProjectConfig
    {
        std::string projectName;
        std::string version;
        std::string workingDirectory;
        std::string startupScene;

        std::string exeIconPath;
        std::optional<std::string> inputMapping;
        std::optional<std::string> engineVersion;
        std::optional<std::string> lastModified;

        ProjectFileVersion schemaVersion;

        bool isValid() const
        {
            return !projectName.empty() &&
                   !version.empty() &&
                   !workingDirectory.empty() &&
                   !startupScene.empty();
        }
    };

}
