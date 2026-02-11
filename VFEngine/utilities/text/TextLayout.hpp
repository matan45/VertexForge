#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <cstdint>

namespace resource
{
    struct FontData;
}

namespace text
{
    struct LayoutGlyph
    {
        glm::vec2 offset;    // Pixel offset from text origin
        glm::vec2 size;      // Glyph quad size in pixels
        glm::vec4 uvRect;    // u0, v0, u1, v1 in atlas (normalized 0-1)
        uint32_t codepoint = 0;
    };

    struct LayoutResult
    {
        std::vector<LayoutGlyph> glyphs;
        glm::vec2 boundingBox{0.0f};
    };

    uint32_t decodeUTF8(const std::string& text, size_t& index);

    LayoutResult layoutText(
        const resource::FontData& fontData,
        const std::string& text,
        float fontSize,
        float maxWidth = 0.0f,
        float lineSpacing = 1.0f,
        float letterSpacing = 0.0f
    );
}
