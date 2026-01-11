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

        // Step 1: Load font file into memory (0% - 10%)
        std::vector<unsigned char> fontBuffer;
        if (!loadFontFile(file.path, fontBuffer))
        {
            vfLogError("Failed to load font file: {}", file.path);
            return;
        }

        if (progressCallback) progressCallback(0.1f);

        // Step 2: Initialize stb_truetype font info (10% - 15%)
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

        // Step 3: Calculate scale and extract metrics (15% - 20%)
        float scale = stbtt_ScaleForPixelHeight(&fontInfo, static_cast<float>(config.baseFontSize));

        resource::FontData fontData;
        fontData.headerFileType = resource::FileType::FONT;
        fontData.metadata.baseFontSize = config.baseFontSize;

        extractFontMetrics(&fontInfo, scale, fontData.metadata, fileName);

        // Set format flags
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

        // Step 4: Build character ranges (20% - 25%)
        fontData.characterRanges = buildCharacterRanges(config);

        if (progressCallback) progressCallback(0.25f);

        // Step 5: Generate atlas and extract glyph data (25% - 80%)
        bool atlasSuccess = generateSDFAtlas(&fontInfo, scale, fontData.characterRanges, config, fontData);

        if (!atlasSuccess)
        {
            vfLogError("Failed to generate font atlas for: {}", file.path);
            return;
        }

        if (progressCallback) progressCallback(0.8f);

        // Step 6: Extract kerning pairs (80% - 90%)
        if (config.includeKerning)
        {
            extractKerningPairs(&fontInfo, scale, fontData.glyphs, fontData.kerningPairs);
        }

        if (progressCallback) progressCallback(0.9f);

        // Step 7: Sort glyphs by codepoint for binary search
        std::sort(fontData.glyphs.begin(), fontData.glyphs.end(),
                  [](const resource::GlyphData& a, const resource::GlyphData& b) {
                      return a.codepoint < b.codepoint;
                  });

        // Step 8: Save to file (90% - 100%)
        saveToFile(location, fileName, fontData);

        if (progressCallback) progressCallback(1.0f);

        vfLogInfo("Successfully imported font: {} ({} glyphs, {}x{} atlas)",
                  fileName, fontData.glyphs.size(),
                  fontData.atlas.width, fontData.atlas.height);
    }

    bool Font::loadFontFile(std::string_view path, std::vector<unsigned char>& fontBuffer) const
    {
        std::ifstream file(std::string(path), std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            return false;
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        fontBuffer.resize(static_cast<size_t>(size));
        if (!file.read(reinterpret_cast<char*>(fontBuffer.data()), size))
        {
            return false;
        }

        return true;
    }

    void Font::extractFontMetrics(const void* fontInfoPtr, float scale,
                                  resource::FontMetadata& metadata,
                                  std::string_view fileName) const
    {
        const stbtt_fontinfo* fontInfo = static_cast<const stbtt_fontinfo*>(fontInfoPtr);

        int ascent, descent, lineGap;
        stbtt_GetFontVMetrics(fontInfo, &ascent, &descent, &lineGap);

        metadata.ascender = static_cast<float>(ascent) * scale;
        metadata.descender = static_cast<float>(descent) * scale;  // Usually negative
        metadata.lineHeight = (static_cast<float>(ascent - descent + lineGap)) * scale;

        // Estimate underline position (typically around descender / 2)
        metadata.underlinePosition = metadata.descender * 0.5f;
        metadata.underlineThickness = scale * 1.0f;  // 1 unit thickness at base size

        // Use filename as font name since parsing name table is complex
        metadata.fontName = std::string(fileName);
        metadata.fontStyle = "Regular";
    }

    std::vector<resource::CharacterRange> Font::buildCharacterRanges(
        const FontImportConfig& config) const
    {
        std::vector<resource::CharacterRange> ranges;

        if (config.includeBasicLatin)
        {
            ranges.push_back({0x0020, 0x007E});  // Basic Latin (ASCII printable)
        }

        if (config.includeLatin1Supplement)
        {
            ranges.push_back({0x00A0, 0x00FF});  // Latin-1 Supplement
        }

        if (config.includeLatinExtendedA)
        {
            ranges.push_back({0x0100, 0x017F});  // Latin Extended-A
        }

        if (config.includeLatinExtendedB)
        {
            ranges.push_back({0x0180, 0x024F});  // Latin Extended-B
        }

        if (config.includeGreek)
        {
            ranges.push_back({0x0370, 0x03FF});  // Greek and Coptic
        }

        if (config.includeCyrillic)
        {
            ranges.push_back({0x0400, 0x04FF});  // Cyrillic
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

        // Count total glyphs needed
        uint32_t totalGlyphs = countTotalGlyphs(ranges);

        // Prepare rectangle packing structures
        std::vector<stbrp_rect> rects;
        rects.reserve(totalGlyphs);

        // Temporary storage for glyph data
        struct GlyphTemp
        {
            uint32_t codepoint;
            int glyphIndex;
            int width, height;
            int xoff, yoff;
            int advanceWidth, leftSideBearing;
        };
        std::vector<GlyphTemp> glyphTemps;
        glyphTemps.reserve(totalGlyphs);

        // Padding for SDF
        int padding = static_cast<int>(config.sdfPadding);
        uint8_t onEdge = config.sdfOnEdgeValue;
        float pixelDistScale = static_cast<float>(onEdge) / config.sdfSpread;

        // Phase 1: Calculate glyph sizes for packing
        int rectId = 0;
        for (const auto& range : ranges)
        {
            for (uint32_t cp = range.rangeStart; cp <= range.rangeEnd; ++cp)
            {
                int glyphIndex = stbtt_FindGlyphIndex(fontInfo, static_cast<int>(cp));

                // Skip if glyph doesn't exist in font (but keep space characters)
                if (glyphIndex == 0 && cp != ' ')
                {
                    continue;
                }

                // Get glyph metrics
                int advanceWidth, leftSideBearing;
                stbtt_GetGlyphHMetrics(fontInfo, glyphIndex, &advanceWidth, &leftSideBearing);

                // Get SDF bitmap dimensions
                int width = 0, height = 0, xoff = 0, yoff = 0;

                if (glyphIndex != 0)
                {
                    unsigned char* sdfBitmap = stbtt_GetGlyphSDF(
                        fontInfo, scale, glyphIndex, padding, onEdge, pixelDistScale,
                        &width, &height, &xoff, &yoff);

                    if (sdfBitmap)
                    {
                        stbtt_FreeSDF(sdfBitmap, nullptr);
                    }
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
                glyphTemps.push_back(temp);

                // Add to rect packer (with atlas padding)
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
            return false;
        }

        // Phase 2: Pack rectangles into atlas
        int atlasWidth = static_cast<int>(config.atlasWidth);
        int atlasHeight = static_cast<int>(config.atlasHeight);

        std::vector<stbrp_node> nodes(atlasWidth);
        stbrp_context packContext;
        stbrp_init_target(&packContext, atlasWidth, atlasHeight, nodes.data(),
                          static_cast<int>(nodes.size()));

        stbrp_pack_rects(&packContext, rects.data(), static_cast<int>(rects.size()));

        // Calculate actual atlas size used
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

        // Round up to power of 2
        actualWidth = nextPowerOf2(actualWidth);
        actualHeight = nextPowerOf2(actualHeight);

        // Clamp to configured max
        actualWidth = std::min(actualWidth, config.atlasWidth);
        actualHeight = std::min(actualHeight, config.atlasHeight);

        // Ensure minimum size
        actualWidth = std::max(actualWidth, 64u);
        actualHeight = std::max(actualHeight, 64u);

        // Phase 3: Allocate atlas and render glyphs
        fontData.atlas.width = actualWidth;
        fontData.atlas.height = actualHeight;
        fontData.atlas.format = config.generateSDF ?
            resource::FontAtlasFormat::SDF_8 : resource::FontAtlasFormat::GRAYSCALE_8;
        fontData.atlas.pixels.resize(static_cast<size_t>(actualWidth) * actualHeight, 0);

        // Phase 4: Render each glyph into atlas
        fontData.glyphs.reserve(glyphTemps.size());

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

                // Render SDF glyph into atlas
                int width, height, xoff, yoff;
                unsigned char* sdfBitmap = stbtt_GetGlyphSDF(
                    fontInfo, scale, temp.glyphIndex, padding, onEdge, pixelDistScale,
                    &width, &height, &xoff, &yoff);

                if (sdfBitmap)
                {
                    // Copy to atlas
                    for (int y = 0; y < height; ++y)
                    {
                        for (int x = 0; x < width; ++x)
                        {
                            size_t atlasIdx = (static_cast<size_t>(rect.y) + y) * actualWidth
                                            + (static_cast<size_t>(rect.x) + x);
                            size_t srcIdx = static_cast<size_t>(y) * width + x;
                            fontData.atlas.pixels[atlasIdx] = sdfBitmap[srcIdx];
                        }
                    }
                    stbtt_FreeSDF(sdfBitmap, nullptr);
                }
            }
            else
            {
                // Empty glyph or didn't fit
                glyph.atlasX = 0;
                glyph.atlasY = 0;
                glyph.atlasWidth = 0;
                glyph.atlasHeight = 0;
            }

            fontData.glyphs.push_back(glyph);
        }

        return true;
    }

    void Font::extractKerningPairs(const void* fontInfoPtr, float scale,
                                   const std::vector<resource::GlyphData>& glyphs,
                                   std::vector<resource::KerningPair>& kerningPairs) const
    {
        const stbtt_fontinfo* fontInfo = static_cast<const stbtt_fontinfo*>(fontInfoPtr);

        // Get kerning table length
        int tableLength = stbtt_GetKerningTableLength(fontInfo);
        if (tableLength <= 0)
        {
            return;
        }

        // Get all kerning entries
        std::vector<stbtt_kerningentry> entries(tableLength);
        int actualLength = stbtt_GetKerningTable(fontInfo, entries.data(), tableLength);

        // Build a map of glyph indices to codepoints for our glyphs
        std::unordered_map<int, uint32_t> glyphIndexToCodepoint;
        for (const auto& glyph : glyphs)
        {
            int glyphIndex = stbtt_FindGlyphIndex(fontInfo, static_cast<int>(glyph.codepoint));
            if (glyphIndex != 0)
            {
                glyphIndexToCodepoint[glyphIndex] = glyph.codepoint;
            }
        }

        // Filter kerning pairs to only include glyphs we have
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

                if (std::abs(pair.kerningAmount) > 0.001f)  // Skip negligible kerning
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

        std::ofstream outFile(filePath, std::ios::binary);
        if (!outFile)
        {
            vfLogError("Failed to open file for writing: {}", filePath.string());
            return;
        }

        using namespace resource::endian;

        // Write header
        writeLE<uint8_t>(outFile, static_cast<uint8_t>(fontData.headerFileType));
        writeLE<uint32_t>(outFile, Version::major);
        writeLE<uint32_t>(outFile, Version::minor);
        writeLE<uint32_t>(outFile, Version::patch);
        writeLE<uint32_t>(outFile, static_cast<uint32_t>(fontData.formatFlags));

        // Write metadata
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

        // Write SDF parameters
        writeLE<float>(outFile, fontData.sdfParams.spread);
        writeLE<uint32_t>(outFile, fontData.sdfParams.padding);
        writeLE<float>(outFile, fontData.sdfParams.edgeValue);
        writeLE<uint32_t>(outFile, fontData.sdfParams.reserved);

        // Write character ranges
        writeLE<uint32_t>(outFile, static_cast<uint32_t>(fontData.characterRanges.size()));
        for (const auto& range : fontData.characterRanges)
        {
            writeLE<uint32_t>(outFile, range.rangeStart);
            writeLE<uint32_t>(outFile, range.rangeEnd);
        }

        // Write glyphs
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

        // Write kerning pairs
        writeLE<uint32_t>(outFile, static_cast<uint32_t>(fontData.kerningPairs.size()));
        for (const auto& pair : fontData.kerningPairs)
        {
            writeLE<uint32_t>(outFile, pair.leftCodepoint);
            writeLE<uint32_t>(outFile, pair.rightCodepoint);
            writeLE<float>(outFile, pair.kerningAmount);
        }

        // Write atlas
        writeLE<uint32_t>(outFile, fontData.atlas.width);
        writeLE<uint32_t>(outFile, fontData.atlas.height);
        writeLE<uint32_t>(outFile, static_cast<uint32_t>(fontData.atlas.format));

        auto dataSize = static_cast<uint32_t>(fontData.atlas.pixels.size());
        writeLE<uint32_t>(outFile, dataSize);
        outFile.write(reinterpret_cast<const char*>(fontData.atlas.pixels.data()), dataSize);

        outFile.close();
    }
}
