#pragma once

#include "../launch/ProjectPathRules.hpp"

#include <exception>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// VK-1726: the .vfpak key a path names. The exporter stores the game folder's files under relative,
// '/'-separated keys ("Assets/terrain/x.vfTerrain"), and the Runtime opens them by paths relative to its
// working directory, the folder that holds the .vfpak. A save slot lives outside that folder
// (%LOCALAPPDATA%\<Publisher>\<Game>\saves), so a scene saved into it refers to the game's terrain by an
// ABSOLUTE path (SceneSerialization::makeSettingsRefPath); that path must still find the archive entry.
namespace resource
{
    namespace archivePathKeyDetail
    {
        inline bool isSeparator(char c) noexcept
        {
            return c == '\\' || c == '/';
        }

        // True when `path` is spelled as an absolute Windows path: a drive ("C:\", "C:/") or a UNC or device
        // prefix ("\\server", "//server"). Checked on the text, before any path object is built, so a relative
        // key costs what it always did.
        inline bool spelledAbsolute(std::string_view path) noexcept
        {
            if (path.size() >= 3 && path[1] == ':' && isSeparator(path[2]))
            {
                const char drive = path[0];
                return (drive >= 'A' && drive <= 'Z') || (drive >= 'a' && drive <= 'z');
            }
            return path.size() >= 2 && isSeparator(path[0]) && isSeparator(path[1]);
        }

        inline std::string withForwardSlashes(std::string_view path)
        {
            std::string key(path);
            for (char& c : key)
            {
                if (c == '\\')
                {
                    c = '/';
                }
            }
            return key;
        }
    }

    // The archive key for `path`. A relative path is its own key with '\' turned into '/', exactly as before
    // VK-1726. An absolute path that lies inside one of `archiveRoots` (absolute folders that hold the .vfpak,
    // compared component by component, ignoring case, without touching the disk) becomes its relative,
    // '/'-separated key; any other path is returned with '/' separators, misses the archive, and the caller reads
    // it from disk. Never throws.
    inline std::string archiveKeyFor(std::string_view path, std::span<const std::filesystem::path> archiveRoots)
    {
        if (!archiveRoots.empty() && archivePathKeyDetail::spelledAbsolute(path))
        {
            try
            {
                const std::filesystem::path absolutePath{std::string(path)};
                for (const std::filesystem::path& root : archiveRoots)
                {
                    const std::optional<std::filesystem::path> relative =
                        launch::lexicallyRelativeInside(root, absolutePath);
                    if (relative && *relative != std::filesystem::path("."))
                    {
                        return relative->generic_string();
                    }
                }
            }
            catch (const std::exception&)
            {
                // Text this process cannot turn into a path (not valid in its code page): the plain key below.
            }
        }
        return archivePathKeyDetail::withForwardSlashes(path);
    }

    inline std::string archiveKeyFor(std::string_view path, const std::filesystem::path& archiveRoot)
    {
        return archiveKeyFor(path, std::span<const std::filesystem::path>(&archiveRoot, 1));
    }
}
