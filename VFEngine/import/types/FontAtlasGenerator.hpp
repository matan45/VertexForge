#pragma once
#include <vector>
#include <cstdint>
#include "Font.hpp"
#include "resource/Types.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

namespace types
{
    class FontAtlasGenerator
    {
    public:
        bool generateGrayscaleAtlas(FT_Face face, uint32_t fontSize,
                                    const std::vector<resource::CharacterRange>& ranges,
                                    const FontImportConfig& config,
                                    resource::FontData& fontData) const;

        bool generateSDFAtlas(FT_Face face, uint32_t fontSize,
                              const std::vector<resource::CharacterRange>& ranges,
                              const FontImportConfig& config,
                              resource::FontData& fontData) const;

        bool generateMTSDFAtlas(FT_Face face, uint32_t fontSize,
                                const std::vector<resource::CharacterRange>& ranges,
                                const FontImportConfig& config,
                                resource::FontData& fontData) const;

        bool generateColorAtlas(FT_Face face, uint32_t fontSize,
                                const std::vector<resource::CharacterRange>& ranges,
                                const FontImportConfig& config,
                                resource::FontData& fontData) const;

        void extractKerningPairs(FT_Face face, float scale,
                                 const std::vector<resource::GlyphData>& glyphs,
                                 std::vector<resource::KerningPair>& kerningPairs) const;
    };
}
