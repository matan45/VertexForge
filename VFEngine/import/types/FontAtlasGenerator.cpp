#include "FontAtlasGenerator.hpp"
#include "SDFGenerator.hpp"

#include <algorithm>
#include <cmath>

#define STB_RECT_PACK_IMPLEMENTATION
#include <stb_rect_pack.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_COLOR_H

namespace types
{
    namespace
    {
        struct SDFGlyphTemp
        {
            uint32_t codepoint;
            FT_UInt glyphIndex;
            int targetWidth, targetHeight;
            float bearingX, bearingY;
            float advanceX;
            SDFResult sdfResult;
        };

        struct ColorGlyphTemp
        {
            uint32_t codepoint;
            int width, height;
            float bearingX, bearingY;
            float advanceX;
            std::vector<unsigned char> rgbaPixels;
        };

        struct PackedAtlasResult
        {
            uint32_t actualWidth = 0;
            uint32_t actualHeight = 0;
        };

        uint32_t countTotalGlyphs(const std::vector<resource::CharacterRange>& ranges)
        {
            uint32_t total = 0;
            for (const auto& range : ranges)
                total += (range.rangeEnd - range.rangeStart + 1);
            return total;
        }

        uint32_t nextPowerOf2(uint32_t v)
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

        // ---- Shared packing ----

        PackedAtlasResult packGlyphRects(std::vector<stbrp_rect>& rects,
                                          uint32_t atlasWidth, uint32_t atlasHeight)
        {
            int iWidth = static_cast<int>(atlasWidth);
            int iHeight = static_cast<int>(atlasHeight);

            std::vector<stbrp_node> nodes(iWidth);
            stbrp_context packContext;
            stbrp_init_target(&packContext, iWidth, iHeight, nodes.data(),
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
            actualWidth = std::clamp(actualWidth, 64u, atlasWidth);
            actualHeight = std::clamp(actualHeight, 64u, atlasHeight);
            return {actualWidth, actualHeight};
        }

        void reportUnpackedGlyphs(const std::vector<uint32_t>& unpackedGlyphs,
                                    uint32_t atlasWidth, uint32_t atlasHeight)
        {
            if (unpackedGlyphs.empty()) return;

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
                        unpackedGlyphs.size(), atlasWidth, atlasHeight, failedChars);
        }

        // ---- SDF helpers ----

        std::optional<SDFGlyphTemp> processSDFGlyph(FT_Face face, uint32_t fontSize,
                                                      uint32_t hiresSize, int padding,
                                                      const FontImportConfig& config,
                                                      uint32_t cp)
        {
            FT_UInt glyphIndex = FT_Get_Char_Index(face, cp);
            if (glyphIndex == 0 && cp != ' ')
                return std::nullopt;

            FT_Set_Pixel_Sizes(face, 0, fontSize);
            if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT))
                return std::nullopt;
            float advanceX = static_cast<float>(face->glyph->advance.x) / 64.0f;

