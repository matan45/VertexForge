#pragma once

#include "config/ProjectConfig.hpp"
#include "resource/VirtualFileSystem.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace services::font_fallback
{
    // `archiveMode` is a parameter rather than a VFS query so the path arithmetic is
    // testable without mounting an archive; the overload below supplies the real answer.
    [[nodiscard]] inline std::vector<std::string> resolve(
        const config::ProjectConfig& project, bool archiveMode)
    {
        namespace fs = std::filesystem;

        std::vector<std::string> resolved;
        const size_t count =
            std::min(project.fontFallbackChain.size(), config::ProjectConfig::maxFontFallbacks);
        resolved.reserve(count);

        for (size_t i = 0; i < count; ++i)
        {
            fs::path path(project.fontFallbackChain[i]);

            // Stored fallbacks are relative to the project's Assets root in BOTH modes,
            // so both need the same join. In the editor workingDirectory is the absolute
            // Assets path; in a packaged build GameExporter ships workingDirectory =
            // "Assets" and archives every asset as "Assets/" + relativePath, which is
            // exactly the key the VFS lookup needs.
            if (path.is_relative())
            {
                path = fs::path(project.workingDirectory) / path;
            }

            // Archive keys are always forward-slashed; filesystem paths keep the
            // platform separator.
            resolved.push_back(archiveMode ? path.lexically_normal().generic_string()
                                           : path.lexically_normal().string());
        }
        return resolved;
    }

    [[nodiscard]] inline std::vector<std::string> resolve(const config::ProjectConfig& project)
    {
        return resolve(project, resource::VirtualFileSystem::instance().isArchiveMode());
    }
}

namespace services::font_asset
{
    // Whether an import result is a font atlas. Case-insensitive: the importer reports
    // its output path as it built it, and ".vfFont" / ".vffont" both round-trip through
    // the rest of the pipeline.
    [[nodiscard]] inline bool isFontOutput(std::string_view outputPath)
    {
        std::string ext = std::filesystem::path(outputPath).extension().string();
        std::ranges::transform(ext, ext.begin(), [](unsigned char c)
                               { return static_cast<char>(std::tolower(c)); });
        return ext == ".vffont";
    }
}
