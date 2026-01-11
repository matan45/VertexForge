#pragma once
#include <string>
#include <functional>
#include <vector>
#include <cstdint>
#include "config/Config.hpp"
#include "resource/Types.hpp"

namespace types
{
    using FontProgressCallback = std::function<void(float progress)>;

    struct FontImportConfig
    {
        uint32_t baseFontSize = 32;
        bool generateSDF = true;
        bool includeKerning = true;
        uint32_t sdfPadding = 4;
        float sdfSpread = 4.0f;
        uint8_t sdfOnEdgeValue = 128;

        // Character ranges
        bool includeBasicLatin = true;        // U+0020-U+007F
        bool includeLatin1Supplement = true;  // U+0080-U+00FF
        bool includeLatinExtendedA = false;   // U+0100-U+017F
        bool includeLatinExtendedB = false;   // U+0180-U+024F
        bool includeGreek = false;            // U+0370-U+03FF
        bool includeCyrillic = false;         // U+0400-U+04FF

        // Atlas settings
        uint32_t atlasWidth = 1024;
        uint32_t atlasHeight = 1024;
        uint32_t atlasPadding = 1;
    };

    class Font
    {
    public:
        void loadFromFile(const importConfig::ImportFiles& file, std::string_view fileName,
                          std::string_view location,
                          const FontImportConfig& config = FontImportConfig{},
                          FontProgressCallback progressCallback = nullptr) const;

    private:
        // Font loading
        bool loadFontFile(std::string_view path, std::vector<unsigned char>& buffer) const;

        // Metrics extraction
        void extractFontMetrics(const void* fontInfo, float scale,
                                resource::FontMetadata& metadata,
                                std::string_view fileName) const;

        // Character ranges
        std::vector<resource::CharacterRange> buildCharacterRanges(
            const FontImportConfig& config) const;

        // Atlas generation
        bool generateSDFAtlas(const void* fontInfo, float scale,
                              const std::vector<resource::CharacterRange>& ranges,
                              const FontImportConfig& config,
                              resource::FontData& fontData) const;

        // Kerning extraction
        void extractKerningPairs(const void* fontInfo, float scale,
                                 const std::vector<resource::GlyphData>& glyphs,
                                 std::vector<resource::KerningPair>& kerningPairs) const;

        // Serialization
        void saveToFile(std::string_view location, std::string_view fileName,
                        const resource::FontData& fontData) const;

        // Helpers
        uint32_t countTotalGlyphs(const std::vector<resource::CharacterRange>& ranges) const;
        static uint32_t nextPowerOf2(uint32_t v);
    };
}
