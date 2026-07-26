#include "print/Log.hpp"
#include "FontSerializer.hpp"
#include "resource/EndianUtils.hpp"
#include "resource/VfFontHeader.hpp"
#include "config/Config.hpp"

#include <fstream>
#include <filesystem>
#include <cmath>
#include <limits>

namespace types
{
    namespace
    {
        constexpr uint32_t maxAtlasDimension = 8192;
        constexpr uint32_t maxCharacterRanges = 1000;
        constexpr uint32_t maxGlyphs = 100000;
        constexpr uint32_t maxKerningPairs = 1000000;
        constexpr uint64_t maxFontFileSize = 128ULL * 1024 * 1024;

        bool isZeroDistanceParameters(const resource::SDFParameters& params)
        {
            return params.spread == 0.0f && params.padding == 0 &&
                   params.edgeValue == 0.0f && params.pxRange == 0.0f;
        }

        bool validateModeParameters(const resource::FontData& fontData)
        {
            const bool sdf = resource::hasFlag(
                fontData.formatFlags, resource::FontFormatFlags::SDF_ENABLED);
            const bool msdf = resource::hasFlag(
                fontData.formatFlags, resource::FontFormatFlags::MSDF_ENABLED);
            const bool color = resource::hasFlag(
                fontData.formatFlags, resource::FontFormatFlags::COLOR_EMOJI);
            const auto& params = fontData.sdfParams;

            if (!std::isfinite(params.spread) || !std::isfinite(params.edgeValue) ||
                !std::isfinite(params.pxRange))
            {
                return false;
            }

            switch (fontData.atlas.format)
            {
                case resource::FontAtlasFormat::GRAYSCALE_8:
                    return !sdf && !msdf && !color && isZeroDistanceParameters(params);
                case resource::FontAtlasFormat::SDF_8:
                    return sdf && !msdf && !color &&
                           params.spread > 0.0f &&
                           params.padding <= maxAtlasDimension &&
                           params.edgeValue >= 0.0f && params.edgeValue <= 1.0f &&
                           params.pxRange == 0.0f;
                case resource::FontAtlasFormat::RGBA_32:
                    return !sdf && !msdf && color && isZeroDistanceParameters(params);
                case resource::FontAtlasFormat::MTSDF_RGBA_32:
                    return sdf && msdf && !color &&
                           params.pxRange >= 1.0f && params.pxRange <= 16.0f &&
                           params.spread == params.pxRange &&
                           params.padding == 0 && params.edgeValue == 0.5f;
                default:
                    return false;
            }
        }

