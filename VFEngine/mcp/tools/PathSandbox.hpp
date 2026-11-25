#pragma once

#include "launch/ProjectPathRules.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace mcp::tools
{
    // Tool arguments and results carry paths as UTF-8. Narrow std::filesystem::path
    // construction uses the process code page (UTF-8 under the VK-1684 manifest, the ANSI
    // code page on Windows older than 10 1903), so always go through u8string.
    inline std::filesystem::path pathFromUtf8(std::string_view utf8)
    {
        return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
    }

    inline std::string pathToUtf8(const std::filesystem::path& path)
    {
        const std::u8string utf8 = path.u8string();
        return std::string(utf8.begin(), utf8.end());
    }

    inline std::string genericPathToUtf8(const std::filesystem::path& path)
    {
        const std::u8string utf8 = path.generic_u8string();
        return std::string(utf8.begin(), utf8.end());
    }

    namespace detail
    {
        // VK-1687: shared with the project path rules (utilities/launch/ProjectPathRules.hpp):
        // case-insensitive on Windows; isWithin takes canonical paths, ignores empty elements
        // (a trailing separator) and treats an empty root as containing nothing.
        using launch::componentEquals;
        using launch::isWithin;
    }

    // Resolves `relative` (UTF-8; relative to `root`, or absolute) and returns the
    // canonical absolute path only when it is `root` itself or lies beneath it after
    // "..", symlinks and junctions are resolved. When `requiredExtension` is given
    // (e.g. ".mt"), the file name must end with it (case-insensitive). On rejection
    // returns nullopt and sets `error`. Nothing is created on disk.
    inline std::optional<std::filesystem::path> resolveInside(const std::filesystem::path& root,
                                                              const std::string& relative,
                                                              std::string& error,
                                                              std::string_view requiredExtension = {})
    {
        namespace fs = std::filesystem;

        if (relative.empty())
        {
            error = "path is empty";
            return std::nullopt;
        }
        if (relative.find('\0') != std::string::npos)
        {
            error = "path contains a NUL character";
            return std::nullopt;
        }

        std::error_code ec;
        const fs::path canonicalRoot = fs::weakly_canonical(fs::absolute(root, ec), ec);
        if (ec || canonicalRoot.empty())
        {
            error = "cannot resolve sandbox root '" + pathToUtf8(root) + "'";
            return std::nullopt;
        }

        const fs::path requested = pathFromUtf8(relative);
        const fs::path joined = requested.is_absolute() ? requested : canonicalRoot / requested;
        const fs::path candidate = fs::weakly_canonical(joined, ec);
        if (ec || candidate.empty())
        {
            error = "cannot resolve path '" + relative + "'";
            return std::nullopt;
        }

        if (!detail::isWithin(canonicalRoot, candidate))
        {
            error = "path '" + relative + "' is outside the allowed root '" + pathToUtf8(canonicalRoot) + "'";
            return std::nullopt;
        }

        if (!requiredExtension.empty())
        {
            const fs::path required = pathFromUtf8(requiredExtension);
            if (!candidate.has_filename() || !detail::componentEquals(candidate.extension(), required))
            {
                error = "path '" + relative + "' must have the " + std::string(requiredExtension) + " extension";
                return std::nullopt;
            }
        }

        return candidate;
    }
}
