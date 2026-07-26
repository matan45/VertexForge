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
