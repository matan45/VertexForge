#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <cstddef>
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

    // ------------------------------------------------------------------
    // VK-1632: alignment. Shared by TextPipeline (world/screen TextComponent)
    // and UITextPipeline (UILabelComponent), which each used to carry their
    // own copy of the line-partition + alignment loop.
    // ------------------------------------------------------------------

    // Mirrors components::HorizontalAlignment / VerticalAlignment. The render
    // structs (TextTypes.hpp, UITextRenderTypes.hpp) carry the raw uint8_t, so
    // use toHAlign / toVAlign to convert - they map anything out of range to
    // Left / Top, matching the old switch statements' `default:` arm.
    enum class HAlign : uint8_t { Left = 0, Center = 1, Right = 2 };
    enum class VAlign : uint8_t { Top = 0, Middle = 1, Bottom = 2 };

    [[nodiscard]] HAlign toHAlign(uint8_t value) noexcept;
    [[nodiscard]] VAlign toVAlign(uint8_t value) noexcept;

    // A run of glyphs that share a visual line.
    struct LineSpan
    {
        size_t start = 0;    // index of the first glyph in LayoutResult::glyphs
        size_t count = 0;
        float minX = 0.0f;   // ink extents (offset.x .. offset.x + size.x), not advance extents
        float maxX = 0.0f;
        float lineY = 0.0f;  // cursorY of this line's origin
    };

    // Group glyphs by their lineY (the line origin recorded at layout time).
    // Critically NOT by offset.y, which varies per glyph based on bearingY
    // and would split a single visual line into one fake "line" per bearingY.
    [[nodiscard]] std::vector<LineSpan> partitionLines(const std::vector<LayoutGlyph>& glyphs);

    struct LineMetrics
    {
        // Distance between consecutive line origins: metadata.lineHeight * scale * lineSpacing.
        float lineHeight = 0.0f;
        // Height of a single line's em box: (ascender - descender) * scale. Excludes the
        // font's line gap and is NOT scaled by lineSpacing, so the last line of a block
        // contributes only its own height.
        float singleLineHeight = 0.0f;
    };

    [[nodiscard]] LineMetrics computeLineMetrics(const resource::FontData& fontData,
                                                 float fontSize,
                                                 float lineSpacing);

    struct AlignParams
    {
        HAlign horizontal = HAlign::Left;
        VAlign vertical = VAlign::Top;
        // Box to align inside. The caller owns any fallback/gating policy (e.g. substituting
        // boundingBox.x when there is no explicit rect, or forcing VAlign::Top when the rect
        // has no height) - this function does no gating of its own.
        glm::vec2 contentSize{0.0f};
        float lineHeight = 0.0f;
        float singleLineHeight = 0.0f;
    };

    struct AlignmentOffsets
    {
        std::vector<LineSpan> lines;
        std::vector<float> lineOffsetX;  // parallel to `lines`
        float offsetY = 0.0f;            // applies to the whole block
    };

    // Pure: partitions `layout` into lines and returns the offsets that would align it.
    [[nodiscard]] AlignmentOffsets computeAlignedLineOrigins(const LayoutResult& layout,
                                                             const AlignParams& params);

    // computeAlignedLineOrigins + fold the offsets into each glyph's offset.
    // boundingBox is left untouched: it describes the un-aligned layout.
    void applyAlignment(LayoutResult& layout, const AlignParams& params);
}