            FT_Set_Pixel_Sizes(face, 0, hiresSize);
            if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_RENDER))
            {
                FT_Set_Pixel_Sizes(face, 0, fontSize);
                return std::nullopt;
            }
            FT_Bitmap* bitmap = &face->glyph->bitmap;

            SDFGlyphTemp temp;
            temp.codepoint = cp;
            temp.glyphIndex = glyphIndex;
            temp.advanceX = advanceX;

            if (bitmap->width > 0 && bitmap->rows > 0)
            {
                float scaleDown = static_cast<float>(fontSize) / static_cast<float>(hiresSize);
                temp.targetWidth = static_cast<int>(std::ceil(bitmap->width * scaleDown)) + padding * 2;
                temp.targetHeight = static_cast<int>(std::ceil(bitmap->rows * scaleDown)) + padding * 2;
                temp.bearingX = static_cast<float>(face->glyph->bitmap_left) * scaleDown
                               - static_cast<float>(padding);
                temp.bearingY = static_cast<float>(face->glyph->bitmap_top) * scaleDown
                               + static_cast<float>(padding);

                SDFResult sdf = SDFGenerator::generateFromBitmap(
                    bitmap->buffer,
                    static_cast<int>(bitmap->width), static_cast<int>(bitmap->rows),
                    temp.targetWidth, temp.targetHeight,
                    config.sdfSpread, config.sdfOnEdgeValue);
                temp.targetWidth = sdf.width;
                temp.targetHeight = sdf.height;
                temp.sdfResult = std::move(sdf);
            }
            else
            {
                temp.targetWidth = 0;
                temp.targetHeight = 0;
                temp.bearingX = 0.0f;
                temp.bearingY = 0.0f;
            }
            return temp;
        }

        std::pair<std::vector<SDFGlyphTemp>, std::vector<stbrp_rect>>
        rasterizeSDFGlyphs(FT_Face face, uint32_t fontSize,
                           const std::vector<resource::CharacterRange>& ranges,
                           const FontImportConfig& config)
        {
            uint32_t hiresSize = fontSize * 4;
            int padding = static_cast<int>(config.sdfPadding);
            uint32_t totalGlyphs = countTotalGlyphs(ranges);

            std::vector<SDFGlyphTemp> glyphTemps;
            glyphTemps.reserve(totalGlyphs);
            std::vector<stbrp_rect> rects;
            rects.reserve(totalGlyphs);
            int rectId = 0;

            for (const auto& range : ranges)
            {
                for (uint32_t cp = range.rangeStart; cp <= range.rangeEnd; ++cp)
                {
                    auto temp = processSDFGlyph(face, fontSize, hiresSize, padding, config, cp);
                    if (!temp) continue;

                    glyphTemps.push_back(std::move(*temp));
                    stbrp_rect rect{};
                    rect.id = rectId++;
                    rect.w = static_cast<stbrp_coord>(glyphTemps.back().targetWidth + config.atlasPadding);
                    rect.h = static_cast<stbrp_coord>(glyphTemps.back().targetHeight + config.atlasPadding);
                    rects.push_back(rect);
                }
            }
            return {std::move(glyphTemps), std::move(rects)};
        }

        void copySDFPixelsToAtlas(const SDFGlyphTemp& temp, const stbrp_rect& rect,
                                    uint32_t actualWidth, resource::FontData& fontData)
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
                        fontData.atlas.pixels[atlasIdx] = temp.sdfResult.pixels[srcIdx];
                }
            }
        }

        resource::GlyphData buildGlyphData(uint32_t codepoint, float advanceX,
                                             float bearingX, float bearingY,
                                             float glyphWidth, float glyphHeight)
        {
            resource::GlyphData glyph;
            glyph.codepoint = codepoint;
            glyph.advanceX = advanceX;
            glyph.advanceY = 0.0f;
            glyph.bearingX = bearingX;
            glyph.bearingY = bearingY;
            glyph.glyphWidth = glyphWidth;
            glyph.glyphHeight = glyphHeight;
            glyph.atlasX = glyph.atlasY = glyph.atlasWidth = glyph.atlasHeight = 0;
            return glyph;
        }

        void compositeSDFAtlas(const std::vector<SDFGlyphTemp>& glyphTemps,
                                const std::vector<stbrp_rect>& rects,
                                const PackedAtlasResult& dims,
                                const FontImportConfig& config,
                                resource::FontData& fontData)
        {
            fontData.atlas.width = dims.actualWidth;
            fontData.atlas.height = dims.actualHeight;
            fontData.atlas.format = config.generateSDF ?
                resource::FontAtlasFormat::SDF_8 : resource::FontAtlasFormat::GRAYSCALE_8;
            fontData.atlas.pixels.resize(static_cast<size_t>(dims.actualWidth) * dims.actualHeight, 0);
            fontData.glyphs.reserve(glyphTemps.size());

            std::vector<uint32_t> unpackedGlyphs;
            uint32_t emptyGlyphCount = 0;
            uint32_t packedCount = 0;

            for (size_t i = 0; i < glyphTemps.size(); ++i)
            {
                const auto& temp = glyphTemps[i];
                const auto& rect = rects[i];
                auto glyph = buildGlyphData(temp.codepoint, temp.advanceX, temp.bearingX,
                                             temp.bearingY, static_cast<float>(temp.targetWidth),
                                             static_cast<float>(temp.targetHeight));

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
                        if (destEndX > dims.actualWidth || destEndY > dims.actualHeight)
                        {
                            glyph.atlasX = glyph.atlasY = glyph.atlasWidth = glyph.atlasHeight = 0;
                            unpackedGlyphs.push_back(temp.codepoint);
                        }
                        else
                        {
                            copySDFPixelsToAtlas(temp, rect, dims.actualWidth, fontData);
                        }
                    }
                    ++packedCount;
                }
                else if (temp.targetWidth > 0 && temp.targetHeight > 0)
                {
                    unpackedGlyphs.push_back(temp.codepoint);
                }
                else
                {
                    ++emptyGlyphCount;
                }

                fontData.glyphs.push_back(glyph);
            }

            reportUnpackedGlyphs(unpackedGlyphs, dims.actualWidth, dims.actualHeight);
            vfLogInfo("Font atlas generated: {} glyphs packed, {} empty glyphs, {} failed to pack. "
                     "Atlas size: {}x{}",
                     packedCount, emptyGlyphCount, unpackedGlyphs.size(),
                     dims.actualWidth, dims.actualHeight);
        }

        // ---- Color helpers ----

        void selectStrikeAndPalette(FT_Face face, uint32_t fontSize)
        {
            if (face->num_fixed_sizes > 0)
            {
                int bestStrike = 0;
                int bestDiff = std::abs(static_cast<int>(face->available_sizes[0].height)
                                      - static_cast<int>(fontSize));
                for (int i = 1; i < face->num_fixed_sizes; ++i)
                {
                    int diff = std::abs(static_cast<int>(face->available_sizes[i].height)
                                      - static_cast<int>(fontSize));
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

            FT_Palette_Data paletteData;
            if (FT_Palette_Data_Get(face, &paletteData) == 0 && paletteData.num_palettes > 0)
            {
                FT_Color* palette = nullptr;
                FT_Palette_Select(face, 0, &palette);
            }
        }

        void convertBitmapToRGBA(const FT_Bitmap* bitmap, std::vector<unsigned char>& rgbaPixels)
        {
            size_t pixelCount = static_cast<size_t>(bitmap->width) * bitmap->rows;
            rgbaPixels.resize(pixelCount * 4);

            if (bitmap->pixel_mode == FT_PIXEL_MODE_BGRA)
            {
                for (size_t p = 0; p < pixelCount; ++p)
                {
                    size_t off = p * 4;
                    rgbaPixels[off + 0] = bitmap->buffer[off + 2];
                    rgbaPixels[off + 1] = bitmap->buffer[off + 1];
                    rgbaPixels[off + 2] = bitmap->buffer[off + 0];
                    rgbaPixels[off + 3] = bitmap->buffer[off + 3];
                }
            }
            else if (bitmap->pixel_mode == FT_PIXEL_MODE_GRAY)
            {
                for (size_t p = 0; p < pixelCount; ++p)
                {
                    rgbaPixels[p * 4 + 0] = 255;
                    rgbaPixels[p * 4 + 1] = 255;
                    rgbaPixels[p * 4 + 2] = 255;
                    rgbaPixels[p * 4 + 3] = bitmap->buffer[p];
                }
            }
        }

        std::optional<ColorGlyphTemp> processColorGlyph(FT_Face face, uint32_t cp)
        {
            FT_UInt glyphIndex = FT_Get_Char_Index(face, cp);
            if (glyphIndex == 0 && cp != ' ')
                return std::nullopt;

            FT_Int32 loadFlags = FT_LOAD_COLOR | FT_LOAD_RENDER | FT_LOAD_NO_SVG;
            if (FT_Load_Glyph(face, glyphIndex, loadFlags))
                return std::nullopt;

            FT_Bitmap* bitmap = &face->glyph->bitmap;
            if (bitmap->width == 0 || bitmap->rows == 0)
            {
                if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_RENDER | FT_LOAD_NO_SVG))
                    return std::nullopt;
                bitmap = &face->glyph->bitmap;
                if (bitmap->width == 0 || bitmap->rows == 0)
                    return std::nullopt;
            }

            ColorGlyphTemp temp;
            temp.codepoint = cp;
            temp.width = static_cast<int>(bitmap->width);
            temp.height = static_cast<int>(bitmap->rows);
            temp.bearingX = static_cast<float>(face->glyph->bitmap_left);
            temp.bearingY = static_cast<float>(face->glyph->bitmap_top);
            temp.advanceX = static_cast<float>(face->glyph->advance.x) / 64.0f;

            if (bitmap->buffer)
                convertBitmapToRGBA(bitmap, temp.rgbaPixels);

            if (temp.rgbaPixels.empty())
                return std::nullopt;
            return temp;
        }

        std::pair<std::vector<ColorGlyphTemp>, std::vector<stbrp_rect>>
        rasterizeColorGlyphs(FT_Face face,
                              const std::vector<resource::CharacterRange>& ranges,
                              const FontImportConfig& config)
        {
            uint32_t totalGlyphs = countTotalGlyphs(ranges);
            std::vector<ColorGlyphTemp> glyphTemps;
            glyphTemps.reserve(totalGlyphs);
            std::vector<stbrp_rect> rects;
            rects.reserve(totalGlyphs);
            int rectId = 0;

            for (const auto& range : ranges)
            {
                for (uint32_t cp = range.rangeStart; cp <= range.rangeEnd; ++cp)
                {
                    auto temp = processColorGlyph(face, cp);
                    if (!temp) continue;

                    glyphTemps.push_back(std::move(*temp));
                    stbrp_rect rect{};
                    rect.id = rectId++;
                    rect.w = static_cast<stbrp_coord>(glyphTemps.back().width + config.atlasPadding);
                    rect.h = static_cast<stbrp_coord>(glyphTemps.back().height + config.atlasPadding);
                    rects.push_back(rect);
                }
            }
            return {std::move(glyphTemps), std::move(rects)};
        }

        void copyColorPixelsToAtlas(const ColorGlyphTemp& temp, const stbrp_rect& rect,
                                      uint32_t actualWidth, resource::FontData& fontData)
        {
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
        }

        void compositeColorAtlas(const std::vector<ColorGlyphTemp>& glyphTemps,
                                  const std::vector<stbrp_rect>& rects,
                                  const PackedAtlasResult& dims,
                                  resource::FontData& fontData)
        {
            fontData.atlas.width = dims.actualWidth;
            fontData.atlas.height = dims.actualHeight;
            fontData.atlas.format = resource::FontAtlasFormat::RGBA_32;
            fontData.atlas.pixels.resize(static_cast<size_t>(dims.actualWidth) * dims.actualHeight * 4, 0);
            fontData.glyphs.reserve(glyphTemps.size());
            uint32_t packedCount = 0;

            for (size_t i = 0; i < glyphTemps.size(); ++i)
            {
                const auto& temp = glyphTemps[i];
                const auto& rect = rects[i];
                auto glyph = buildGlyphData(temp.codepoint, temp.advanceX, temp.bearingX,
                                             temp.bearingY, static_cast<float>(temp.width),
                                             static_cast<float>(temp.height));

                if (rect.was_packed && temp.width > 0 && temp.height > 0 && !temp.rgbaPixels.empty())
                {
                    glyph.atlasX = static_cast<uint32_t>(rect.x);
                    glyph.atlasY = static_cast<uint32_t>(rect.y);
                    glyph.atlasWidth = static_cast<uint32_t>(temp.width);
                    glyph.atlasHeight = static_cast<uint32_t>(temp.height);
                    copyColorPixelsToAtlas(temp, rect, dims.actualWidth, fontData);
                    ++packedCount;
                }
                fontData.glyphs.push_back(glyph);
            }

            vfLogInfo("Color font atlas generated: {} glyphs packed. Atlas size: {}x{}",
                     packedCount, dims.actualWidth, dims.actualHeight);
        }
    } // anonymous namespace

    // ---- Public methods ----

    bool FontAtlasGenerator::generateSDFAtlas(FT_Face face, uint32_t fontSize,
                                               const std::vector<resource::CharacterRange>& ranges,
                                               const FontImportConfig& config,
                                               resource::FontData& fontData) const
    {
        auto [glyphTemps, rects] = rasterizeSDFGlyphs(face, fontSize, ranges, config);
        FT_Set_Pixel_Sizes(face, 0, fontSize);

        if (rects.empty())
        {
            vfLogWarning("No valid glyphs found in font");
            return false;
        }

        auto dims = packGlyphRects(rects, config.atlasWidth, config.atlasHeight);
        compositeSDFAtlas(glyphTemps, rects, dims, config, fontData);
        return true;
    }

    bool FontAtlasGenerator::generateColorAtlas(FT_Face face, uint32_t fontSize,
                                                  const std::vector<resource::CharacterRange>& ranges,
                                                  const FontImportConfig& config,
                                                  resource::FontData& fontData) const
    {
        selectStrikeAndPalette(face, fontSize);
        auto [glyphTemps, rects] = rasterizeColorGlyphs(face, ranges, config);

        if (rects.empty())
        {
            vfLogWarning("No valid glyphs found in color font");
            return false;
        }

        uint32_t atlasW = std::max(config.atlasWidth, 2048u);
        uint32_t atlasH = std::max(config.atlasHeight, 2048u);
        auto dims = packGlyphRects(rects, atlasW, atlasH);
        compositeColorAtlas(glyphTemps, rects, dims, fontData);
        return true;
    }

    void FontAtlasGenerator::extractKerningPairs(FT_Face face, float scale,
                                                   const std::vector<resource::GlyphData>& glyphs,
                                                   std::vector<resource::KerningPair>& kerningPairs) const
    {
        if (!FT_HAS_KERNING(face))
            return;

        std::vector<std::pair<uint32_t, FT_UInt>> codepointGlyphs;
        for (const auto& glyph : glyphs)
        {
            FT_UInt idx = FT_Get_Char_Index(face, glyph.codepoint);
            if (idx != 0)
                codepointGlyphs.emplace_back(glyph.codepoint, idx);
        }

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
}
