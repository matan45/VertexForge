#include "FontResource.hpp"
#include "../print/Log.hpp"
#include "../config/Config.hpp"
#include "EndianUtils.hpp"

#include <fstream>
#include <limits>

namespace resource
{
    FontData FontResource::loadFont(std::string_view path)
    {
        FontData fontData;

        if (path.empty())
        {
            vfLogError("Empty path provided for font loading");
            return {};
        }

        std::ifstream inFile(path.data(), std::ios::binary);
        if (!inFile)
        {
            vfLogError("Failed to open font file for reading: {}", path);
            return {};
        }

        inFile.seekg(0, std::ios::end);
        auto fileSize = inFile.tellg();
        inFile.seekg(0, std::ios::beg);

        constexpr std::streamoff maxFontFileSize = 50 * 1024 * 1024; // 50 MB
        if (fileSize > maxFontFileSize)
        {
            vfLogError("Font file too large ({} bytes, max {} bytes): {}",
                       static_cast<size_t>(fileSize), static_cast<size_t>(maxFontFileSize), path);
            return {};
        }

        using namespace endian;

        uint8_t headerFileType = readLE<uint8_t>(inFile);
        fontData.headerFileType = static_cast<FileType>(headerFileType);

        if (fontData.headerFileType != FileType::FONT)
        {
            vfLogError("Invalid font file type: expected FONT ({}), got {}",
                       static_cast<int>(FileType::FONT), static_cast<int>(fontData.headerFileType));
            return {};
        }

        uint32_t majorVersion = readLE<uint32_t>(inFile);
        uint32_t minorVersion = readLE<uint32_t>(inFile);
        uint32_t patchVersion = readLE<uint32_t>(inFile);

        fontData.version.major = majorVersion;
        fontData.version.minor = minorVersion;
        fontData.version.patch = patchVersion;

        if (majorVersion != Version::major || minorVersion != Version::minor || patchVersion != Version::patch)
        {
            vfLogError("Incompatible font file version: {}.{}.{}, expected {}.{}.{}. Re-import required.",
                       majorVersion, minorVersion, patchVersion,
                       Version::major, Version::minor, Version::patch);
            return {};
        }

        fontData.formatFlags = static_cast<FontFormatFlags>(readLE<uint32_t>(inFile));

        uint32_t nameLength = readLE<uint32_t>(inFile);
        if (nameLength > 0 && nameLength < 1024)
        {
            fontData.metadata.fontName.resize(nameLength);
            inFile.read(fontData.metadata.fontName.data(), nameLength);
        }

        uint32_t styleLength = readLE<uint32_t>(inFile);
        if (styleLength > 0 && styleLength < 1024)
        {
            fontData.metadata.fontStyle.resize(styleLength);
            inFile.read(fontData.metadata.fontStyle.data(), styleLength);
        }

        fontData.metadata.baseFontSize = readLE<uint32_t>(inFile);
        fontData.metadata.lineHeight = readLE<float>(inFile);
        fontData.metadata.ascender = readLE<float>(inFile);
        fontData.metadata.descender = readLE<float>(inFile);
        fontData.metadata.underlinePosition = readLE<float>(inFile);
        fontData.metadata.underlineThickness = readLE<float>(inFile);

        fontData.sdfParams.spread = readLE<float>(inFile);
        fontData.sdfParams.padding = readLE<uint32_t>(inFile);
        fontData.sdfParams.edgeValue = readLE<float>(inFile);
        fontData.sdfParams.reserved = readLE<uint32_t>(inFile);

        uint32_t rangeCount = readLE<uint32_t>(inFile);
        if (rangeCount > 1000)
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
            fontData.characterRanges.push_back(range);
        }

        uint32_t glyphCount = readLE<uint32_t>(inFile);
        if (glyphCount > 100000)
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
            fontData.glyphs.push_back(glyph);
        }

        uint32_t kerningCount = readLE<uint32_t>(inFile);
        if (kerningCount > 1000000)
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
            fontData.kerningPairs.push_back(pair);
        }

        fontData.atlas.width = readLE<uint32_t>(inFile);
        fontData.atlas.height = readLE<uint32_t>(inFile);
        fontData.atlas.format = static_cast<FontAtlasFormat>(readLE<uint32_t>(inFile));

        constexpr uint32_t maxAtlasDimension = 8192;
        if (fontData.atlas.width == 0 || fontData.atlas.height == 0 ||
            fontData.atlas.width > maxAtlasDimension || fontData.atlas.height > maxAtlasDimension)
        {
            vfLogError("Invalid atlas dimensions: {}x{}", fontData.atlas.width, fontData.atlas.height);
            return {};
        }

        uint32_t bytesPerPixel = 1;
        if (fontData.atlas.format == FontAtlasFormat::RGBA_32)
        {
            bytesPerPixel = 4;
        }
        else if (fontData.atlas.format != FontAtlasFormat::GRAYSCALE_8 &&
            fontData.atlas.format != FontAtlasFormat::SDF_8)
        {
            vfLogError("Unknown atlas format: {}", static_cast<uint32_t>(fontData.atlas.format));
            return {};
        }

        uint64_t expectedSize64 = static_cast<uint64_t>(fontData.atlas.width) *
            static_cast<uint64_t>(fontData.atlas.height) *
            static_cast<uint64_t>(bytesPerPixel);

        constexpr uint64_t maxAtlasBytes = 512ULL * 1024 * 1024;
        if (expectedSize64 > maxAtlasBytes || expectedSize64 > std::numeric_limits<size_t>::max())
        {
            vfLogError("Atlas size too large: {} bytes (max {} bytes)",
                       expectedSize64, maxAtlasBytes);
            return {};
        }

        size_t expectedSize = static_cast<size_t>(expectedSize64);
        uint32_t atlasDataSize = readLE<uint32_t>(inFile);

        if (atlasDataSize != expectedSize && atlasDataSize > 0)
        {
            vfLogWarning("Atlas data size mismatch: expected {}, got {}", expectedSize, atlasDataSize);
        }

        if (atlasDataSize > 0)
        {
            fontData.atlas.pixels.resize(atlasDataSize);
            inFile.read(reinterpret_cast<char*>(fontData.atlas.pixels.data()), atlasDataSize);
        }

        if (inFile.fail() && !inFile.eof())
        {
            vfLogError("Error reading font file: {}", path);
            return {};
        }

        inFile.close();

        vfLogInfo("Loaded font: {} ({} glyphs, {}x{} atlas)",
                  fontData.metadata.fontName, fontData.glyphs.size(),
                  fontData.atlas.width, fontData.atlas.height);

        return fontData;
    }
}
