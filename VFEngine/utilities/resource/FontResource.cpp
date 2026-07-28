#include "FontResource.hpp"
#include "../print/Log.hpp"
#include "EndianUtils.hpp"
#include "VfFontHeader.hpp"
#include "VFSHelpers.hpp"

#include <cmath>
#include <exception>
#include <limits>
#include <sstream>
#include <string>

namespace resource
{
    namespace
    {
        constexpr size_t maxFontFileSize = 128ULL * 1024 * 1024;
        constexpr uint32_t maxAtlasDimension = 8192;
        constexpr uint32_t maxCharacterRanges = 1000;
        constexpr uint32_t maxGlyphs = 100000;
        constexpr uint32_t maxKerningPairs = 1000000;

        bool isZeroDistanceParameters(const SDFParameters& params)
        {
            return params.spread == 0.0f && params.padding == 0 &&
                   params.edgeValue == 0.0f && params.pxRange == 0.0f;
        }

        bool hasKnownFlags(FontFormatFlags flags)
        {
            constexpr uint32_t knownFlags =
                static_cast<uint32_t>(FontFormatFlags::SDF_ENABLED) |
                static_cast<uint32_t>(FontFormatFlags::KERNING_ENABLED) |
                static_cast<uint32_t>(FontFormatFlags::MULTI_SIZE) |
                static_cast<uint32_t>(FontFormatFlags::COLOR_EMOJI) |
                static_cast<uint32_t>(FontFormatFlags::MSDF_ENABLED);
            return (static_cast<uint32_t>(flags) & ~knownFlags) == 0;
        }

        bool hasValidModeParameters(const FontData& fontData)
        {
            const bool sdf = hasFlag(fontData.formatFlags, FontFormatFlags::SDF_ENABLED);
            const bool msdf = hasFlag(fontData.formatFlags, FontFormatFlags::MSDF_ENABLED);
            const bool color = hasFlag(fontData.formatFlags, FontFormatFlags::COLOR_EMOJI);
            const auto& params = fontData.sdfParams;

            if (!std::isfinite(params.spread) || !std::isfinite(params.edgeValue) ||
                !std::isfinite(params.pxRange))
            {
                return false;
            }

            switch (fontData.atlas.format)
            {
                case FontAtlasFormat::GRAYSCALE_8:
                    return !sdf && !msdf && !color && isZeroDistanceParameters(params);
                case FontAtlasFormat::SDF_8:
                    return sdf && !msdf && !color &&
                           params.spread > 0.0f &&
                           params.padding <= maxAtlasDimension &&
                           params.edgeValue >= 0.0f && params.edgeValue <= 1.0f &&
                           params.pxRange == 0.0f;
                case FontAtlasFormat::RGBA_32:
                    return !sdf && !msdf && color && isZeroDistanceParameters(params);
                case FontAtlasFormat::MTSDF_RGBA_32:
                    return sdf && msdf && !color &&
                           params.pxRange >= 1.0f && params.pxRange <= 16.0f &&
                           params.spread == params.pxRange &&
                           params.padding == 0 && params.edgeValue == 0.5f;
                default:
                    return false;
            }
        }
    }

    FontHeaderStatus FontResource::probeHeader(std::string_view path)
    {
        if (path.empty())
        {
            return FontHeaderStatus::Unreadable;
        }

        const auto data = resource::readFileBytes(std::string(path));
        if (data.size() < FONT_HEADER_SIZE)
        {
            return FontHeaderStatus::Unreadable;
        }

        try
        {
            std::string headerBytes(data.begin(), data.begin() + FONT_HEADER_SIZE);
            std::istringstream in(headerBytes, std::ios::binary);
            in.exceptions(std::ios::badbit | std::ios::failbit);

            VfFontHeader header;
            readVfFontHeader(in, header);
            return classifyVfFontHeader(header, data.size());
        }
        catch (const std::exception&)
        {
            return FontHeaderStatus::Unreadable;
        }
    }

