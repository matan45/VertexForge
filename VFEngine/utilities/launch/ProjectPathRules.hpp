#pragma once

#include "EditorStateDirectory.hpp"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

// VK-1687: which paths lie inside a project folder, and how a path inside it is written relative to
// it, so that a moved or cloned project still finds its own files. Paths are compared the way the file
// system compares them (case-insensitive on Windows), after "..", symlinks and junctions are resolved.
// The MCP path sandbox (mcp/tools/PathSandbox.hpp) uses the same componentEquals / isWithin.
namespace launch
{
    // True when two paths (or single components) name the same thing. NTFS paths are
    // case-insensitive; weakly_canonical only normalises the case of the components that exist on
    // disk. Compares the whole native string, so it works for full paths too.
    inline bool componentEquals(const std::filesystem::path& a, const std::filesystem::path& b)
    {
#ifdef _WIN32
        const std::wstring& left = a.native();
        const std::wstring& right = b.native();
        return std::equal(left.begin(), left.end(), right.begin(), right.end(),
                          [](wchar_t x, wchar_t y) { return std::towlower(x) == std::towlower(y); });
#else
        return a == b;
#endif
    }

    namespace detail
    {
        // A trailing separator yields an empty final element; it is not a component.
        inline void skipEmptyElements(std::filesystem::path::const_iterator& it,
                                      const std::filesystem::path::const_iterator& end)
        {
            while (it != end && it->empty())
            {
                ++it;
            }
        }

        // Walks the components of `root` against the leading components of `candidate`. On success
        // `below` is the first element of `candidate` past `root`. A root with no component at all
        // (an empty path) contains nothing.
        inline bool matchLeadingComponents(const std::filesystem::path& root,
                                           const std::filesystem::path& candidate,
                                           std::filesystem::path::const_iterator& below)
        {
            auto rootIt = root.begin();
            below = candidate.begin();
            bool matchedAny = false;
            for (;;)
            {
                skipEmptyElements(rootIt, root.end());
                if (rootIt == root.end())
                {
                    return matchedAny;
                }
                skipEmptyElements(below, candidate.end());
                if (below == candidate.end() || !componentEquals(*rootIt, *below))
                {
                    return false;
                }
                matchedAny = true;
                ++rootIt;
                ++below;
            }
        }

        // The absolute, weakly canonical form of `path`; nullopt when it is empty or cannot be made
        // absolute. Some file systems (RAM disks, network redirectors) cannot report a final path,
        // so weakly_canonical fails on them; their paths are then compared lexically instead of
        // everything on them counting as outside every folder.
        inline std::optional<std::filesystem::path> canonicalAbsolute(const std::filesystem::path& path)
        {
            if (path.empty())
            {
                return std::nullopt;
            }
            std::error_code ec;
            const std::filesystem::path absolute = std::filesystem::absolute(path, ec);
            if (ec || absolute.empty())
            {
                return std::nullopt;
            }
            std::filesystem::path canonical = std::filesystem::weakly_canonical(absolute, ec);
            if (ec || canonical.empty())
            {
                canonical = absolute.lexically_normal();
            }
            return canonical;
        }
    }

    // True when `candidate` is `root` or lies beneath it. Both must already be canonical (no ".",
    // ".." or redundant separators). An empty root contains nothing.
    inline bool isWithin(const std::filesystem::path& root, const std::filesystem::path& candidate)
    {
        std::filesystem::path::const_iterator below;
        return detail::matchLeadingComponents(root, candidate, below);
    }

    // `path` relative to `root` when it is `root` itself (".") or lies beneath it, compared component by
    // component and ignoring case; nullopt when it lies outside, or either path is empty. Unlike
    // relativeInside it never touches the file system: both paths are only lexically normalised, so pass
    // a `root` that is already absolute and canonical. Cheap enough for every archive lookup
    // (resource::archiveKeyFor, VK-1726).
    inline std::optional<std::filesystem::path> lexicallyRelativeInside(const std::filesystem::path& root,
                                                                        const std::filesystem::path& path)
    {
        if (root.empty() || path.empty())
        {
            return std::nullopt;
        }
        const std::filesystem::path normalRoot = root.lexically_normal();
        const std::filesystem::path normalPath = path.lexically_normal();

        std::filesystem::path::const_iterator below;
        if (!detail::matchLeadingComponents(normalRoot, normalPath, below))
        {
            return std::nullopt;
        }

        std::filesystem::path relative;
        for (; below != normalPath.end(); ++below)
        {
            if (!below->empty())
            {
                relative /= *below;
            }
        }
        if (relative.empty())
        {
            relative = ".";
        }
        return relative;
    }

    // `path` relative to `root` when it is `root` itself (".") or lies beneath it; nullopt when it
    // lies outside, or either path is empty or cannot be made absolute. Both may be relative (to
    // the current directory) and need not exist. Store the result with generic_string(). Never
    // built with lexically_relative, which compares case-sensitively ("c:\proj" vs "C:\Proj"
    // gives "..\..").
    inline std::optional<std::filesystem::path> relativeInside(const std::filesystem::path& root,
                                                               const std::filesystem::path& path)
    {
        const std::optional<std::filesystem::path> canonicalRoot = detail::canonicalAbsolute(root);
        const std::optional<std::filesystem::path> canonicalPath = detail::canonicalAbsolute(path);
        if (!canonicalRoot || !canonicalPath)
        {
            return std::nullopt;
        }

        std::filesystem::path::const_iterator below;
        if (!detail::matchLeadingComponents(*canonicalRoot, *canonicalPath, below))
        {
            return std::nullopt;
        }

        std::filesystem::path relative;
        for (; below != canonicalPath->end(); ++below)
        {
            if (!below->empty())
            {
                relative /= *below;
            }
        }
        if (relative.empty())
        {
            relative = ".";
        }
        return relative;
    }

    // True when `path` is `root` or lies beneath it (see relativeInside).
    inline bool liesInside(const std::filesystem::path& root, const std::filesystem::path& path)
    {
        return relativeInside(root, path).has_value();
    }

    // True when both paths name the same folder.
    inline bool sameFolder(const std::filesystem::path& a, const std::filesystem::path& b)
    {
        return liesInside(a, b) && liesInside(b, a);
    }

    // True when the editor may create the startup scene `scenePath` for the project whose .vfproj
    // lies in `projectFolder`: a .vfScene file beneath that folder, outside the editor's own state
    // folder (EditorStateDirectory.hpp). A scene is never created anywhere else.
    inline bool starterSceneAllowed(const std::filesystem::path& projectFolder, const std::filesystem::path& scenePath)
    {
        const std::optional<std::filesystem::path> relative = relativeInside(projectFolder, scenePath);
        if (!relative || !relative->has_filename() || *relative == ".")
        {
            return false;
        }

        const std::filesystem::path stateFolder(editorStateDirName);
        for (const std::filesystem::path& part : *relative)
        {
            if (componentEquals(part, stateFolder))
            {
                return false;
            }
        }

        return componentEquals(relative->extension(), std::filesystem::path(L".vfScene"));
    }
}