        bool validateFontData(const resource::FontData& fontData)
        {
            if (fontData.headerFileType != resource::FileType::FONT)
            {
                vfLogError("Cannot serialize font with invalid file type");
                return false;
            }

            constexpr uint32_t knownFlags =
                static_cast<uint32_t>(resource::FontFormatFlags::SDF_ENABLED) |
                static_cast<uint32_t>(resource::FontFormatFlags::KERNING_ENABLED) |
                static_cast<uint32_t>(resource::FontFormatFlags::MULTI_SIZE) |
                static_cast<uint32_t>(resource::FontFormatFlags::COLOR_EMOJI) |
                static_cast<uint32_t>(resource::FontFormatFlags::MSDF_ENABLED);
            if ((static_cast<uint32_t>(fontData.formatFlags) & ~knownFlags) != 0)
            {
                vfLogError("Cannot serialize font with unknown format flags");
                return false;
            }

            if (fontData.metadata.fontName.size() >= 1024 ||
                fontData.metadata.fontStyle.size() >= 1024 ||
                fontData.metadata.baseFontSize == 0 ||
                !std::isfinite(fontData.metadata.lineHeight) ||
                !std::isfinite(fontData.metadata.ascender) ||
                !std::isfinite(fontData.metadata.descender) ||
                !std::isfinite(fontData.metadata.underlinePosition) ||
                !std::isfinite(fontData.metadata.underlineThickness))
            {
                vfLogError("Cannot serialize font with invalid metadata");
                return false;
            }

            if (fontData.characterRanges.size() > maxCharacterRanges ||
                fontData.glyphs.size() > maxGlyphs ||
                fontData.kerningPairs.size() > maxKerningPairs)
            {
                vfLogError("Cannot serialize font with excessive table counts");
                return false;
            }

            for (const auto& range : fontData.characterRanges)
            {
                if (range.rangeStart > range.rangeEnd)
                {
                    vfLogError("Cannot serialize font with an invalid character range");
                    return false;
                }
            }

            if (fontData.atlas.width == 0 || fontData.atlas.height == 0 ||
                fontData.atlas.width > maxAtlasDimension ||
                fontData.atlas.height > maxAtlasDimension)
            {
                vfLogError("Cannot serialize font with invalid atlas dimensions");
                return false;
            }

            const uint32_t bytesPerPixel =
                resource::fontAtlasBytesPerPixel(fontData.atlas.format);
            if (bytesPerPixel == 0 || !validateModeParameters(fontData))
            {
                vfLogError("Cannot serialize font with inconsistent atlas format, flags, or parameters");
                return false;
            }

            const uint64_t expectedAtlasSize =
                static_cast<uint64_t>(fontData.atlas.width) *
                static_cast<uint64_t>(fontData.atlas.height) *
                bytesPerPixel;
            if (expectedAtlasSize != fontData.atlas.pixels.size() ||
                expectedAtlasSize > std::numeric_limits<uint32_t>::max())
            {
                vfLogError("Cannot serialize font: atlas requires {} bytes but contains {}",
                           expectedAtlasSize, fontData.atlas.pixels.size());
                return false;
            }

            for (const auto& glyph : fontData.glyphs)
            {
                if (!std::isfinite(glyph.advanceX) || !std::isfinite(glyph.advanceY) ||
                    !std::isfinite(glyph.bearingX) || !std::isfinite(glyph.bearingY) ||
                    !std::isfinite(glyph.glyphWidth) || !std::isfinite(glyph.glyphHeight) ||
                    glyph.glyphWidth < 0.0f || glyph.glyphHeight < 0.0f ||
                    static_cast<uint64_t>(glyph.atlasX) + glyph.atlasWidth > fontData.atlas.width ||
                    static_cast<uint64_t>(glyph.atlasY) + glyph.atlasHeight > fontData.atlas.height)
                {
                    vfLogError("Cannot serialize font with invalid glyph data for U+{:04X}",
                               glyph.codepoint);
                    return false;
                }
            }

            for (const auto& pair : fontData.kerningPairs)
            {
                if (!std::isfinite(pair.kerningAmount))
                {
                    vfLogError("Cannot serialize font with a non-finite kerning amount");
                    return false;
                }
            }

            if (!resource::hasFlag(fontData.formatFlags,
                                   resource::FontFormatFlags::KERNING_ENABLED) &&
                !fontData.kerningPairs.empty())
            {
                vfLogError("Cannot serialize kerning pairs without KERNING_ENABLED");
                return false;
            }

            const uint64_t serializedSize =
                resource::FONT_HEADER_SIZE +
                48ULL + fontData.metadata.fontName.size() + fontData.metadata.fontStyle.size() +
                4ULL + 8ULL * fontData.characterRanges.size() +
                4ULL + 48ULL * fontData.glyphs.size() +
                4ULL + 12ULL * fontData.kerningPairs.size() +
                16ULL + expectedAtlasSize;
            if (serializedSize > maxFontFileSize)
            {
                vfLogError("Cannot serialize font larger than {} bytes", maxFontFileSize);
                return false;
            }

            return true;
        }

        void writeHeader(std::ofstream& outFile, const resource::FontData& fontData)
        {
            // Version fields come from the VfFontHeader defaults - the .vfFont format
            // owns its version and is intentionally decoupled from the engine version.
            resource::VfFontHeader header;
            header.fileType = static_cast<uint8_t>(fontData.headerFileType);
            header.formatFlags = static_cast<uint32_t>(fontData.formatFlags);
            resource::writeVfFontHeader(outFile, header);
        }

        void writeMetadata(std::ofstream& outFile, const resource::FontData& fontData)
        {
            using namespace resource::endian;
            auto nameBytes = static_cast<uint32_t>(fontData.metadata.fontName.size());
            writeLE<uint32_t>(outFile, nameBytes);
            outFile.write(fontData.metadata.fontName.data(), nameBytes);

            auto styleBytes = static_cast<uint32_t>(fontData.metadata.fontStyle.size());
            writeLE<uint32_t>(outFile, styleBytes);
            outFile.write(fontData.metadata.fontStyle.data(), styleBytes);

            writeLE<uint32_t>(outFile, fontData.metadata.baseFontSize);
            writeLE<float>(outFile, fontData.metadata.lineHeight);
            writeLE<float>(outFile, fontData.metadata.ascender);
            writeLE<float>(outFile, fontData.metadata.descender);
            writeLE<float>(outFile, fontData.metadata.underlinePosition);
            writeLE<float>(outFile, fontData.metadata.underlineThickness);

            writeLE<float>(outFile, fontData.sdfParams.spread);
            writeLE<uint32_t>(outFile, fontData.sdfParams.padding);
            writeLE<float>(outFile, fontData.sdfParams.edgeValue);
            writeLE<float>(outFile, fontData.sdfParams.pxRange);
        }