    FontData FontResource::loadFont(std::string_view path)
    {
        if (path.empty())
        {
            vfLogError("Empty path provided for font loading");
            return {};
        }

        auto data = resource::readFileBytes(std::string(path));
        if (data.empty())
        {
            vfLogError("Failed to read font file: {}", path);
            return {};
        }
        if (data.size() > maxFontFileSize)
        {
            vfLogError("Font file too large ({} bytes, max {} bytes): {}",
                       data.size(), maxFontFileSize, path);
            return {};
        }

        try
        {
            std::string dataStr(data.begin(), data.end());
            std::istringstream inFile(dataStr, std::ios::binary);
            inFile.exceptions(std::ios::badbit | std::ios::failbit);

            using namespace endian;
            FontData fontData;

            VfFontHeader header;
            readVfFontHeader(inFile, header);

            fontData.headerFileType = static_cast<FileType>(header.fileType);
            fontData.version.major = header.versionMajor;
            fontData.version.minor = header.versionMinor;
            fontData.version.patch = header.versionPatch;

            // Same classifier the editor's staleness check uses, so "loadable here" and
            // "flagged as needing a reimport there" can never disagree.
            switch (classifyVfFontHeader(header, data.size()))
            {
            case FontHeaderStatus::Ok:
                break;
            case FontHeaderStatus::NotAFont:
                // Every .vfFont written before format 2.0.0 lands here: that layout had
                // no magic word at all. Nothing is recoverable from it.
                vfLogError("Not a readable .vfFont (bad magic or file type): {}. "
                           "Re-import the source font — text using it will render in the "
                           "built-in default typeface until you do.",
                           path);
                return {};
            case FontHeaderStatus::StaleVersion:
                vfLogError("Incompatible font format version in {}: {}.{}.{}, expected {}.{}.{}. "
                           "Re-import the source font — text using it will render in the "
                           "built-in default typeface until you do.",
                           path,
                           header.versionMajor, header.versionMinor, header.versionPatch,
                           FONT_FORMAT_VERSION_MAJOR, FONT_FORMAT_VERSION_MINOR,
                           FONT_FORMAT_VERSION_PATCH);
                return {};
            case FontHeaderStatus::Unreadable:
                vfLogError("Truncated font file: {}", path);
                return {};
            }

            fontData.formatFlags = static_cast<FontFormatFlags>(header.formatFlags);
            if (!hasKnownFlags(fontData.formatFlags))
            {
                vfLogError("Unknown font format flags: {}", header.formatFlags);
                return {};
            }

            const uint32_t nameLength = readLE<uint32_t>(inFile);
            if (nameLength >= 1024)
            {
                vfLogError("Invalid font name length: {}", nameLength);
                return {};
            }
            fontData.metadata.fontName.resize(nameLength);
            if (nameLength > 0)
                inFile.read(fontData.metadata.fontName.data(), nameLength);

            const uint32_t styleLength = readLE<uint32_t>(inFile);
            if (styleLength >= 1024)
            {
                vfLogError("Invalid font style length: {}", styleLength);
                return {};
            }
            fontData.metadata.fontStyle.resize(styleLength);
            if (styleLength > 0)
                inFile.read(fontData.metadata.fontStyle.data(), styleLength);

            fontData.metadata.baseFontSize = readLE<uint32_t>(inFile);
            fontData.metadata.lineHeight = readLE<float>(inFile);
            fontData.metadata.ascender = readLE<float>(inFile);
            fontData.metadata.descender = readLE<float>(inFile);
            fontData.metadata.underlinePosition = readLE<float>(inFile);
            fontData.metadata.underlineThickness = readLE<float>(inFile);
            if (fontData.metadata.baseFontSize == 0 ||
                !std::isfinite(fontData.metadata.lineHeight) ||
                !std::isfinite(fontData.metadata.ascender) ||
                !std::isfinite(fontData.metadata.descender) ||
                !std::isfinite(fontData.metadata.underlinePosition) ||
                !std::isfinite(fontData.metadata.underlineThickness))
            {
                vfLogError("Invalid font metadata in {}", path);
                return {};
            }

            fontData.sdfParams.spread = readLE<float>(inFile);
            fontData.sdfParams.padding = readLE<uint32_t>(inFile);
            fontData.sdfParams.edgeValue = readLE<float>(inFile);
            fontData.sdfParams.pxRange = readLE<float>(inFile);

            const uint32_t rangeCount = readLE<uint32_t>(inFile);
            if (rangeCount > maxCharacterRanges)
            {
                vfLogError("Invalid character range count: {}", rangeCount);
                return {};
            }
            fontData.characterRanges.reserve(rangeCount);
            for (uint32_t i = 0; i < rangeCount; ++i)
            {
                CharacterRange range;
                range.rangeStart = readLE<uint32_t>(inFile);
                range.rangeEnd = readLE<uint32_t>(inFile);
                if (range.rangeStart > range.rangeEnd)
                {
                    vfLogError("Invalid character range: {}-{}", range.rangeStart, range.rangeEnd);
                    return {};
                }
                fontData.characterRanges.push_back(range);
            }

            const uint32_t glyphCount = readLE<uint32_t>(inFile);
            if (glyphCount > maxGlyphs)
            {
                vfLogError("Invalid glyph count: {}", glyphCount);
                return {};
            }
            fontData.glyphs.reserve(glyphCount);
            for (uint32_t i = 0; i < glyphCount; ++i)
            {
                GlyphData glyph;
                glyph.codepoint = readLE<uint32_t>(inFile);
                glyph.advanceX = readLE<float>(inFile);
                glyph.advanceY = readLE<float>(inFile);
                glyph.bearingX = readLE<float>(inFile);
                glyph.bearingY = readLE<float>(inFile);
                glyph.glyphWidth = readLE<float>(inFile);
                glyph.glyphHeight = readLE<float>(inFile);
                glyph.atlasX = readLE<uint32_t>(inFile);
                glyph.atlasY = readLE<uint32_t>(inFile);
                glyph.atlasWidth = readLE<uint32_t>(inFile);
                glyph.atlasHeight = readLE<uint32_t>(inFile);
                glyph.glyphFlags = readLE<uint32_t>(inFile);
                if (!std::isfinite(glyph.advanceX) || !std::isfinite(glyph.advanceY) ||
                    !std::isfinite(glyph.bearingX) || !std::isfinite(glyph.bearingY) ||
                    !std::isfinite(glyph.glyphWidth) || !std::isfinite(glyph.glyphHeight) ||
                    glyph.glyphWidth < 0.0f || glyph.glyphHeight < 0.0f)
                {
                    vfLogError("Invalid glyph metrics for U+{:04X}", glyph.codepoint);
                    return {};
                }
                fontData.glyphs.push_back(glyph);
            }

            const uint32_t kerningCount = readLE<uint32_t>(inFile);
            if (kerningCount > maxKerningPairs)
            {
                vfLogError("Invalid kerning pair count: {}", kerningCount);
                return {};
            }
            fontData.kerningPairs.reserve(kerningCount);
            for (uint32_t i = 0; i < kerningCount; ++i)
            {
                KerningPair pair;
                pair.leftCodepoint = readLE<uint32_t>(inFile);
                pair.rightCodepoint = readLE<uint32_t>(inFile);
                pair.kerningAmount = readLE<float>(inFile);
                if (!std::isfinite(pair.kerningAmount))
                {
                    vfLogError("Invalid kerning amount");
                    return {};
                }
                fontData.kerningPairs.push_back(pair);
            }
            if (!hasFlag(fontData.formatFlags, FontFormatFlags::KERNING_ENABLED) &&
                !fontData.kerningPairs.empty())
            {
                vfLogError("Kerning data is present without KERNING_ENABLED");
                return {};
            }

            fontData.atlas.width = readLE<uint32_t>(inFile);
            fontData.atlas.height = readLE<uint32_t>(inFile);
            fontData.atlas.format = static_cast<FontAtlasFormat>(readLE<uint32_t>(inFile));
            if (fontData.atlas.width == 0 || fontData.atlas.height == 0 ||
                fontData.atlas.width > maxAtlasDimension ||
                fontData.atlas.height > maxAtlasDimension)
            {
                vfLogError("Invalid atlas dimensions: {}x{}",
                           fontData.atlas.width, fontData.atlas.height);
                return {};
            }

            const uint32_t bytesPerPixel = fontAtlasBytesPerPixel(fontData.atlas.format);
            if (bytesPerPixel == 0)
            {
                vfLogError("Unknown atlas format: {}",
                           static_cast<uint32_t>(fontData.atlas.format));
                return {};
            }

            const uint64_t expectedSize64 =
                static_cast<uint64_t>(fontData.atlas.width) *
                static_cast<uint64_t>(fontData.atlas.height) *
                bytesPerPixel;
            if (expectedSize64 > std::numeric_limits<uint32_t>::max() ||
                expectedSize64 > std::numeric_limits<size_t>::max())
            {
                vfLogError("Atlas size is not representable: {} bytes", expectedSize64);
                return {};
            }

            const uint32_t atlasDataSize = readLE<uint32_t>(inFile);
            if (atlasDataSize != expectedSize64)
            {
                vfLogError("Atlas data size mismatch: expected {}, got {}",
                           expectedSize64, atlasDataSize);
                return {};
            }

            fontData.atlas.pixels.resize(atlasDataSize);
            inFile.read(reinterpret_cast<char*>(fontData.atlas.pixels.data()), atlasDataSize);

            if (!hasValidModeParameters(fontData))
            {
                vfLogError("Inconsistent font atlas format, flags, or distance parameters");
                return {};
            }

            for (const auto& glyph : fontData.glyphs)
            {
                if (static_cast<uint64_t>(glyph.atlasX) + glyph.atlasWidth >
                        fontData.atlas.width ||
                    static_cast<uint64_t>(glyph.atlasY) + glyph.atlasHeight >
                        fontData.atlas.height)
                {
                    vfLogError("Glyph U+{:04X} lies outside the atlas", glyph.codepoint);
                    return {};
                }
            }

            if (inFile.rdbuf()->in_avail() != 0)
            {
                vfLogError("Unexpected trailing data in font file: {}", path);
                return {};
            }

            vfLogInfo("Loaded font: {} ({} glyphs, {}x{} atlas)",
                      fontData.metadata.fontName, fontData.glyphs.size(),
                      fontData.atlas.width, fontData.atlas.height);
            return fontData;
        }
        catch (const std::ios_base::failure&)
        {
            vfLogError("Truncated or unreadable font file: {}", path);
            return {};
        }
        catch (const std::bad_alloc&)
        {
            vfLogError("Failed to allocate memory while loading font: {}", path);
            return {};
        }
        catch (const std::exception& e)
        {
            vfLogError("Exception while loading font {}: {}", path, e.what());
            return {};
        }
    }
}
