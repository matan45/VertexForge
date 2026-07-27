#pragma once
#include <glm/glm.hpp>
#include <span>
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
        // VK-1636: which FaceSet slot this glyph's atlas came from. Always 0 for
        // the single-font entry points. The caller must key the glyph's draw batch
        // (and therefore its descriptor set) by THIS face's cache key, not the
        // label's - a real Bold face lives in a different atlas.
        uint8_t faceIndex = 0;
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

    // ------------------------------------------------------------------
    // VK-1636: multi-face layout, for real Bold / Italic sibling faces.
    // ------------------------------------------------------------------

    // Up to four faces of one family, indexed by the style bits from
    // text/FontStyleFace.hpp: 0 = regular, 1 = bold, 2 = italic, 3 = bold-italic.
    // faces[0] is mandatory; a null slot silently falls back to it.
    //
    // IMPORTANT: populate ONLY the slots this string actually uses. The block
    // metrics below are a max over every non-null slot, so parking a Bold face in
    // a set whose text contains no bold would silently give the whole label the
    // Bold face's taller line height. computeLineMetrics(FaceSet) takes the same
    // max and MUST be handed the same set, or vertical alignment drifts against
    // the layout it is aligning.
    struct FaceSet
    {
        const resource::FontData* faces[4] = {nullptr, nullptr, nullptr, nullptr};
    };

    // Deliberately no public resolveIndex() helper: a slot can be non-null yet
    // unusable (zero baseFontSize, empty atlas), and a helper that only tested for
    // null would hand callers an index layout never emits. Read the authoritative
    // answer off LayoutGlyph::faceIndex instead.

    // Lays `text` out across `faceSet`, choosing each glyph's face from
    // perCodepointFace (parallel to the decoded codepoint sequence, exactly like
    // RichTextResult::perCodepoint). Entries past the end - and an empty span -
    // mean face 0, so passing {} degrades to plain single-font layout.
    //
    // All faces share one baseline and one line height (the max across the set);
    // stepping the baseline per face would visibly jog the text across a [b]
    // boundary. Kerning is suppressed across a face change, and a codepoint the
    // chosen face lacks is drawn from face 0 rather than dropped - a Bold face
    // imported with narrower character ranges leaves holes otherwise.
    LayoutResult layoutTextStyled(
        const FaceSet& faceSet,
        const std::string& text,
        std::span<const uint8_t> perCodepointFace,
        float fontSize,
        float maxWidth = 0.0f,
        float lineSpacing = 1.0f,
        float letterSpacing = 0.0f
    );

    // Per-line ellipsis truncation. For each line whose width exceeds maxWidth,
    // drop trailing glyphs and append U+2026 (or "..." fallback if U+2026 is not
    // in the atlas). If the rect is too narrow for even the ellipsis, drops all
    // glyphs on that line. Operates in place; updates boundingBox.x accordingly.
    //
    // The ellipsis is always taken from `fontData` (face 0) and keeps faceIndex 0,
    // matching the existing rule that a synthesized glyph renders with the base
    // style. baselineShift is how far layoutTextStyled pushed the shared baseline
    // past this face's own ascender - 0 for every single-font caller, so the
    // arithmetic is unchanged for them.
    void applyEllipsis(
        LayoutResult& layout,
        const resource::FontData& fontData,
        float fontSize,
        float maxWidth,
        float letterSpacing,
        float baselineShift = 0.0f
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

    // VK-1636: the max of the single-face metrics over every populated slot, which
    // is what layoutTextStyled lays the block out with. Hand it the SAME FaceSet.
    [[nodiscard]] LineMetrics computeLineMetrics(const FaceSet& faceSet,
                                                 float fontSize,
                                                 float lineSpacing);

    // VK-1636: how far layoutTextStyled's shared baseline sits below faces[0]'s own
    // ascender, in pixels. Exactly 0.0f when faces[0] has the tallest ascender in
    // the set (and therefore for every single-face set), which is what keeps the
    // legacy arithmetic bit-identical. Feed this to applyEllipsis.
    [[nodiscard]] float baselineShiftForFaceSet(const FaceSet& faceSet, float fontSize);

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

    // ------------------------------------------------------------------
    // VK-1637: box policy. The rules for "which box, which wrap width, which
    // gate" used to be inlined in TextPipeline and UITextPipeline, which are
    // both Vulkan-bound TUs and therefore unreachable from the CPU-only test
    // suite. The rules are pure, so they live here instead. Only the world path
    // (TextPipeline) is wired for now - see the two capability flags below.
    // ------------------------------------------------------------------

    // Mirrors components::TextOverflow; None == components::TextOverflow::Overflow.
    // Kept local for the same reason HAlign / VAlign are: this header must not pull in
    // components/UIComponents.hpp, which drags entt and AssetRef into every consumer.
    // The mapping is locked by static_asserts in tests/test_textbox_policy.cpp.
    enum class OverflowMode : uint8_t { None = 0, Clip = 1, Ellipsis = 2 };

    [[nodiscard]] OverflowMode toOverflowMode(uint8_t value) noexcept;

    // The authored intent, straight off the component / render struct.
    struct TextBoxRequest
    {
        float maxWidth = 0.0f;    // box width; 0 = no box
        float rectHeight = 0.0f;  // box height; 0 = no box
        bool wordWrap = true;
        uint8_t overflow = 0;     // components::TextOverflow ordinal
        uint8_t horizontal = 0;   // components::HorizontalAlignment ordinal
        uint8_t vertical = 0;     // components::VerticalAlignment ordinal
        // Can the calling pipeline actually issue a scissor? UITextPipeline can;
        // TextPipeline cannot, so Clip degrades to None there rather than silently
        // meaning something else.
        bool clipSupported = false;
        // TextPipeline forces VAlign::Top when there is no height; UITextPipeline does
        // not gate at all, because a UIRect always has one. This flag RECORDS that
        // disagreement rather than resolving it - converging the two changes behaviour
        // for degenerate zero-height rects and belongs in its own ticket.
        bool requireHeightForVAlign = true;
    };

    // The resolved decisions. Every field feeds exactly one call downstream.
    struct TextBoxPolicy
    {
        float wrapWidth = 0.0f;        // -> layoutText / layoutTextStyled maxWidth
        bool ellipsis = false;         // -> call applyEllipsis
        float ellipsisWidth = 0.0f;    // -> applyEllipsis maxWidth
        bool clip = false;             // -> caller sets a scissor; always false when
                                       //    clipSupported is false
        HAlign horizontal = HAlign::Left;
        VAlign vertical = VAlign::Top;
        // True when there is no box width to align against. The caller substitutes
        // layout.boundingBox.x, which is only known after layout and therefore cannot be
        // resolved here.
        bool alignToInkWidth = false;
        float alignWidth = 0.0f;
        float alignHeight = 0.0f;
    };

    // Pure. Every gate is written !(x > 0.0f) rather than x <= 0.0f: both text paths take
    // unvalidated script input, and NaN makes `x <= 0` false - it would sail straight into
    // the box branch and poison the layout.
    [[nodiscard]] TextBoxPolicy resolveTextBox(const TextBoxRequest& request) noexcept;
}
