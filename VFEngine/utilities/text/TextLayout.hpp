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
        glm::vec2 offset;    // Pixel offset from text origin (visual top-left of glyph quad)
        glm::vec2 size;      // Glyph quad size in pixels
        glm::vec4 uvRect;    // u0, v0, u1, v1 in atlas (normalized 0-1)
        uint32_t codepoint = 0;
        float lineY = 0.0f;  // cursorY of the line this glyph was placed on.
                             // Use this (not offset.y) to group glyphs by line,
                             // since offset.y varies per glyph by bearingY.
        // Index of this glyph's codepoint in the source string's decoded
        // codepoint sequence (every decodeUTF8 result counts, including
        // whitespace and glyphless codepoints). UINT32_MAX for synthesized
        // glyphs (e.g. the ellipsis), which render with the base style.
        uint32_t charIndex = 0;
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

    // Per-line ellipsis truncation. For each line whose width exceeds maxWidth,
    // drop trailing glyphs and append U+2026 (or "..." fallback if U+2026 is not
    // in the atlas). If the rect is too narrow for even the ellipsis, drops all
    // glyphs on that line. Operates in place; updates boundingBox.x accordingly.
    void applyEllipsis(
        LayoutResult& layout,
        const resource::FontData& fontData,
        float fontSize,
        float maxWidth,
        float letterSpacing
    );
}
