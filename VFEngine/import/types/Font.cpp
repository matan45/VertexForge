#include "Font.hpp"
#include "print/EditorLogger.hpp"
#include "../controllers/files/FileUtils.hpp"
#include "resource/EndianUtils.hpp"

#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <unordered_map>

// IMPORTANT: stb_rect_pack must be included BEFORE stb_truetype
// so that stb_truetype uses the external rect pack instead of its internal one
#define STB_RECT_PACK_IMPLEMENTATION
#include <stb_rect_pack.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

namespace types
{
    void Font::loadFromFile(const importConfig::ImportFiles& file, std::string_view fileName,
                            std::string_view location,
                            const FontImportConfig& config,
                            FontProgressCallback progressCallback) const
    {
        if (progressCallback) progressCallback(0.0f);

        std::vector<unsigned char> fontBuffer;
        if (!loadFontFile(file.path, fontBuffer))
        {
            vfLogError("Failed to load font file: {}", file.path);
            return;
        }

        if (progressCallback) progressCallback(0.1f);

        stbtt_fontinfo fontInfo;
        int fontOffset = stbtt_GetFontOffsetForIndex(fontBuffer.data(), 0);
        if (fontOffset < 0)
        {
            vfLogError("Invalid font file or font index: {}", file.path);
            return;
        }

        if (!stbtt_InitFont(&fontInfo, fontBuffer.data(), fontOffset))
        {
            vfLogError("Failed to initialize font: {}", file.path);
            return;
        }

        if (progressCallback) progressCallback(0.15f);

        float scale = stbtt_ScaleForPixelHeight(&fontInfo, static_cast<float>(config.baseFontSize));

        resource::FontData fontData;
        fontData.headerFileType = resource::FileType::FONT;
        fontData.metadata.baseFontSize = config.baseFontSize;

        extractFontMetrics(&fontInfo, scale, fontData.metadata, fileName);

        fontData.formatFlags = resource::FontFormatFlags::NONE;
        if (config.generateSDF)
        {
            fontData.formatFlags = fontData.formatFlags | resource::FontFormatFlags::SDF_ENABLED;
            fontData.sdfParams.spread = config.sdfSpread;
            fontData.sdfParams.padding = config.sdfPadding;
            fontData.sdfParams.edgeValue = static_cast<float>(config.sdfOnEdgeValue) / 255.0f;
        }
        if (config.includeKerning)
        {
            fontData.formatFlags = fontData.formatFlags | resource::FontFormatFlags::KERNING_ENABLED;
        }

        if (progressCallback) progressCallback(0.2f);

        fontData.characterRanges = buildCharacterRanges(config);

        if (progressCallback) progressCallback(0.25f);

        bool atlasSuccess = generateSDFAtlas(&fontInfo, scale, fontData.characterRanges, config, fontData);

        if (!atlasSuccess)
        {
            vfLogError("Failed to generate font atlas for: {}", file.path);
            return;
        }

        if (progressCallback) progressCallback(0.8f);

        if (config.includeKerning)
        {
            extractKerningPairs(&fontInfo, scale, fontData.glyphs, fontData.kerningPairs);
        }

        if (progressCallback) progressCallback(0.9f);

        std::sort(fontData.glyphs.begin(), fontData.glyphs.end(),
                  [](const resource::GlyphData& a, const resource::GlyphData& b) {
                      return a.codepoint < b.codepoint;
                  });

        saveToFile(location, fileName, fontData);

        if (progressCallback) progressCallback(1.0f);

        vfLogInfo("Successfully imported font: {} ({} glyphs, {}x{} atlas)",
                  fileName, fontData.glyphs.size(),
                  fontData.atlas.width, fontData.atlas.height);
    }

