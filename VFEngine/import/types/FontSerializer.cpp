#include "FontSerializer.hpp"
#include "resource/EndianUtils.hpp"
#include "config/Config.hpp"

#include <fstream>
#include <filesystem>

namespace types
{
    namespace
    {
        void writeHeader(std::ofstream& outFile, const resource::FontData& fontData)
        {
            using namespace resource::endian;
            writeLE<uint8_t>(outFile, static_cast<uint8_t>(fontData.headerFileType));
            writeLE<uint32_t>(outFile, Version::major);
            writeLE<uint32_t>(outFile, Version::minor);
            writeLE<uint32_t>(outFile, Version::patch);
            writeLE<uint32_t>(outFile, static_cast<uint32_t>(fontData.formatFlags));
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
            writeLE<uint32_t>(outFile, fontData.sdfParams.reserved);
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

        void finalizeTempFile(const std::filesystem::path& tempPath,
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
                    return;
                }
            }

            std::filesystem::rename(tempPath, filePath, ec);
            if (ec)
            {
                vfLogError("Failed to rename temp file to {}: {}", filePath.string(), ec.message());
                std::filesystem::remove(tempPath);
            }
        }
    } // anonymous namespace

    void FontSerializer::saveToFile(std::string_view location, std::string_view fileName,
                                     const resource::FontData& fontData) const
    {
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
                return;
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
                return;
            }

            finalizeTempFile(tempPath, filePath);
        }
        catch (const std::ios_base::failure& e)
        {
            vfLogError("I/O error while writing font file {}: {}", tempPath.string(), e.what());
            std::error_code ec;
            std::filesystem::remove(tempPath, ec);
        }
        catch (const std::exception& e)
        {
            vfLogError("Exception while writing font file {}: {}", tempPath.string(), e.what());
            std::error_code ec;
            std::filesystem::remove(tempPath, ec);
        }
    }
}
