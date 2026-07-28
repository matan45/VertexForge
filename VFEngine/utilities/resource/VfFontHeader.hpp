#pragma once
#include "Types.hpp"
#include "EndianUtils.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <ostream>

// Single source of truth for the .vfFont on-disk header. Shared by the writer
// (import/types/FontSerializer.cpp, inside the Import DLL) and the reader
// (utilities/resource/FontResource.cpp, inside Utilities) so the two sides
// cannot drift.
//
// VK-1627: the .vfFont format version is owned HERE and is deliberately
// independent of the global engine version in utilities/config/Config.hpp.
// Bumping the engine version must never invalidate fonts. Bumping the constants
// below is a deliberate act that invalidates every .vfFont on disk and requires
// a re-import.
//
//   off  size  field
//   ---  ----  ---------------------------------------------------------
//     0     4  magic               'V','F','F','T'
//     4     1  fileType            (resource::FileType, FONT == 6)
//     5     4  formatVersionMajor  (FONT_FORMAT_VERSION_MAJOR)
//     9     4  formatVersionMinor  (FONT_FORMAT_VERSION_MINOR)
//    13     4  formatVersionPatch  (FONT_FORMAT_VERSION_PATCH)
//    17     4  formatFlags         (resource::FontFormatFlags)
//    21   ...  metadata block (see FontSerializer.cpp / FontResource.cpp)
//
// All multi-byte fields are little-endian (see EndianUtils.hpp).
//
// Note: FontData::version is a FileVersion, whose default member initialisers
// point at the GLOBAL engine version (Config.hpp). It only carries the font
// format version on a FontData returned successfully by FontResource::loadFont,
// which overwrites it from disk.
//
// Version history:
//   1.0.0 - implicit. Header carried the GLOBAL engine version and had no magic.
//   2.0.0 - VK-1627. Format-owned version + 'VFFT' magic. All 1.0.0 files are invalid.

namespace resource
{
    inline constexpr std::array<char, 4> FONT_MAGIC = {'V', 'F', 'F', 'T'};
    inline constexpr uint32_t FONT_FORMAT_VERSION_MAJOR = 2;
    inline constexpr uint32_t FONT_FORMAT_VERSION_MINOR = 0;
    inline constexpr uint32_t FONT_FORMAT_VERSION_PATCH = 0;
    inline constexpr std::size_t FONT_HEADER_SIZE = 21; // first byte of the metadata block

    // Raw header fields, in on-disk order. Defaults describe a header written by
    // the current engine; the reader overwrites every field from disk.
    struct VfFontHeader
    {
        std::array<char, 4> magic = FONT_MAGIC;
        uint8_t fileType = static_cast<uint8_t>(FileType::FONT);
        uint32_t versionMajor = FONT_FORMAT_VERSION_MAJOR;
        uint32_t versionMinor = FONT_FORMAT_VERSION_MINOR;
        uint32_t versionPatch = FONT_FORMAT_VERSION_PATCH;
        uint32_t formatFlags = 0;
    };

    // Why a .vfFont could not be accepted. Distinguishing these matters because the
    // remedy differs: NotAFont / StaleVersion both mean "re-import it", and that has to
    // be said out loud - VK-1628's default-font substitution otherwise turns a rejected
    // font into text that renders in the WRONG typeface rather than not at all.
    enum class FontHeaderStatus : uint8_t
    {
        Ok,
        Unreadable,   // missing, unreadable, or shorter than the header
        NotAFont,     // wrong magic or fileType - includes every pre-2.0.0 file, which had no magic
        StaleVersion, // 'VFFT' but a format version this engine does not read
    };

    // Pure classification of an already-read header. `byteCount` is how much of the file
    // was actually available, so a truncated file is reported as Unreadable rather than
    // as a garbage version. The reader and the editor's staleness check both go through
    // here so they cannot disagree about what "loadable" means.
    [[nodiscard]] inline FontHeaderStatus classifyVfFontHeader(const VfFontHeader& header,
                                                               std::size_t byteCount) noexcept
    {
        if (byteCount < FONT_HEADER_SIZE)
        {
            return FontHeaderStatus::Unreadable;
        }
        if (header.magic != FONT_MAGIC ||
            header.fileType != static_cast<uint8_t>(FileType::FONT))
        {
            return FontHeaderStatus::NotAFont;
        }
        if (header.versionMajor != FONT_FORMAT_VERSION_MAJOR ||
            header.versionMinor != FONT_FORMAT_VERSION_MINOR ||
            header.versionPatch != FONT_FORMAT_VERSION_PATCH)
        {
            return FontHeaderStatus::StaleVersion;
        }
        return FontHeaderStatus::Ok;
    }

    // Writes the header in exactly the layout above. Does NOT write the metadata block.
    inline void writeVfFontHeader(std::ostream& out, const VfFontHeader& header)
    {
        out.write(header.magic.data(), 4);
        endian::writeLE<uint8_t>(out, header.fileType);
        endian::writeLE<uint32_t>(out, header.versionMajor);
        endian::writeLE<uint32_t>(out, header.versionMinor);
        endian::writeLE<uint32_t>(out, header.versionPatch);
        endian::writeLE<uint32_t>(out, header.formatFlags);
    }

    // Reads the header sequentially into `header`. Performs no validation - the
    // caller checks magic / fileType / version. The stream is left positioned at
    // the first byte of the metadata block.
    inline void readVfFontHeader(std::istream& in, VfFontHeader& header)
    {
        // Zero first: the struct defaults to a VALID magic, so a truncated read
        // would otherwise leave FONT_MAGIC in place and slip past the caller's gate.
        header.magic.fill('\0');
        in.read(header.magic.data(), 4);

        header.fileType = endian::readLE<uint8_t>(in);
        header.versionMajor = endian::readLE<uint32_t>(in);
        header.versionMinor = endian::readLE<uint32_t>(in);
        header.versionPatch = endian::readLE<uint32_t>(in);
        header.formatFlags = endian::readLE<uint32_t>(in);
    }
}