    bool Font::loadFontFile(std::string_view path, std::vector<unsigned char>& fontBuffer) const
    {
        try
        {
            std::ifstream file(std::string(path), std::ios::binary | std::ios::ate);
            if (!file.is_open())
            {
                vfLogError("Failed to open font file: {}", path);
                return false;
            }

            std::streampos pos = file.tellg();
            if (pos == std::streampos(-1))
            {
                vfLogError("Failed to get font file size: {}", path);
                return false;
            }

            auto size = static_cast<std::streamsize>(pos);
            if (size <= 0)
            {
                vfLogError("Font file is empty or invalid: {}", path);
                return false;
            }

            // Sanity check for file size (fonts shouldn't be gigabytes)
            constexpr std::streamsize maxFontSize = 100 * 1024 * 1024;  // 100 MB
            if (size > maxFontSize)
            {
                vfLogError("Font file too large ({} bytes, max {} bytes): {}",
                          size, maxFontSize, path);
                return false;
            }

            file.seekg(0, std::ios::beg);
            if (!file)
            {
                vfLogError("Failed to seek to beginning of font file: {}", path);
                return false;
            }

            fontBuffer.resize(static_cast<size_t>(size));
            if (!file.read(reinterpret_cast<char*>(fontBuffer.data()), size))
            {
                vfLogError("Failed to read font file data: {}", path);
                return false;
            }

            // Check for CFF-based OpenType fonts (not supported by stb_truetype)
            // The 'OTTO' signature at the start indicates CFF outlines
            if (fontBuffer.size() >= 4)
            {
                bool isCFF = (fontBuffer[0] == 'O' && fontBuffer[1] == 'T' &&
                             fontBuffer[2] == 'T' && fontBuffer[3] == 'O');
                if (isCFF)
                {
                    vfLogError("Font file uses CFF (PostScript) outlines which are not supported: {}", path);
                    vfLogError("Please convert this OTF font to TTF format using a font converter tool, "
                              "or use a TTF version of this font.");
                    return false;
                }
            }

            return true;
        }
        catch (const std::bad_alloc& e)
        {
            vfLogError("Failed to allocate memory for font file {}: {}", path, e.what());
            return false;
        }
        catch (const std::exception& e)
        {
            vfLogError("Exception while loading font file {}: {}", path, e.what());
            return false;
        }
    }

    void Font::extractFontMetrics(const void* fontInfoPtr, float scale,
                                  resource::FontMetadata& metadata,
                                  std::string_view fileName) const
    {
        const stbtt_fontinfo* fontInfo = static_cast<const stbtt_fontinfo*>(fontInfoPtr);

        int ascent, descent, lineGap;
        stbtt_GetFontVMetrics(fontInfo, &ascent, &descent, &lineGap);

        metadata.ascender = static_cast<float>(ascent) * scale;
        metadata.descender = static_cast<float>(descent) * scale;
        metadata.lineHeight = (static_cast<float>(ascent - descent + lineGap)) * scale;
        metadata.underlinePosition = metadata.descender * 0.5f;
        metadata.underlineThickness = scale * 1.0f;
        metadata.fontName = std::string(fileName);
        metadata.fontStyle = "Regular";
    }

    std::vector<resource::CharacterRange> Font::buildCharacterRanges(
        const FontImportConfig& config) const
    {
        std::vector<resource::CharacterRange> ranges;

        if (config.includeBasicLatin)
        {
            ranges.push_back({0x0020, 0x007E});
        }

        if (config.includeLatin1Supplement)
        {
            ranges.push_back({0x00A0, 0x00FF});
        }

        if (config.includeLatinExtendedA)
        {
            ranges.push_back({0x0100, 0x017F});
        }

        if (config.includeLatinExtendedB)
        {
            ranges.push_back({0x0180, 0x024F});
        }

        if (config.includeGreek)
        {
            ranges.push_back({0x0370, 0x03FF});
        }

        if (config.includeCyrillic)
        {
            ranges.push_back({0x0400, 0x04FF});
        }

        return ranges;
    }

    uint32_t Font::countTotalGlyphs(const std::vector<resource::CharacterRange>& ranges) const
    {
        uint32_t total = 0;
        for (const auto& range : ranges)
        {
            total += (range.rangeEnd - range.rangeStart + 1);
        }
        return total;
    }

    uint32_t Font::nextPowerOf2(uint32_t v)
    {
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        v++;
        return v;
    }

