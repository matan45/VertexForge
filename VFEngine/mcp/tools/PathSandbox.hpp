#pragma once

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace mcp::tools
{
    // Tool arguments and results carry paths as UTF-8. Narrow std::filesystem::path
    // construction uses the ANSI code page on Windows, so always go through u8string.
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
        inline bool componentEquals(const std::filesystem::path& a, const std::filesystem::path& b)
        {
#ifdef _WIN32
            // NTFS paths are case-insensitive; weakly_canonical only normalises the
            // case of the components that exist on disk.
            const std::wstring& left = a.native();
            const std::wstring& right = b.native();
            return std::equal(left.begin(), left.end(), right.begin(), right.end(),
                              [](wchar_t x, wchar_t y) { return std::towlower(x) == std::towlower(y); });
#else
            return a == b;
#endif
        }

        // True when `candidate` is `root` or lies beneath it. Both must already be
        // canonical (no ".", "..", or redundant separators).
        inline bool isWithin(const std::filesystem::path& root, const std::filesystem::path& candidate)
        {
            auto rootIt = root.begin();
            auto candidateIt = candidate.begin();
            for (; rootIt != root.end(); ++rootIt, ++candidateIt)
            {
                // A trailing separator on the root yields an empty final element.
                if (rootIt->empty())
                {
                    continue;
                }
                if (candidateIt == candidate.end() || !componentEquals(*rootIt, *candidateIt))
                {
                    return false;
                }
            }
            return true;
        }
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
