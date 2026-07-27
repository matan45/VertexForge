#pragma once

#include "config/ProjectConfig.hpp"
#include "resource/VirtualFileSystem.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace services::font_fallback
{
    [[nodiscard]] inline std::vector<std::string> resolve(
        const config::ProjectConfig& project)
    {
        namespace fs = std::filesystem;

        std::vector<std::string> resolved;
        const size_t count =
            std::min(project.fontFallbackChain.size(), config::ProjectConfig::maxFontFallbacks);
        resolved.reserve(count);

        const bool archiveMode = resource::VirtualFileSystem::instance().isArchiveMode();
        for (size_t i = 0; i < count; ++i)
        {
            fs::path path(project.fontFallbackChain[i]);
            if (archiveMode)
            {
                // Exported project paths are already archive-relative (normally
                // Assets/...). Prefixing the shipped workingDirectory would
                // produce Assets/Assets/... and miss the VFS entry.
                resolved.push_back(path.lexically_normal().generic_string());
                continue;
            }

            if (path.is_relative())
            {
                path = fs::path(project.workingDirectory) / path;
            }
            resolved.push_back(path.lexically_normal().string());
        }
        return resolved;
    }
}