    bool Font::generateSDFAtlas(const void* fontInfoPtr, float scale,
                                const std::vector<resource::CharacterRange>& ranges,
                                const FontImportConfig& config,
                                resource::FontData& fontData) const
    {
        const stbtt_fontinfo* fontInfo = static_cast<const stbtt_fontinfo*>(fontInfoPtr);

        uint32_t totalGlyphs = countTotalGlyphs(ranges);

        std::vector<stbrp_rect> rects;
        rects.reserve(totalGlyphs);

        struct GlyphTemp
        {
            uint32_t codepoint;
            int glyphIndex;
            int width, height;
            int xoff, yoff;
            int advanceWidth, leftSideBearing;
            unsigned char* sdfBitmap = nullptr;
        };
        std::vector<GlyphTemp> glyphTemps;
        glyphTemps.reserve(totalGlyphs);

        // RAII cleanup for SDF bitmaps on any exit path
        auto cleanupBitmaps = [&glyphTemps]() {
            for (auto& temp : glyphTemps)
            {
                if (temp.sdfBitmap)
                {
                    stbtt_FreeSDF(temp.sdfBitmap, nullptr);
                    temp.sdfBitmap = nullptr;
                }
            }
        };

        int padding = static_cast<int>(config.sdfPadding);
        uint8_t onEdge = config.sdfOnEdgeValue;
        float pixelDistScale = static_cast<float>(onEdge) / config.sdfSpread;

        int rectId = 0;
        for (const auto& range : ranges)
        {
            for (uint32_t cp = range.rangeStart; cp <= range.rangeEnd; ++cp)
            {
                int glyphIndex = stbtt_FindGlyphIndex(fontInfo, static_cast<int>(cp));

                if (glyphIndex == 0 && cp != ' ')
                {
                    continue;
                }

                int advanceWidth, leftSideBearing;
                stbtt_GetGlyphHMetrics(fontInfo, glyphIndex, &advanceWidth, &leftSideBearing);

                int width = 0, height = 0, xoff = 0, yoff = 0;
                unsigned char* sdfBitmap = nullptr;

                if (glyphIndex != 0)
                {
                    sdfBitmap = stbtt_GetGlyphSDF(
                        fontInfo, scale, glyphIndex, padding, onEdge, pixelDistScale,
                        &width, &height, &xoff, &yoff);
                }

                GlyphTemp temp;
                temp.codepoint = cp;
                temp.glyphIndex = glyphIndex;
                temp.width = width;
                temp.height = height;
                temp.xoff = xoff;
                temp.yoff = yoff;
                temp.advanceWidth = advanceWidth;
                temp.leftSideBearing = leftSideBearing;
                temp.sdfBitmap = sdfBitmap;
                glyphTemps.push_back(temp);

                stbrp_rect rect;
                rect.id = rectId++;
                rect.w = static_cast<stbrp_coord>(width + config.atlasPadding);
                rect.h = static_cast<stbrp_coord>(height + config.atlasPadding);
                rect.x = 0;
                rect.y = 0;
                rect.was_packed = 0;
                rects.push_back(rect);
            }
        }

        if (rects.empty())
        {
            vfLogWarning("No valid glyphs found in font");
            cleanupBitmaps();
            return false;
        }

        int atlasWidth = static_cast<int>(config.atlasWidth);
        int atlasHeight = static_cast<int>(config.atlasHeight);

        std::vector<stbrp_node> nodes(atlasWidth);
        stbrp_context packContext;
        stbrp_init_target(&packContext, atlasWidth, atlasHeight, nodes.data(),
                          static_cast<int>(nodes.size()));

        stbrp_pack_rects(&packContext, rects.data(), static_cast<int>(rects.size()));

        uint32_t actualWidth = 0, actualHeight = 0;
        for (const auto& rect : rects)
        {
            if (rect.was_packed)
            {
                actualWidth = std::max(actualWidth,
                    static_cast<uint32_t>(rect.x + rect.w));
                actualHeight = std::max(actualHeight,
                    static_cast<uint32_t>(rect.y + rect.h));
            }
        }

        actualWidth = nextPowerOf2(actualWidth);
        actualHeight = nextPowerOf2(actualHeight);
        actualWidth = std::min(actualWidth, config.atlasWidth);
        actualHeight = std::min(actualHeight, config.atlasHeight);
        actualWidth = std::max(actualWidth, 64u);
        actualHeight = std::max(actualHeight, 64u);

        fontData.atlas.width = actualWidth;
        fontData.atlas.height = actualHeight;
        fontData.atlas.format = config.generateSDF ?
            resource::FontAtlasFormat::SDF_8 : resource::FontAtlasFormat::GRAYSCALE_8;
        fontData.atlas.pixels.resize(static_cast<size_t>(actualWidth) * actualHeight, 0);

        fontData.glyphs.reserve(glyphTemps.size());

        std::vector<uint32_t> unpackedGlyphs;
        uint32_t emptyGlyphCount = 0;
        uint32_t packedCount = 0;

        for (size_t i = 0; i < glyphTemps.size(); ++i)
        {
            const auto& temp = glyphTemps[i];
            const auto& rect = rects[i];

            resource::GlyphData glyph;
            glyph.codepoint = temp.codepoint;
            glyph.advanceX = static_cast<float>(temp.advanceWidth) * scale;
            glyph.advanceY = 0.0f;  // Horizontal text
            glyph.bearingX = static_cast<float>(temp.xoff);
            glyph.bearingY = static_cast<float>(-temp.yoff);  // Convert from top-left to baseline
            glyph.glyphWidth = static_cast<float>(temp.width);
            glyph.glyphHeight = static_cast<float>(temp.height);

            if (rect.was_packed && temp.width > 0 && temp.height > 0)
            {
                glyph.atlasX = static_cast<uint32_t>(rect.x);
                glyph.atlasY = static_cast<uint32_t>(rect.y);
                glyph.atlasWidth = static_cast<uint32_t>(temp.width);
                glyph.atlasHeight = static_cast<uint32_t>(temp.height);

                if (temp.sdfBitmap)
                {
                    // Bounds validation before copying to atlas
                    uint32_t destEndX = static_cast<uint32_t>(rect.x) + static_cast<uint32_t>(temp.width);
                    uint32_t destEndY = static_cast<uint32_t>(rect.y) + static_cast<uint32_t>(temp.height);

                    if (destEndX > actualWidth || destEndY > actualHeight)
                    {
                        vfLogError("Glyph U+{:04X} would overflow atlas bounds: dest ({},{}) to ({},{}) exceeds atlas {}x{}",
                                  temp.codepoint, rect.x, rect.y, destEndX, destEndY, actualWidth, actualHeight);
                        unpackedGlyphs.push_back(temp.codepoint);
                        glyph.atlasX = 0;
                        glyph.atlasY = 0;
                        glyph.atlasWidth = 0;
                        glyph.atlasHeight = 0;
                    }
                    else
                    {
                        size_t atlasSize = fontData.atlas.pixels.size();
                        for (int y = 0; y < temp.height; ++y)
                        {
                            for (int x = 0; x < temp.width; ++x)
                            {
                                size_t atlasIdx = (static_cast<size_t>(rect.y) + y) * actualWidth
                                                + (static_cast<size_t>(rect.x) + x);
                                size_t srcIdx = static_cast<size_t>(y) * temp.width + x;

                                if (atlasIdx < atlasSize)
                                {
                                    fontData.atlas.pixels[atlasIdx] = temp.sdfBitmap[srcIdx];
                                }
                            }
                        }
                    }
                }
                ++packedCount;
            }
            else
            {
                glyph.atlasX = 0;
                glyph.atlasY = 0;
                glyph.atlasWidth = 0;
                glyph.atlasHeight = 0;

                if (temp.width > 0 && temp.height > 0)
                {
                    unpackedGlyphs.push_back(temp.codepoint);
                }
                else
                {
                    ++emptyGlyphCount;
                }
            }

            fontData.glyphs.push_back(glyph);
        }

        if (!unpackedGlyphs.empty())
        {
            std::string failedChars;
            for (size_t i = 0; i < unpackedGlyphs.size() && i < 20; ++i)
            {
                uint32_t cp = unpackedGlyphs[i];
                if (cp >= 32 && cp < 127)
                {
                    failedChars += static_cast<char>(cp);
                }
                else
                {
                    failedChars += "U+" + std::to_string(cp);
                }
                if (i < unpackedGlyphs.size() - 1 && i < 19)
                {
                    failedChars += ", ";
                }
            }
            if (unpackedGlyphs.size() > 20)
            {
                failedChars += "... and " + std::to_string(unpackedGlyphs.size() - 20) + " more";
            }

            vfLogWarning("Font atlas too small: {} glyphs could not be packed. "
                        "Consider increasing atlas size (current: {}x{}). Failed characters: {}",
                        unpackedGlyphs.size(), actualWidth, actualHeight, failedChars);
        }

        vfLogInfo("Font atlas generated: {} glyphs packed, {} empty glyphs, {} failed to pack. "
                 "Atlas size: {}x{}",
                 packedCount, emptyGlyphCount, unpackedGlyphs.size(), actualWidth, actualHeight);

        cleanupBitmaps();

        return true;
    }