        void writeGlyphData(std::ofstream& outFile, const resource::FontData& fontData)
        {
            using namespace resource::endian;
            writeLE<uint32_t>(outFile, static_cast<uint32_t>(fontData.characterRanges.size()));
            for (const auto& range : fontData.characterRanges)
            {
                writeLE<uint32_t>(outFile, range.rangeStart);
                writeLE<uint32_t>(outFile, range.rangeEnd);
            }

            writeLE<uint32_t>(outFile, static_cast<uint32_t>(fontData.glyphs.size()));
            for (const auto& glyph : fontData.glyphs)
            {
                writeLE<uint32_t>(outFile, glyph.codepoint);
                writeLE<float>(outFile, glyph.advanceX);
                writeLE<float>(outFile, glyph.advanceY);
                writeLE<float>(outFile, glyph.bearingX);
                writeLE<float>(outFile, glyph.bearingY);
                writeLE<float>(outFile, glyph.glyphWidth);
                writeLE<float>(outFile, glyph.glyphHeight);
                writeLE<uint32_t>(outFile, glyph.atlasX);
                writeLE<uint32_t>(outFile, glyph.atlasY);
                writeLE<uint32_t>(outFile, glyph.atlasWidth);
                writeLE<uint32_t>(outFile, glyph.atlasHeight);
                writeLE<uint32_t>(outFile, glyph.glyphFlags);
            }
        }

        void writeKerningData(std::ofstream& outFile, const resource::FontData& fontData)
        {
            using namespace resource::endian;
            writeLE<uint32_t>(outFile, static_cast<uint32_t>(fontData.kerningPairs.size()));
            for (const auto& pair : fontData.kerningPairs)
            {
                writeLE<uint32_t>(outFile, pair.leftCodepoint);
                writeLE<uint32_t>(outFile, pair.rightCodepoint);
                writeLE<float>(outFile, pair.kerningAmount);
            }
        }

        void writeAtlasData(std::ofstream& outFile, const resource::FontData& fontData)
        {
            using namespace resource::endian;
            writeLE<uint32_t>(outFile, fontData.atlas.width);
            writeLE<uint32_t>(outFile, fontData.atlas.height);
            writeLE<uint32_t>(outFile, static_cast<uint32_t>(fontData.atlas.format));

            auto dataSize = static_cast<uint32_t>(fontData.atlas.pixels.size());
            writeLE<uint32_t>(outFile, dataSize);
            outFile.write(reinterpret_cast<const char*>(fontData.atlas.pixels.data()), dataSize);
        }

        bool finalizeTempFile(const std::filesystem::path& tempPath,
                              const std::filesystem::path& filePath)
        {
            std::error_code ec;
            if (std::filesystem::exists(filePath, ec))
            {
                std::filesystem::remove(filePath, ec);
                if (ec)
                {
                    vfLogError("Failed to remove existing font file {}: {}", filePath.string(), ec.message());
                    std::filesystem::remove(tempPath);
                    return false;
                }
            }
            else if (ec)
            {
                vfLogError("Failed to inspect existing font file {}: {}", filePath.string(), ec.message());
                std::filesystem::remove(tempPath);
                return false;
            }

            std::filesystem::rename(tempPath, filePath, ec);
            if (ec)
            {
                vfLogError("Failed to rename temp file to {}: {}", filePath.string(), ec.message());
                std::filesystem::remove(tempPath);
                return false;
            }
            return true;
        }
    } // anonymous namespace

    bool FontSerializer::saveToFile(std::string_view location, std::string_view fileName,
                                    const resource::FontData& fontData) const
    {
        if (!validateFontData(fontData))
            return false;

        std::filesystem::path filePath = std::filesystem::path(location) /
            (std::string(fileName) + "." + FileExtension::font);
        std::filesystem::path tempPath = filePath;
        tempPath += ".tmp";

        try
        {
            std::ofstream outFile(tempPath, std::ios::binary);
            if (!outFile)
            {
                vfLogError("Failed to open temp file for writing: {}", tempPath.string());
                return false;
            }
            outFile.exceptions(std::ios::badbit | std::ios::failbit);

            writeHeader(outFile, fontData);
            writeMetadata(outFile, fontData);
            writeGlyphData(outFile, fontData);
            writeKerningData(outFile, fontData);
            writeAtlasData(outFile, fontData);

            outFile.flush();
            outFile.close();

            if (!outFile)
            {
                vfLogError("Failed to flush/close font file: {}", tempPath.string());
                std::filesystem::remove(tempPath);
                return false;
            }

            return finalizeTempFile(tempPath, filePath);
        }
        catch (const std::ios_base::failure& e)
        {
            vfLogError("I/O error while writing font file {}: {}", tempPath.string(), e.what());
            std::error_code ec;
            std::filesystem::remove(tempPath, ec);
            return false;
        }
        catch (const std::exception& e)
        {
            vfLogError("Exception while writing font file {}: {}", tempPath.string(), e.what());
            std::error_code ec;
            std::filesystem::remove(tempPath, ec);
            return false;
        }
    }
}
