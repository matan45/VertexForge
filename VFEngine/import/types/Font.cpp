#include "print/Log.hpp"
#include "Font.hpp"
#include "FontAtlasGenerator.hpp"
#include "FontSerializer.hpp"

#include <fstream>
#include <algorithm>
#include <cmath>

#include <ft2build.h>
#include FT_FREETYPE_H

namespace types
{
    bool Font::loadFromFile(const importConfig::ImportFiles& file, std::string_view fileName,
                            std::string_view location, const FontImportConfig& config,
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
            return false;
        auto cleanupFT = [&]() { if (face) FT_Done_Face(face); if (library) FT_Done_FreeType(library); };

        bool colorFont = isColorFont(face);
        selectFontSize(face, config, colorFont);

        resource::FontData fontData;
        FontImportConfig effectiveConfig;
        initFontData(face, fontData, config, effectiveConfig, fileName, colorFont);
        if (progressCallback) progressCallback(0.2f);

        FontAtlasGenerator atlasGen;
        bool atlasSuccess = colorFont
            ? atlasGen.generateColorAtlas(face, config.baseFontSize, fontData.characterRanges, effectiveConfig, fontData)
            : atlasGen.generateSDFAtlas(face, config.baseFontSize, fontData.characterRanges, effectiveConfig, fontData);
        if (!atlasSuccess)
        {
            vfLogError("Failed to generate font atlas for: {}", file.path);
            cleanupFT();
            return false;
        }
        if (progressCallback) progressCallback(0.8f);

        if (config.includeKerning)
            atlasGen.extractKerningPairs(face, 1.0f, fontData.glyphs, fontData.kerningPairs);

        std::sort(fontData.glyphs.begin(), fontData.glyphs.end(),
                  [](const auto& a, const auto& b) { return a.codepoint < b.codepoint; });

        FontSerializer{}.saveToFile(location, fileName, fontData);
        cleanupFT();
        if (progressCallback) progressCallback(1.0f);

        vfLogDebug("Successfully imported font: {} ({} glyphs, {}x{} atlas, {})",
                  fileName, fontData.glyphs.size(), fontData.atlas.width, fontData.atlas.height,
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

            constexpr std::streamsize maxFontSize = 100 * 1024 * 1024;
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

    void Font::selectFontSize(FT_Face face, const FontImportConfig& config, bool colorFont) const
    {
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
    }

    void Font::initFontData(FT_Face face, resource::FontData& fontData,
                             const FontImportConfig& config, FontImportConfig& effectiveConfig,
                             std::string_view fileName, bool colorFont) const
    {
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
            fontData.formatFlags = fontData.formatFlags | resource::FontFormatFlags::KERNING_ENABLED;

        effectiveConfig = config;
        if (colorFont)
        {
            effectiveConfig.includeEmoji = true;
            effectiveConfig.includeMiscSymbols = true;
            effectiveConfig.includeDingbats = true;
        }
        fontData.characterRanges = buildCharacterRanges(effectiveConfig);
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

        if (config.includeEmoji)
        {
            ranges.push_back({0x1F600, 0x1F64F});
            ranges.push_back({0x1F300, 0x1F5FF});
            ranges.push_back({0x1F900, 0x1F9FF});
            ranges.push_back({0x1FA70, 0x1FAFF});
        }
        if (config.includeMiscSymbols)
            ranges.push_back({0x2600, 0x26FF});
        if (config.includeDingbats)
            ranges.push_back({0x2700, 0x27BF});

        return ranges;
    }
}