    void Font::extractKerningPairs(const void* fontInfoPtr, float scale,
                                   const std::vector<resource::GlyphData>& glyphs,
                                   std::vector<resource::KerningPair>& kerningPairs) const
    {
        const stbtt_fontinfo* fontInfo = static_cast<const stbtt_fontinfo*>(fontInfoPtr);

        int tableLength = stbtt_GetKerningTableLength(fontInfo);
        if (tableLength <= 0)
        {
            return;
        }

        std::vector<stbtt_kerningentry> entries(tableLength);
        int actualLength = stbtt_GetKerningTable(fontInfo, entries.data(), tableLength);

        std::unordered_map<int, uint32_t> glyphIndexToCodepoint;
        for (const auto& glyph : glyphs)
        {
            int glyphIndex = stbtt_FindGlyphIndex(fontInfo, static_cast<int>(glyph.codepoint));
            if (glyphIndex != 0)
            {
                glyphIndexToCodepoint[glyphIndex] = glyph.codepoint;
            }
        }

        for (int i = 0; i < actualLength; ++i)
        {
            const auto& entry = entries[i];

            auto it1 = glyphIndexToCodepoint.find(entry.glyph1);
            auto it2 = glyphIndexToCodepoint.find(entry.glyph2);

            if (it1 != glyphIndexToCodepoint.end() && it2 != glyphIndexToCodepoint.end())
            {
                resource::KerningPair pair;
                pair.leftCodepoint = it1->second;
                pair.rightCodepoint = it2->second;
                pair.kerningAmount = static_cast<float>(entry.advance) * scale;

                if (std::abs(pair.kerningAmount) > 0.001f)
                {
                    kerningPairs.push_back(pair);
                }
            }
        }
    }

