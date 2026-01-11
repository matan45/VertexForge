#include "FontResource.hpp"
#include "../print/EditorLogger.hpp"
#include "../config/Config.hpp"
#include "EndianUtils.hpp"

#include <fstream>
#include <limits>

namespace resource
{
    FontData FontResource::loadFont(std::string_view path)
    {
        FontData fontData;

        // Validate input
        if (path.empty())
        {
            vfLogError("Empty path provided for font loading");
            return {};
        }

        // Open the file in binary mode
        std::ifstream inFile(path.data(), std::ios::binary);
        if (!inFile)
        {
            vfLogError("Failed to open font file for reading: {}", path);
            return {};
        }

        // Check file size for sanity
        inFile.seekg(0, std::ios::end);
        auto fileSize = inFile.tellg();
        inFile.seekg(0, std::ios::beg);

        constexpr std::streamoff maxFontFileSize = 50 * 1024 * 1024;  // 50 MB
        if (fileSize > maxFontFileSize)
        {
            vfLogError("Font file too large ({} bytes, max {} bytes): {}",
                      static_cast<size_t>(fileSize), static_cast<size_t>(maxFontFileSize), path);
            return {};
        }

        using namespace endian;

        // Read header
        uint8_t headerFileType = readLE<uint8_t>(inFile);
        fontData.headerFileType = static_cast<FileType>(headerFileType);

        if (fontData.headerFileType != FileType::FONT)
        {
            vfLogError("Invalid font file type: expected FONT ({}), got {}",
                      static_cast<int>(FileType::FONT), static_cast<int>(fontData.headerFileType));
            return {};
        }

        // Read version
        uint32_t majorVersion = readLE<uint32_t>(inFile);
        uint32_t minorVersion = readLE<uint32_t>(inFile);
        uint32_t patchVersion = readLE<uint32_t>(inFile);

        fontData.version.major = majorVersion;
        fontData.version.minor = minorVersion;
        fontData.version.patch = patchVersion;

        // Version compatibility check
        if (majorVersion != Version::major)
        {
            vfLogError("Incompatible font file major version: {}.{}.{}, expected {}.x.x",
                      majorVersion, minorVersion, patchVersion, Version::major);
            return {};
        }

        // Read format flags
        fontData.formatFlags = static_cast<FontFormatFlags>(readLE<uint32_t>(inFile));

        // Read metadata - font name
        uint32_t nameLength = readLE<uint32_t>(inFile);
        if (nameLength > 0 && nameLength < 1024)  // Sanity check
        {
            fontData.metadata.fontName.resize(nameLength);
            inFile.read(fontData.metadata.fontName.data(), nameLength);
        }

        // Read metadata - font style
        uint32_t styleLength = readLE<uint32_t>(inFile);
        if (styleLength > 0 && styleLength < 1024)  // Sanity check
        {
            fontData.metadata.fontStyle.resize(styleLength);
            inFile.read(fontData.metadata.fontStyle.data(), styleLength);
        }

        // Read metadata - numeric values
        fontData.metadata.baseFontSize = readLE<uint32_t>(inFile);
        fontData.metadata.lineHeight = readLE<float>(inFile);
        fontData.metadata.ascender = readLE<float>(inFile);
        fontData.metadata.descender = readLE<float>(inFile);
        fontData.metadata.underlinePosition = readLE<float>(inFile);
        fontData.metadata.underlineThickness = readLE<float>(inFile);

        // Read SDF parameters
        fontData.sdfParams.spread = readLE<float>(inFile);
        fontData.sdfParams.padding = readLE<uint32_t>(inFile);
        fontData.sdfParams.edgeValue = readLE<float>(inFile);
        fontData.sdfParams.reserved = readLE<uint32_t>(inFile);

        // Read character ranges
        uint32_t rangeCount = readLE<uint32_t>(inFile);
        if (rangeCount > 1000)  // Sanity check
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

        // Read glyphs
        uint32_t glyphCount = readLE<uint32_t>(inFile);
        if (glyphCount > 100000)  // Sanity check
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
            glyph.reserved = readLE<uint32_t>(inFile);
            fontData.glyphs.push_back(glyph);
        }

        // Read kerning pairs
        uint32_t kerningCount = readLE<uint32_t>(inFile);
        if (kerningCount > 1000000)  // Sanity check
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

        // Read atlas
        fontData.atlas.width = readLE<uint32_t>(inFile);
        fontData.atlas.height = readLE<uint32_t>(inFile);
        fontData.atlas.format = static_cast<FontAtlasFormat>(readLE<uint32_t>(inFile));

        // Validate atlas dimensions
        if (fontData.atlas.width == 0 || fontData.atlas.height == 0 ||
            fontData.atlas.width > 8192 || fontData.atlas.height > 8192)
        {
            vfLogError("Invalid atlas dimensions: {}x{}", fontData.atlas.width, fontData.atlas.height);
            return {};
        }

        uint32_t atlasDataSize = readLE<uint32_t>(inFile);

        // Validate atlas data size (use size_t to prevent overflow)
        size_t expectedSize = static_cast<size_t>(fontData.atlas.width) * fontData.atlas.height;
        if (fontData.atlas.format == FontAtlasFormat::RGBA_32)
        {
            expectedSize *= 4;
        }

        if (expectedSize > std::numeric_limits<uint32_t>::max())
        {
            vfLogError("Atlas size too large: {}", expectedSize);
            return {};
        }

        if (atlasDataSize != expectedSize && atlasDataSize > 0)
        {
            vfLogWarning("Atlas data size mismatch: expected {}, got {}", expectedSize, atlasDataSize);
        }

        if (atlasDataSize > 0)
        {
            fontData.atlas.pixels.resize(atlasDataSize);
            inFile.read(reinterpret_cast<char*>(fontData.atlas.pixels.data()), atlasDataSize);
        }

        // Verify read was successful
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
