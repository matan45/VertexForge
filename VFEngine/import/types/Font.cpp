#include "Font.hpp"
#include "SDFGenerator.hpp"
#include "print/EditorLogger.hpp"
#include "../controllers/files/FileUtils.hpp"
#include "resource/EndianUtils.hpp"

#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

// stb_rect_pack is still used for atlas bin-packing
#define STB_RECT_PACK_IMPLEMENTATION
#include <stb_rect_pack.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_COLOR_H

namespace types
{
    bool Font::loadFromFile(const importConfig::ImportFiles& file, std::string_view fileName,
                            std::string_view location,
                            const FontImportConfig& config,
                            FontProgressCallback progressCallback) const
    {
        if (progressCallback) progressCallback(0.0f);

        std::vector<unsigned char> fontBuffer;
        if (!loadFontFile(file.path, fontBuffer))
        {
            vfLogError("Failed to load font file: {}", file.path);
            return false;
        }

        if (progressCallback) progressCallback(0.1f);

        FT_Library library = nullptr;
        FT_Face face = nullptr;

        if (!initFreeType(library, face, fontBuffer, file.path))
        {
            return false;
        }

        // RAII cleanup for FreeType resources
        auto cleanupFT = [&]()
        {
            if (face) FT_Done_Face(face);
            if (library) FT_Done_FreeType(library);
        };

        if (progressCallback) progressCallback(0.15f);

        bool colorFont = isColorFont(face);

        // For bitmap-only fonts, select a strike; for outline fonts, set pixel size
        if (colorFont && face->num_fixed_sizes > 0)
        {
            int bestStrike = 0;
            int bestDiff = std::abs(static_cast<int>(face->available_sizes[0].height)
                                  - static_cast<int>(config.baseFontSize));
            for (int i = 1; i < face->num_fixed_sizes; ++i)
            {
                int diff = std::abs(static_cast<int>(face->available_sizes[i].height)
                                  - static_cast<int>(config.baseFontSize));
                if (diff < bestDiff)
                {
                    bestDiff = diff;
                    bestStrike = i;
                }
            }
            FT_Select_Size(face, bestStrike);
        }
        else
        {
            FT_Set_Pixel_Sizes(face, 0, config.baseFontSize);
        }

        resource::FontData fontData;
        fontData.headerFileType = resource::FileType::FONT;
        fontData.metadata.baseFontSize = config.baseFontSize;

        extractFontMetrics(face, config.baseFontSize, fontData.metadata, fileName);

        fontData.formatFlags = resource::FontFormatFlags::NONE;

        if (colorFont)
        {
            fontData.formatFlags = fontData.formatFlags | resource::FontFormatFlags::COLOR_EMOJI;
        }
        else if (config.generateSDF)
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

        // For color emoji fonts, automatically include emoji ranges
        FontImportConfig effectiveConfig = config;
        if (colorFont)
        {
            effectiveConfig.includeEmoji = true;
            effectiveConfig.includeMiscSymbols = true;
            effectiveConfig.includeDingbats = true;
        }

        fontData.characterRanges = buildCharacterRanges(effectiveConfig);

        if (progressCallback) progressCallback(0.25f);

        bool atlasSuccess = false;
        if (colorFont)
        {
            atlasSuccess = generateColorAtlas(face, config.baseFontSize,
                                              fontData.characterRanges, config, fontData);
        }
        else
        {
            atlasSuccess = generateSDFAtlas(face, config.baseFontSize,
                                            fontData.characterRanges, config, fontData);
        }

        if (!atlasSuccess)
        {
            vfLogError("Failed to generate font atlas for: {}", file.path);
            cleanupFT();
            return false;
        }

        if (progressCallback) progressCallback(0.8f);

        if (config.includeKerning)
        {
            float scale = 1.0f; // FreeType metrics are already in pixel units at the set size
            extractKerningPairs(face, scale, fontData.glyphs, fontData.kerningPairs);
        }

        if (progressCallback) progressCallback(0.9f);

        std::sort(fontData.glyphs.begin(), fontData.glyphs.end(),
                  [](const resource::GlyphData& a, const resource::GlyphData& b) {
                      return a.codepoint < b.codepoint;
                  });

        saveToFile(location, fileName, fontData);

        cleanupFT();

        if (progressCallback) progressCallback(1.0f);

        vfLogInfo("Successfully imported font: {} ({} glyphs, {}x{} atlas, {})",
                  fileName, fontData.glyphs.size(),
                  fontData.atlas.width, fontData.atlas.height,
                  colorFont ? "color" : "SDF");

        return true;
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

    bool Font::initFreeType(FT_Library& library, FT_Face& face,
                            const std::vector<unsigned char>& buffer,
                            std::string_view path) const
    {
        FT_Error error = FT_Init_FreeType(&library);
        if (error)
        {
            vfLogError("Failed to initialize FreeType library (error {})", error);
            return false;
        }

        error = FT_New_Memory_Face(library, buffer.data(),
                                   static_cast<FT_Long>(buffer.size()), 0, &face);
        if (error)
        {
            vfLogError("FreeType failed to load font face from: {} (error {})", path, error);
            FT_Done_FreeType(library);
            library = nullptr;
            return false;
        }

        // Explicitly select Unicode charmap for full supplementary plane support
        error = FT_Select_Charmap(face, FT_ENCODING_UNICODE);
        if (error)
        {
            vfLogWarning("FreeType: no Unicode charmap found in font: {} (error {})", path, error);
        }

        return true;
    }

    void Font::extractFontMetrics(FT_Face face, uint32_t fontSize,
                                  resource::FontMetadata& metadata,
                                  std::string_view fileName) const
    {
        // FreeType metrics are in 26.6 fixed-point format (divide by 64)
        FT_Size_Metrics metrics = face->size->metrics;

        metadata.ascender = static_cast<float>(metrics.ascender) / 64.0f;
        metadata.descender = static_cast<float>(metrics.descender) / 64.0f;
        metadata.lineHeight = static_cast<float>(metrics.height) / 64.0f;
        metadata.underlinePosition = metadata.descender * 0.5f;
        metadata.underlineThickness = static_cast<float>(fontSize) / 32.0f;

        metadata.fontName = face->family_name ? face->family_name : std::string(fileName);
        metadata.fontStyle = face->style_name ? face->style_name : "Regular";
    }

    bool Font::isColorFont(FT_Face face) const
    {
        return FT_HAS_COLOR(face) != 0;
    }

    std::vector<resource::CharacterRange> Font::buildCharacterRanges(
        const FontImportConfig& config) const
    {
        std::vector<resource::CharacterRange> ranges;

        if (config.includeBasicLatin)
            ranges.push_back({0x0020, 0x007E});

        if (config.includeLatin1Supplement)
            ranges.push_back({0x00A0, 0x00FF});

        if (config.includeLatinExtendedA)
            ranges.push_back({0x0100, 0x017F});

        if (config.includeLatinExtendedB)
            ranges.push_back({0x0180, 0x024F});

        if (config.includeGreek)
            ranges.push_back({0x0370, 0x03FF});

        if (config.includeCyrillic)
            ranges.push_back({0x0400, 0x04FF});

        // Emoji ranges
        if (config.includeEmoji)
        {
            ranges.push_back({0x1F600, 0x1F64F}); // Emoticons
            ranges.push_back({0x1F300, 0x1F5FF}); // Misc Symbols and Pictographs
            ranges.push_back({0x1F900, 0x1F9FF}); // Supplemental Symbols and Pictographs
            ranges.push_back({0x1FA70, 0x1FAFF}); // Symbols and Pictographs Extended-A
        }

        if (config.includeMiscSymbols)
            ranges.push_back({0x2600, 0x26FF});

        if (config.includeDingbats)
            ranges.push_back({0x2700, 0x27BF});

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

    bool Font::generateSDFAtlas(FT_Face face, uint32_t fontSize,
                                const std::vector<resource::CharacterRange>& ranges,
                                const FontImportConfig& config,
                                resource::FontData& fontData) const
    {
        uint32_t totalGlyphs = countTotalGlyphs(ranges);

        // Render at 4x resolution for better SDF quality
        uint32_t hiresSize = fontSize * 4;
        int padding = static_cast<int>(config.sdfPadding);

        struct GlyphTemp
        {
            uint32_t codepoint;
            FT_UInt glyphIndex;
            int targetWidth, targetHeight;
            float bearingX, bearingY;
            float advanceX;
            SDFResult sdfResult;
        };
        std::vector<GlyphTemp> glyphTemps;
        glyphTemps.reserve(totalGlyphs);

        std::vector<stbrp_rect> rects;
        rects.reserve(totalGlyphs);

        int rectId = 0;
        for (const auto& range : ranges)
        {
            for (uint32_t cp = range.rangeStart; cp <= range.rangeEnd; ++cp)
            {
                FT_UInt glyphIndex = FT_Get_Char_Index(face, cp);
                if (glyphIndex == 0 && cp != ' ')
                    continue;

                // Get metrics at base size
                FT_Set_Pixel_Sizes(face, 0, fontSize);
                if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT))
                    continue;

                float advanceX = static_cast<float>(face->glyph->advance.x) / 64.0f;

                // Render bitmap at high resolution for SDF generation
                FT_Set_Pixel_Sizes(face, 0, hiresSize);
                if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_RENDER))
                {
                    FT_Set_Pixel_Sizes(face, 0, fontSize);
                    continue;
                }

                FT_Bitmap* bitmap = &face->glyph->bitmap;

                // Calculate target (base-size) dimensions including SDF padding
                int targetWidth = 0;
                int targetHeight = 0;
                float bearingX = 0.0f;
                float bearingY = 0.0f;

                if (bitmap->width > 0 && bitmap->rows > 0)
                {
                    // Scale hires bitmap metrics back to base size
                    float scaleDown = static_cast<float>(fontSize) / static_cast<float>(hiresSize);
                    targetWidth = static_cast<int>(std::ceil(bitmap->width * scaleDown)) + padding * 2;
                    targetHeight = static_cast<int>(std::ceil(bitmap->rows * scaleDown)) + padding * 2;

                    bearingX = static_cast<float>(face->glyph->bitmap_left) * scaleDown
                               - static_cast<float>(padding);
                    bearingY = static_cast<float>(face->glyph->bitmap_top) * scaleDown
                               + static_cast<float>(padding);

                    // Generate SDF from hires bitmap
                    SDFResult sdf = SDFGenerator::generateFromBitmap(
                        bitmap->buffer,
                        static_cast<int>(bitmap->width), static_cast<int>(bitmap->rows),
                        targetWidth, targetHeight,
                        config.sdfSpread, config.sdfOnEdgeValue);

                    GlyphTemp temp;
                    temp.codepoint = cp;
                    temp.glyphIndex = glyphIndex;
                    temp.targetWidth = sdf.width;
                    temp.targetHeight = sdf.height;
                    temp.bearingX = bearingX;
                    temp.bearingY = bearingY;
                    temp.advanceX = advanceX;
                    temp.sdfResult = std::move(sdf);
                    glyphTemps.push_back(std::move(temp));
                }
                else
                {
                    // Whitespace character (e.g., space)
                    GlyphTemp temp;
                    temp.codepoint = cp;
                    temp.glyphIndex = glyphIndex;
                    temp.targetWidth = 0;
                    temp.targetHeight = 0;
                    temp.bearingX = 0.0f;
                    temp.bearingY = 0.0f;
                    temp.advanceX = advanceX;
                    glyphTemps.push_back(std::move(temp));
                }

                stbrp_rect rect;
                rect.id = rectId++;
                rect.w = static_cast<stbrp_coord>(glyphTemps.back().targetWidth + config.atlasPadding);
                rect.h = static_cast<stbrp_coord>(glyphTemps.back().targetHeight + config.atlasPadding);
                rect.x = 0;
                rect.y = 0;
                rect.was_packed = 0;
                rects.push_back(rect);
            }
        }

        // Reset face size back to base
        FT_Set_Pixel_Sizes(face, 0, fontSize);

        if (rects.empty())
        {
            vfLogWarning("No valid glyphs found in font");
            return false;
        }

        // Pack glyphs into atlas
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
                actualWidth = std::max(actualWidth, static_cast<uint32_t>(rect.x + rect.w));
                actualHeight = std::max(actualHeight, static_cast<uint32_t>(rect.y + rect.h));
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
            glyph.advanceX = temp.advanceX;
            glyph.advanceY = 0.0f;
            glyph.bearingX = temp.bearingX;
            glyph.bearingY = temp.bearingY;
            glyph.glyphWidth = static_cast<float>(temp.targetWidth);
            glyph.glyphHeight = static_cast<float>(temp.targetHeight);

            if (rect.was_packed && temp.targetWidth > 0 && temp.targetHeight > 0)
            {
                glyph.atlasX = static_cast<uint32_t>(rect.x);
                glyph.atlasY = static_cast<uint32_t>(rect.y);
                glyph.atlasWidth = static_cast<uint32_t>(temp.targetWidth);
                glyph.atlasHeight = static_cast<uint32_t>(temp.targetHeight);

                if (!temp.sdfResult.pixels.empty())
                {
                    uint32_t destEndX = glyph.atlasX + glyph.atlasWidth;
                    uint32_t destEndY = glyph.atlasY + glyph.atlasHeight;

                    if (destEndX > actualWidth || destEndY > actualHeight)
                    {
                        vfLogError("Glyph U+{:04X} would overflow atlas bounds", temp.codepoint);
                        glyph.atlasX = glyph.atlasY = glyph.atlasWidth = glyph.atlasHeight = 0;
                        unpackedGlyphs.push_back(temp.codepoint);
                    }
                    else
                    {
                        size_t atlasSize = fontData.atlas.pixels.size();
                        for (int y = 0; y < temp.targetHeight; ++y)
                        {
                            for (int x = 0; x < temp.targetWidth; ++x)
                            {
                                size_t atlasIdx = (static_cast<size_t>(rect.y) + y) * actualWidth
                                                + (static_cast<size_t>(rect.x) + x);
                                size_t srcIdx = static_cast<size_t>(y) * temp.targetWidth + x;

                                if (atlasIdx < atlasSize && srcIdx < temp.sdfResult.pixels.size())
                                {
                                    fontData.atlas.pixels[atlasIdx] = temp.sdfResult.pixels[srcIdx];
                                }
                            }
                        }
                    }
                }
                ++packedCount;
            }
            else
            {
                glyph.atlasX = glyph.atlasY = glyph.atlasWidth = glyph.atlasHeight = 0;

                if (temp.targetWidth > 0 && temp.targetHeight > 0)
                    unpackedGlyphs.push_back(temp.codepoint);
                else
                    ++emptyGlyphCount;
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
                    failedChars += static_cast<char>(cp);
                else
                    failedChars += "U+" + std::to_string(cp);
                if (i < unpackedGlyphs.size() - 1 && i < 19)
                    failedChars += ", ";
            }
            if (unpackedGlyphs.size() > 20)
                failedChars += "... and " + std::to_string(unpackedGlyphs.size() - 20) + " more";

            vfLogWarning("Font atlas too small: {} glyphs could not be packed. "
                        "Consider increasing atlas size (current: {}x{}). Failed characters: {}",
                        unpackedGlyphs.size(), actualWidth, actualHeight, failedChars);
        }

        vfLogInfo("Font atlas generated: {} glyphs packed, {} empty glyphs, {} failed to pack. "
                 "Atlas size: {}x{}",
                 packedCount, emptyGlyphCount, unpackedGlyphs.size(), actualWidth, actualHeight);

        return true;
    }

    bool Font::generateColorAtlas(FT_Face face, uint32_t fontSize,
                                  const std::vector<resource::CharacterRange>& ranges,
                                  const FontImportConfig& config,
                                  resource::FontData& fontData) const
    {
        uint32_t totalGlyphs = countTotalGlyphs(ranges);

        // For bitmap-only fonts (CBDT/CBLC like Noto Color Emoji),
        // we must select a bitmap strike instead of using FT_Set_Pixel_Sizes
        if (face->num_fixed_sizes > 0)
        {
            // Find the strike closest to the requested fontSize
            int bestStrike = 0;
            int bestDiff = std::abs(static_cast<int>(face->available_sizes[0].height) - static_cast<int>(fontSize));
            for (int i = 1; i < face->num_fixed_sizes; ++i)
            {
                int diff = std::abs(static_cast<int>(face->available_sizes[i].height) - static_cast<int>(fontSize));
                if (diff < bestDiff)
                {
                    bestDiff = diff;
                    bestStrike = i;
                }
            }
            FT_Select_Size(face, bestStrike);
        }
        else
        {
            FT_Set_Pixel_Sizes(face, 0, fontSize);
        }

        // Select the first color palette for COLR rendering
        // Without this, COLRv1 glyphs may produce empty bitmaps
        FT_Palette_Data paletteData;
        if (FT_Palette_Data_Get(face, &paletteData) == 0 && paletteData.num_palettes > 0)
        {
            FT_Color* palette = nullptr;
            FT_Palette_Select(face, 0, &palette);
        }

        struct GlyphTemp
        {
            uint32_t codepoint;
            int width, height;
            float bearingX, bearingY;
            float advanceX;
            std::vector<unsigned char> rgbaPixels; // RGBA
        };
        std::vector<GlyphTemp> glyphTemps;
        glyphTemps.reserve(totalGlyphs);

        std::vector<stbrp_rect> rects;
        rects.reserve(totalGlyphs);

        int rectId = 0;
        for (const auto& range : ranges)
        {
            for (uint32_t cp = range.rangeStart; cp <= range.rangeEnd; ++cp)
            {
                FT_UInt glyphIndex = FT_Get_Char_Index(face, cp);
                if (glyphIndex == 0 && cp != ' ')
                    continue;

                // Skip SVG table (requires external renderer) and use COLR/CPAL fallback
                FT_Int32 loadFlags = FT_LOAD_COLOR | FT_LOAD_RENDER | FT_LOAD_NO_SVG;
                if (FT_Load_Glyph(face, glyphIndex, loadFlags))
                    continue;

                FT_Bitmap* bitmap = &face->glyph->bitmap;

                // COLRv1 glyphs may load successfully but produce empty bitmaps
                // if FreeType can't fully render the paint operations.
                // Fallback: render as grayscale outline.
                if (bitmap->width == 0 || bitmap->rows == 0)
                {
                    if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_RENDER | FT_LOAD_NO_SVG))
                        continue;
                    bitmap = &face->glyph->bitmap;
                    if (bitmap->width == 0 || bitmap->rows == 0)
                        continue;
                }

                float advanceX = static_cast<float>(face->glyph->advance.x) / 64.0f;

                GlyphTemp temp;
                temp.codepoint = cp;
                temp.width = static_cast<int>(bitmap->width);
                temp.height = static_cast<int>(bitmap->rows);
                temp.bearingX = static_cast<float>(face->glyph->bitmap_left);
                temp.bearingY = static_cast<float>(face->glyph->bitmap_top);
                temp.advanceX = advanceX;

                if (bitmap->buffer)
                {
                    size_t pixelCount = static_cast<size_t>(bitmap->width) * bitmap->rows;
                    temp.rgbaPixels.resize(pixelCount * 4);

                    if (bitmap->pixel_mode == FT_PIXEL_MODE_BGRA)
                    {
                        // Convert BGRA -> RGBA
                        for (size_t p = 0; p < pixelCount; ++p)
                        {
                            size_t off = p * 4;
                            temp.rgbaPixels[off + 0] = bitmap->buffer[off + 2]; // R
                            temp.rgbaPixels[off + 1] = bitmap->buffer[off + 1]; // G
                            temp.rgbaPixels[off + 2] = bitmap->buffer[off + 0]; // B
                            temp.rgbaPixels[off + 3] = bitmap->buffer[off + 3]; // A
                        }
                    }
                    else if (bitmap->pixel_mode == FT_PIXEL_MODE_GRAY)
                    {
                        // Grayscale fallback: white glyph with alpha from gray value
                        for (size_t p = 0; p < pixelCount; ++p)
                        {
                            temp.rgbaPixels[p * 4 + 0] = 255;
                            temp.rgbaPixels[p * 4 + 1] = 255;
                            temp.rgbaPixels[p * 4 + 2] = 255;
                            temp.rgbaPixels[p * 4 + 3] = bitmap->buffer[p];
                        }
                    }
                }

                if (temp.rgbaPixels.empty())
                    continue;

                glyphTemps.push_back(std::move(temp));

                stbrp_rect rect;
                rect.id = rectId++;
                rect.w = static_cast<stbrp_coord>(glyphTemps.back().width + config.atlasPadding);
                rect.h = static_cast<stbrp_coord>(glyphTemps.back().height + config.atlasPadding);
                rect.x = 0;
                rect.y = 0;
                rect.was_packed = 0;
                rects.push_back(rect);
            }
        }

        if (rects.empty())
        {
            vfLogWarning("No valid glyphs found in color font");
            return false;
        }

        // Use larger atlas for color emoji (bitmaps are bigger)
        uint32_t atlasW = std::max(config.atlasWidth, 2048u);
        uint32_t atlasH = std::max(config.atlasHeight, 2048u);

        int iAtlasW = static_cast<int>(atlasW);
        int iAtlasH = static_cast<int>(atlasH);

        std::vector<stbrp_node> nodes(iAtlasW);
        stbrp_context packContext;
        stbrp_init_target(&packContext, iAtlasW, iAtlasH, nodes.data(),
                          static_cast<int>(nodes.size()));

        stbrp_pack_rects(&packContext, rects.data(), static_cast<int>(rects.size()));

        uint32_t actualWidth = 0, actualHeight = 0;
        for (const auto& rect : rects)
        {
            if (rect.was_packed)
            {
                actualWidth = std::max(actualWidth, static_cast<uint32_t>(rect.x + rect.w));
                actualHeight = std::max(actualHeight, static_cast<uint32_t>(rect.y + rect.h));
            }
        }

        actualWidth = nextPowerOf2(actualWidth);
        actualHeight = nextPowerOf2(actualHeight);
        actualWidth = std::max(actualWidth, 64u);
        actualHeight = std::max(actualHeight, 64u);

        fontData.atlas.width = actualWidth;
        fontData.atlas.height = actualHeight;
        fontData.atlas.format = resource::FontAtlasFormat::RGBA_32;
        fontData.atlas.pixels.resize(static_cast<size_t>(actualWidth) * actualHeight * 4, 0);

        fontData.glyphs.reserve(glyphTemps.size());

        uint32_t packedCount = 0;

        for (size_t i = 0; i < glyphTemps.size(); ++i)
        {
            const auto& temp = glyphTemps[i];
            const auto& rect = rects[i];

            resource::GlyphData glyph;
            glyph.codepoint = temp.codepoint;
            glyph.advanceX = temp.advanceX;
            glyph.advanceY = 0.0f;
            glyph.bearingX = temp.bearingX;
            glyph.bearingY = temp.bearingY;
            glyph.glyphWidth = static_cast<float>(temp.width);
            glyph.glyphHeight = static_cast<float>(temp.height);

            if (rect.was_packed && temp.width > 0 && temp.height > 0 && !temp.rgbaPixels.empty())
            {
                glyph.atlasX = static_cast<uint32_t>(rect.x);
                glyph.atlasY = static_cast<uint32_t>(rect.y);
                glyph.atlasWidth = static_cast<uint32_t>(temp.width);
                glyph.atlasHeight = static_cast<uint32_t>(temp.height);

                // Copy RGBA pixel data into atlas (4 bytes per pixel)
                for (int y = 0; y < temp.height; ++y)
                {
                    for (int x = 0; x < temp.width; ++x)
                    {
                        size_t atlasIdx = ((static_cast<size_t>(rect.y) + y) * actualWidth
                                        + (static_cast<size_t>(rect.x) + x)) * 4;
                        size_t srcIdx = (static_cast<size_t>(y) * temp.width + x) * 4;

                        if (atlasIdx + 3 < fontData.atlas.pixels.size() &&
                            srcIdx + 3 < temp.rgbaPixels.size())
                        {
                            fontData.atlas.pixels[atlasIdx + 0] = temp.rgbaPixels[srcIdx + 0];
                            fontData.atlas.pixels[atlasIdx + 1] = temp.rgbaPixels[srcIdx + 1];
                            fontData.atlas.pixels[atlasIdx + 2] = temp.rgbaPixels[srcIdx + 2];
                            fontData.atlas.pixels[atlasIdx + 3] = temp.rgbaPixels[srcIdx + 3];
                        }
                    }
                }
                ++packedCount;
            }
            else
            {
                glyph.atlasX = glyph.atlasY = glyph.atlasWidth = glyph.atlasHeight = 0;
            }

            fontData.glyphs.push_back(glyph);
        }

        vfLogInfo("Color font atlas generated: {} glyphs packed. Atlas size: {}x{}",
                 packedCount, actualWidth, actualHeight);

        return true;
    }

    void Font::extractKerningPairs(FT_Face face, float scale,
                                   const std::vector<resource::GlyphData>& glyphs,
                                   std::vector<resource::KerningPair>& kerningPairs) const
    {
        if (!FT_HAS_KERNING(face))
            return;

        // Build a set of glyph indices we care about
        std::vector<std::pair<uint32_t, FT_UInt>> codepointGlyphs;
        for (const auto& glyph : glyphs)
        {
            FT_UInt idx = FT_Get_Char_Index(face, glyph.codepoint);
            if (idx != 0)
            {
                codepointGlyphs.emplace_back(glyph.codepoint, idx);
            }
        }

        // Check kerning for all pairs
        for (const auto& [leftCp, leftIdx] : codepointGlyphs)
        {
            for (const auto& [rightCp, rightIdx] : codepointGlyphs)
            {
                FT_Vector kerning;
                FT_Get_Kerning(face, leftIdx, rightIdx, FT_KERNING_DEFAULT, &kerning);
                float amount = static_cast<float>(kerning.x) / 64.0f;

                if (std::abs(amount) > 0.001f)
                {
                    resource::KerningPair pair;
                    pair.leftCodepoint = leftCp;
                    pair.rightCodepoint = rightCp;
                    pair.kerningAmount = amount;
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
                writeLE<uint32_t>(outFile, glyph.glyphFlags);
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