    void Font::saveToFile(std::string_view location, std::string_view fileName,
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

            using namespace resource::endian;

            writeLE<uint8_t>(outFile, static_cast<uint8_t>(fontData.headerFileType));
            writeLE<uint32_t>(outFile, Version::major);
            writeLE<uint32_t>(outFile, Version::minor);
            writeLE<uint32_t>(outFile, Version::patch);
            writeLE<uint32_t>(outFile, static_cast<uint32_t>(fontData.formatFlags));

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
                writeLE<uint32_t>(outFile, glyph.reserved);
            }

            writeLE<uint32_t>(outFile, static_cast<uint32_t>(fontData.kerningPairs.size()));
            for (const auto& pair : fontData.kerningPairs)
            {
                writeLE<uint32_t>(outFile, pair.leftCodepoint);
                writeLE<uint32_t>(outFile, pair.rightCodepoint);
                writeLE<float>(outFile, pair.kerningAmount);
            }

            writeLE<uint32_t>(outFile, fontData.atlas.width);
            writeLE<uint32_t>(outFile, fontData.atlas.height);
            writeLE<uint32_t>(outFile, static_cast<uint32_t>(fontData.atlas.format));

            auto dataSize = static_cast<uint32_t>(fontData.atlas.pixels.size());
            writeLE<uint32_t>(outFile, dataSize);
            outFile.write(reinterpret_cast<const char*>(fontData.atlas.pixels.data()), dataSize);

            outFile.flush();
            outFile.close();

            if (!outFile)
            {
                vfLogError("Failed to flush/close font file: {}", tempPath.string());
                std::filesystem::remove(tempPath);
                return;
            }

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
                return;
            }
        }
        catch (const std::ios_base::failure& e)
        {
            vfLogError("I/O error while writing font file {}: {}", tempPath.string(), e.what());
            std::error_code ec;
            std::filesystem::remove(tempPath, ec);
            return;
        }
        catch (const std::exception& e)
        {
            vfLogError("Exception while writing font file {}: {}", tempPath.string(), e.what());
            std::error_code ec;
            std::filesystem::remove(tempPath, ec);
            return;
        }
    }
}
