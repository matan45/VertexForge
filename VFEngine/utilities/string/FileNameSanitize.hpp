#pragma once

#include <cctype>
#include <string>
#include <string_view>

// One home for "turn arbitrary text into a usable file name". Four byte-identical copies of the
// character filter had accumulated - import/types/Mesh.cpp, import/types/MeshTextureImport.cpp
// (whose comment openly called itself a near-copy), editor/windows/terrain/RoadMeshGenerator.cpp
// and editor/windows/world/WorldSectorWindowDataLayers.cpp - none of them reachable from the
// others, and none handling Win32 reserved device names.
//
// Header-only and dependency-free on purpose: Editor does not link Utilities (see the module graph
// in CLAUDE.md), so a .cpp here would be usable from the importer and not from the two editor
// call sites, which is how the copies started.
namespace strutil
{
    // Every character Win32 forbids in a path component, plus the C0 controls.
    [[nodiscard]] inline bool isIllegalFileNameChar(char c) noexcept
    {
        const auto uc = static_cast<unsigned char>(c);
        return uc < 0x20 || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' ||
               c == '\\' || c == '|' || c == '?' || c == '*';
    }

    // Replaces each illegal character with '_', then trims leading and trailing spaces and dots.
    // The trim is not cosmetic: Win32 silently drops a trailing dot or space, so "report." and
    // "report" name the same file, and a stem of "." or ".." would name a DIRECTORY rather than a
    // file. Returns an empty string when nothing usable remains - callers that need a guaranteed
    // name should use toSafeFileStem instead.
    [[nodiscard]] inline std::string sanitizeFileStem(std::string_view name)
    {
        std::string out;
        out.reserve(name.size());
        for (char c : name)
            out.push_back(isIllegalFileNameChar(c) ? '_' : c);

        const size_t start = out.find_first_not_of(" .");
        if (start == std::string::npos)
            return {};
        const size_t end = out.find_last_not_of(" .");
        return out.substr(start, end - start + 1);
    }

    // The Win32 reserved device names. Matched case-insensitively against the text BEFORE the
    // first period, which is how the Win32 layer itself matches them - so "CON.txt" opens the
    // console device, not a file called CON.txt.
    [[nodiscard]] inline bool isReservedDeviceName(std::string_view stem) noexcept
    {
        if (const size_t dot = stem.find('.'); dot != std::string_view::npos)
            stem = stem.substr(0, dot);

        if (stem.size() < 3 || stem.size() > 4)
            return false;

        char upper[5] = {};
        for (size_t i = 0; i < stem.size(); ++i)
            upper[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(stem[i])));
        const std::string_view s(upper, stem.size());

        if (s == "CON" || s == "PRN" || s == "AUX" || s == "NUL")
            return true;
        if (s.size() == 4 && (s.substr(0, 3) == "COM" || s.substr(0, 3) == "LPT"))
            return s[3] >= '1' && s[3] <= '9';
        return false;
    }

    // sanitizeFileStem, plus a guarantee that the result addresses a real, creatable file:
    // `fallback` when nothing usable survives, and a trailing '_' on a reserved device name.
    [[nodiscard]] inline std::string toSafeFileStem(std::string_view name,
                                                    std::string_view fallback = "unnamed")
    {
        std::string stem = sanitizeFileStem(name);
        if (stem.empty())
            stem = std::string(fallback);
        if (isReservedDeviceName(stem))
            stem += '_';
        return stem;
    }
}
