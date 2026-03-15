#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <cstdint>

namespace plugin
{
    struct PluginDescriptor
    {
        std::string name;
        std::string version;       // "major.minor.patch"
        uint32_t apiVersion = 0;
        std::string author;
        std::string description;
        std::vector<std::string> capabilities;
        std::vector<std::string> dependencies;
        int loadOrder = 100;
        bool enabled = true;
        std::string library;       // DLL filename

        // Parsed from version string
        uint32_t versionMajor = 0;
        uint32_t versionMinor = 0;
        uint32_t versionPatch = 0;

        // Directory containing the .vfplugin file
        std::filesystem::path basePath;
        // Full path to the .vfplugin file itself
        std::filesystem::path descriptorPath;

        std::filesystem::path getLibraryPath() const
        {
            return basePath / library;
        }

        static std::optional<PluginDescriptor> loadFromFile(const std::filesystem::path& path);
    };
}
