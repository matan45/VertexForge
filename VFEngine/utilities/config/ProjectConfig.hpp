#pragma once
#include <cstddef>
#include <string>
#include <cstdint>
#include <optional>
#include <vector>

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
        static constexpr size_t maxFontFallbacks = 3;

        std::string projectName;
        std::string version;
        std::string workingDirectory;
        std::string startupScene;
        // Project-relative .vfFont paths, tried in authored order before the
        // engine's implicit built-in default font.
        std::vector<std::string> fontFallbackChain;

        std::string exeIconPath;
        std::optional<std::string> inputMapping;
        std::optional<std::string> engineVersion;
        std::optional<std::string> lastModified;
        // Stamped into exported .vfproj files; the Runtime warns on boot when it
        // differs from its own plugin::VF_PLUGIN_API_VERSION (plugin DLLs built
        // against another engine build would be rejected at load time).
        std::optional<uint32_t> pluginApiVersion;
        // VK-1726: who publishes the game (Project Settings; empty = none). A shipped game keeps its
        // config, saves, logs and crash reports in %LOCALAPPDATA%\<publisher>\<projectName>
        // (launch/GameStateLocation.hpp).
        std::string publisher;
        // VK-1726: where a shipped game keeps that state: "user" (the per-user folder above) or
        // "portable" (beside its .vfproj). Written by the exporter into the shipped .vfproj only;
        // the editor never reads it (its Play mode uses <project>/.vertexforge).
        std::optional<std::string> stateLocation;

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
