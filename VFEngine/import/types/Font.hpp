#pragma once
#include <string>
#include <functional>
#include <vector>
#include <cstdint>
#include "config/Config.hpp"
#include "resource/Types.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

namespace types
{
    using FontProgressCallback = std::function<void(float progress)>;

    enum class FontFieldMode : uint32_t
    {
        Grayscale = 0,
        SDF = 1,
        MTSDF = 2,
    };

    struct FontImportConfig
    {
        uint32_t baseFontSize = 32;
        FontFieldMode fieldMode = FontFieldMode::SDF;
        bool includeKerning = true;
        uint32_t sdfPadding = 4;
        float sdfSpread = 4.0f;
        uint8_t sdfOnEdgeValue = 128;
        float mtsdfPxRange = 4.0f;

        bool includeBasicLatin = true;
        bool includeLatin1Supplement = true;
        bool includeLatinExtendedA = false;
        bool includeLatinExtendedB = false;
        bool includeGreek = false;
        bool includeCyrillic = false;

        // Emoji ranges (for color emoji fonts)
        bool includeEmoji = false;
        bool includeMiscSymbols = false;
        bool includeDingbats = false;

        uint32_t atlasWidth = 1024;
        uint32_t atlasHeight = 1024;
        uint32_t atlasPadding = 1;
    };

    class Font
    {
    public:
        bool loadFromFile(const importConfig::ImportFiles& file, std::string_view fileName,
                          std::string_view location,
                          const FontImportConfig& config = FontImportConfig{},
                          FontProgressCallback progressCallback = nullptr) const;

    private:
        bool loadFontFile(std::string_view path, std::vector<unsigned char>& buffer) const;

        bool initFreeType(FT_Library& library, FT_Face& face,
                          const std::vector<unsigned char>& buffer,
                          std::string_view path) const;

        void extractFontMetrics(FT_Face face, uint32_t fontSize,
                                resource::FontMetadata& metadata,
                                std::string_view fileName) const;

        bool isColorFont(FT_Face face) const;

        void selectFontSize(FT_Face face, const FontImportConfig& config, bool colorFont) const;

        void initFontData(FT_Face face, resource::FontData& fontData,
                          const FontImportConfig& config, FontImportConfig& effectiveConfig,
                          std::string_view fileName, bool colorFont) const;

        std::vector<resource::CharacterRange> buildCharacterRanges(
            const FontImportConfig& config) const;
    };
}
