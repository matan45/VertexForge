#pragma once

#include <cctype>
#include <cstddef>
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

    namespace detail
    {
        // The length of the Win32 reserved device name `stem` starts with, 0 when it starts with
        // none. Win32 matches the text BEFORE the first period, without its trailing spaces and
        // ignoring case - so "CON.txt" and "nul .tar.gz" are devices, not files. The set is the
        // launcher's twin (ProjectNameRules.java): CON, PRN, AUX, NUL, CONIN$, CONOUT$, and COM/LPT
        // followed by 0-9 or a superscript 1-3 (UTF-8 C2 B9, C2 B2, C2 B3).
        [[nodiscard]] inline std::size_t reservedDeviceNameLength(std::string_view stem) noexcept
        {
            if (const std::size_t dot = stem.find('.'); dot != std::string_view::npos)
                stem = stem.substr(0, dot);
            while (!stem.empty() && stem.back() == ' ')
                stem.remove_suffix(1);

            // The longest is CONOUT$ (7 bytes); COM followed by a superscript digit is 5.
            if (stem.size() < 3 || stem.size() > 7)
                return 0;

            // ASCII upper case only, whatever the C locale says: the superscripts have no case.
            char upper[7] = {};
            for (std::size_t i = 0; i < stem.size(); ++i)
            {
                const char c = stem[i];
                upper[i] = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
            }
            const std::string_view s(upper, stem.size());

            if (s == "CON" || s == "PRN" || s == "AUX" || s == "NUL" || s == "CONIN$" || s == "CONOUT$")
                return stem.size();
            const std::string_view prefix = s.substr(0, 3);
            if (prefix == "COM" || prefix == "LPT")
            {
                const std::string_view digit = s.substr(3);
                if ((digit.size() == 1 && digit[0] >= '0' && digit[0] <= '9') ||
                    digit == "\xC2\xB9" || digit == "\xC2\xB2" || digit == "\xC2\xB3")
                    return stem.size();
            }
            return 0;
        }

        // Escapes a reserved device name by putting '_' right after it ("CON" -> "CON_",
        // "NUL.txt" -> "NUL_.txt"), the only place where it changes what Win32 matches.
        inline void escapeReservedDeviceName(std::string& stem)
        {
            if (const std::size_t length = reservedDeviceNameLength(stem); length != 0)
                stem.insert(length, 1, '_');
        }
    }

    // The Win32 reserved device names (see detail::reservedDeviceNameLength for the matching rule).
    [[nodiscard]] inline bool isReservedDeviceName(std::string_view stem) noexcept
    {
        return detail::reservedDeviceNameLength(stem) != 0;
    }

    // sanitizeFileStem, plus a guarantee that the result addresses a real, creatable file:
    // `fallback` when nothing usable survives, and a '_' after a reserved device name.
    [[nodiscard]] inline std::string toSafeFileStem(std::string_view name,
                                                    std::string_view fallback = "unnamed")
    {
        std::string stem = sanitizeFileStem(name);
        if (stem.empty())
            stem = std::string(fallback);
        detail::escapeReservedDeviceName(stem);
        return stem;
    }

    // toSafeFileStem for a folder name that has to fit a path budget (VK-1726: a shipped game's
    // %LOCALAPPDATA%\<Publisher>\<Game>): at most `maxUtf16Units` UTF-16 units, a four-byte UTF-8
    // sequence counting two. The cut falls on a code point boundary and is trimmed again, as Win32
    // would drop a trailing dot or space. Returns `fallback` (unchecked) when nothing usable is
    // left, also when a budget below 8 units cuts a name down to a device name.
    [[nodiscard]] inline std::string toSafeFolderName(std::string_view name, std::size_t maxUtf16Units,
                                                      std::string_view fallback = "unnamed")
    {
        std::string folder = sanitizeFileStem(name);
        detail::escapeReservedDeviceName(folder);

        std::size_t units = 0;
        std::size_t cut = 0;
        while (cut < folder.size())
        {
            const auto lead = static_cast<unsigned char>(folder[cut]);
            std::size_t sequenceBytes = 1;  // ASCII, a stray continuation byte, an invalid lead byte
            std::size_t sequenceUnits = 1;
            if (lead >= 0xF0 && lead <= 0xF4)
            {
                sequenceBytes = 4;
                sequenceUnits = 2;  // a surrogate pair
            }
            else if (lead >= 0xE0 && lead <= 0xEF)
            {
                sequenceBytes = 3;
            }
            else if (lead >= 0xC2 && lead <= 0xDF)
            {
                sequenceBytes = 2;
            }
            if (sequenceBytes > folder.size() - cut)
                sequenceBytes = folder.size() - cut;  // a sequence cut short by the end of the text
            if (units + sequenceUnits > maxUtf16Units)
                break;
            units += sequenceUnits;
            cut += sequenceBytes;
        }
        folder.resize(cut);

        folder = sanitizeFileStem(folder);  // trims what the cut left at the end
        if (folder.empty() || isReservedDeviceName(folder))
            return std::string(fallback);
        return folder;
    }
}
