#include <doctest.h>
#include <text/TextLayout.hpp>
#include <resource/Types.hpp>
#include <components/UIComponents.hpp>
#include <algorithm>

// ============================================================
// VK-1632: TextLayout line partitioning + alignment unit tests
// ============================================================
//
// Alignment used to be copy-pasted into TextPipeline.cpp and UITextPipeline.cpp,
// where both copies grouped glyphs into lines by LayoutGlyph::offset.y. offset.y
// is cursorY + (ascender - bearingY) * scale, so it varies per glyph and split one
// visual line into a fake line per bearingY. These tests pin the extracted
// text::partitionLines / text::computeAlignedLineOrigins / text::applyAlignment.
//
// Fixture arithmetic (mirrors test_textlayout_ellipsis.cpp):
//   baseFontSize 32, lineHeight 40, ascender 32, descender -8
//   every glyph: advanceX 8, bearingX 0, bearingY 32, atlas 8 x 32
// At fontSize 32 => scale 1, lineHeight 40, singleLineHeight (32 - -8) = 40.

namespace
{
    void pushGlyph(resource::FontData& font, uint32_t cp, float bearingY, uint32_t atlasWidth)
    {
        resource::GlyphData g;
        g.codepoint = cp;
        g.advanceX = 8.0f;
        g.advanceY = 0.0f;
        g.bearingX = 0.0f;
        g.bearingY = bearingY;
        g.glyphWidth = static_cast<float>(atlasWidth);
        g.glyphHeight = 32.0f;
        g.atlasX = 0;
        g.atlasY = 0;
        g.atlasWidth = atlasWidth;
        g.atlasHeight = 32;
        font.glyphs.push_back(g);
    }

    resource::FontData makeFixtureFont()
    {
        resource::FontData font;
        font.metadata.baseFontSize = 32;
        font.metadata.lineHeight = 40.0f;
        font.metadata.ascender = 32.0f;
        font.metadata.descender = -8.0f;

        font.atlas.width = 256;
        font.atlas.height = 256;
        font.atlas.format = resource::FontAtlasFormat::GRAYSCALE_8;

        for (uint32_t cp = 0x20; cp < 0x7F; ++cp)
        {
            pushGlyph(font, cp, 32.0f, 8);
        }
        pushGlyph(font, 0x2026, 32.0f, 8); // U+2026 HORIZONTAL ELLIPSIS

        std::sort(font.glyphs.begin(), font.glyphs.end(),
                  [](const resource::GlyphData& a, const resource::GlyphData& b) {
                      return a.codepoint < b.codepoint;
                  });
        return font;
    }

    // A real font's bearingY varies per glyph ('A' sits on the cap line, 'a' on the
    // x-height line), which is exactly what broke the old offset.y grouping. 'a' is
    // also narrower so the two groupings produce *different* centered offsets.
    resource::FontData makeMixedBearingFont()
    {
        resource::FontData font = makeFixtureFont();
        for (auto& g : font.glyphs)
        {
            if (g.codepoint == 'a')
            {
                g.bearingY = 20.0f;
                g.atlasWidth = 4;
                g.glyphWidth = 4.0f;
            }
        }
        return font;
    }

    text::AlignParams params(text::HAlign h, text::VAlign v, glm::vec2 contentSize,
                             float lineSpacing = 1.0f)
    {
        text::AlignParams p;
        p.horizontal = h;
        p.vertical = v;
        p.contentSize = contentSize;
        p.lineHeight = 40.0f * lineSpacing;
        p.singleLineHeight = 40.0f;
        return p;
    }
}

TEST_SUITE("TextLayoutAlignment") {

// ------------------------------------------------------------------
// Enum bridging
// ------------------------------------------------------------------

TEST_CASE("HAlign/VAlign mirror the component enums") {
    static_assert(static_cast<uint8_t>(text::HAlign::Left) ==
                  static_cast<uint8_t>(components::HorizontalAlignment::Left));
    static_assert(static_cast<uint8_t>(text::HAlign::Center) ==
                  static_cast<uint8_t>(components::HorizontalAlignment::Center));
    static_assert(static_cast<uint8_t>(text::HAlign::Right) ==
                  static_cast<uint8_t>(components::HorizontalAlignment::Right));
    static_assert(static_cast<uint8_t>(text::VAlign::Top) ==
                  static_cast<uint8_t>(components::VerticalAlignment::Top));
    static_assert(static_cast<uint8_t>(text::VAlign::Middle) ==
                  static_cast<uint8_t>(components::VerticalAlignment::Middle));
    static_assert(static_cast<uint8_t>(text::VAlign::Bottom) ==
                  static_cast<uint8_t>(components::VerticalAlignment::Bottom));

    CHECK(text::toHAlign(0) == text::HAlign::Left);
    CHECK(text::toHAlign(1) == text::HAlign::Center);
    CHECK(text::toHAlign(2) == text::HAlign::Right);
    CHECK(text::toVAlign(0) == text::VAlign::Top);
    CHECK(text::toVAlign(1) == text::VAlign::Middle);
    CHECK(text::toVAlign(2) == text::VAlign::Bottom);

    // Out of range falls back to Left/Top, matching the old switch `default:` arms.
    CHECK(text::toHAlign(3) == text::HAlign::Left);
    CHECK(text::toHAlign(255) == text::HAlign::Left);
    CHECK(text::toVAlign(7) == text::VAlign::Top);
    CHECK(text::toVAlign(255) == text::VAlign::Top);
}

TEST_CASE("computeLineMetrics derives from font metadata") {
    auto font = makeFixtureFont();

    auto m = text::computeLineMetrics(font, 32.0f, 1.0f);
    CHECK(m.lineHeight == doctest::Approx(40.0f));
    CHECK(m.singleLineHeight == doctest::Approx(40.0f));

    // lineSpacing scales the distance between line origins, never a line's own height.
    m = text::computeLineMetrics(font, 32.0f, 2.0f);
    CHECK(m.lineHeight == doctest::Approx(80.0f));
    CHECK(m.singleLineHeight == doctest::Approx(40.0f));

    // fontSize scales both.
    m = text::computeLineMetrics(font, 16.0f, 1.0f);
    CHECK(m.lineHeight == doctest::Approx(20.0f));
    CHECK(m.singleLineHeight == doctest::Approx(20.0f));

    // A font with a line gap: lineHeight > ascender - descender.
    font.metadata.lineHeight = 48.0f;
    m = text::computeLineMetrics(font, 32.0f, 1.0f);
    CHECK(m.lineHeight == doctest::Approx(48.0f));
    CHECK(m.singleLineHeight == doctest::Approx(40.0f));

    // Degenerate metadata falls back to the full line height.
    font.metadata.ascender = 0.0f;
    font.metadata.descender = 0.0f;
    m = text::computeLineMetrics(font, 32.0f, 1.0f);
    CHECK(m.singleLineHeight == doctest::Approx(48.0f));
}

// ------------------------------------------------------------------
// partitionLines
// ------------------------------------------------------------------

TEST_CASE("partitionLines: empty layout yields no lines") {
    CHECK(text::partitionLines({}).empty());
}

TEST_CASE("partitionLines: single line spans every glyph") {
    auto font = makeFixtureFont();
    auto layout = text::layoutText(font, "AB", 32.0f, 0.0f, 1.0f, 0.0f);

    auto lines = text::partitionLines(layout.glyphs);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].start == 0);
    CHECK(lines[0].count == 2);
    CHECK(lines[0].minX == doctest::Approx(0.0f));
    CHECK(lines[0].maxX == doctest::Approx(16.0f));
    CHECK(lines[0].lineY == doctest::Approx(0.0f));
}

TEST_CASE("partitionLines: explicit newline splits lines") {
    auto font = makeFixtureFont();
    auto layout = text::layoutText(font, "AB\nA", 32.0f, 0.0f, 1.0f, 0.0f);

    auto lines = text::partitionLines(layout.glyphs);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].count == 2);
    CHECK(lines[0].lineY == doctest::Approx(0.0f));
    CHECK(lines[0].maxX == doctest::Approx(16.0f));
    CHECK(lines[1].start == 2);
    CHECK(lines[1].count == 1);
    CHECK(lines[1].lineY == doctest::Approx(40.0f));
    CHECK(lines[1].maxX == doctest::Approx(8.0f));
}

TEST_CASE("partitionLines: varied per-glyph bearingY still groups as one line") {
    // Regression for the bug the two pipelines carried: grouping by offset.y.
    auto font = makeMixedBearingFont();
    auto layout = text::layoutText(font, "Aa", 32.0f, 0.0f, 1.0f, 0.0f);

    REQUIRE(layout.glyphs.size() == 2);
    // The glyphs really do sit at different offset.y ...
    CHECK(layout.glyphs[0].offset.y == doctest::Approx(0.0f));
    CHECK(layout.glyphs[1].offset.y == doctest::Approx(12.0f));
    // ... but share a line origin.
    CHECK(layout.glyphs[0].lineY == doctest::Approx(0.0f));
    CHECK(layout.glyphs[1].lineY == doctest::Approx(0.0f));

    auto lines = text::partitionLines(layout.glyphs);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].count == 2);
    CHECK(lines[0].minX == doctest::Approx(0.0f));
    CHECK(lines[0].maxX == doctest::Approx(12.0f)); // 'a' is 4 px wide at x = 8
}

// ------------------------------------------------------------------
// Horizontal alignment
// ------------------------------------------------------------------

TEST_CASE("horizontal alignment: single line L/C/R") {
    auto font = makeFixtureFont();
    // "AB" is 16 px of ink in a 100 px box.
    const glm::vec2 box{100.0f, 100.0f};

    SUBCASE("left is a no-op") {
        auto layout = text::layoutText(font, "AB", 32.0f, 0.0f, 1.0f, 0.0f);
        auto offsets = text::computeAlignedLineOrigins(
            layout, params(text::HAlign::Left, text::VAlign::Top, box));
        REQUIRE(offsets.lineOffsetX.size() == 1);
        CHECK(offsets.lineOffsetX[0] == doctest::Approx(0.0f));
        CHECK(offsets.offsetY == doctest::Approx(0.0f));
    }

    SUBCASE("center") {
        auto layout = text::layoutText(font, "AB", 32.0f, 0.0f, 1.0f, 0.0f);
        auto offsets = text::computeAlignedLineOrigins(
            layout, params(text::HAlign::Center, text::VAlign::Top, box));
        REQUIRE(offsets.lineOffsetX.size() == 1);
        CHECK(offsets.lineOffsetX[0] == doctest::Approx(42.0f)); // (100 - 16) / 2
    }

    SUBCASE("right") {
        auto layout = text::layoutText(font, "AB", 32.0f, 0.0f, 1.0f, 0.0f);
        auto offsets = text::computeAlignedLineOrigins(
            layout, params(text::HAlign::Right, text::VAlign::Top, box));
        REQUIRE(offsets.lineOffsetX.size() == 1);
        CHECK(offsets.lineOffsetX[0] == doctest::Approx(84.0f)); // 100 - 16
    }
}

TEST_CASE("horizontal alignment: each line gets its own offset") {
    auto font = makeFixtureFont();
    auto layout = text::layoutText(font, "AB\nA", 32.0f, 0.0f, 1.0f, 0.0f);

    auto offsets = text::computeAlignedLineOrigins(
        layout, params(text::HAlign::Center, text::VAlign::Top, {100.0f, 100.0f}));
    REQUIRE(offsets.lineOffsetX.size() == 2);
    CHECK(offsets.lineOffsetX[0] == doctest::Approx(42.0f)); // (100 - 16) / 2
    CHECK(offsets.lineOffsetX[1] == doctest::Approx(46.0f)); // (100 -  8) / 2
}

TEST_CASE("horizontal alignment: letterSpacing widens the line") {
    auto font = makeFixtureFont();
    // advance 8 + spacing 4 => 'B' starts at 12, ink ends at 20.
    auto layout = text::layoutText(font, "AB", 32.0f, 0.0f, 1.0f, 4.0f);

    auto lines = text::partitionLines(layout.glyphs);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].maxX == doctest::Approx(20.0f));

    auto offsets = text::computeAlignedLineOrigins(
        layout, params(text::HAlign::Center, text::VAlign::Top, {100.0f, 100.0f}));
    CHECK(offsets.lineOffsetX[0] == doctest::Approx(40.0f)); // (100 - 20) / 2
}

TEST_CASE("horizontal alignment: grouping by lineY, not offset.y") {
    // With mixed bearingY, offset.y grouping would make two fake lines of width 8
    // and 4 and shift them by 46 and 48. lineY grouping sees one 12 px line => 44.
    auto font = makeMixedBearingFont();
    auto layout = text::layoutText(font, "Aa", 32.0f, 0.0f, 1.0f, 0.0f);

    text::applyAlignment(layout, params(text::HAlign::Center, text::VAlign::Top,
                                        {100.0f, 100.0f}));

    REQUIRE(layout.glyphs.size() == 2);
    CHECK(layout.glyphs[0].offset.x == doctest::Approx(44.0f)); // (100 - 12) / 2 + 0
    CHECK(layout.glyphs[1].offset.x == doctest::Approx(52.0f)); // (100 - 12) / 2 + 8
    // Vertical positions are untouched by a Top alignment.
    CHECK(layout.glyphs[0].offset.y == doctest::Approx(0.0f));
    CHECK(layout.glyphs[1].offset.y == doctest::Approx(12.0f));
}

// ------------------------------------------------------------------
// Vertical alignment
// ------------------------------------------------------------------

TEST_CASE("vertical alignment: T/M/B at lineSpacing 1 is unchanged") {
    auto font = makeFixtureFont();
    auto layout = text::layoutText(font, "AB", 32.0f, 0.0f, 1.0f, 0.0f);
    const glm::vec2 box{100.0f, 100.0f};
    CHECK(layout.boundingBox.y == doctest::Approx(40.0f));

    CHECK(text::computeAlignedLineOrigins(
              layout, params(text::HAlign::Left, text::VAlign::Top, box)).offsetY
          == doctest::Approx(0.0f));
    CHECK(text::computeAlignedLineOrigins(
              layout, params(text::HAlign::Left, text::VAlign::Middle, box)).offsetY
          == doctest::Approx(30.0f)); // (100 - 40) / 2
    CHECK(text::computeAlignedLineOrigins(
              layout, params(text::HAlign::Left, text::VAlign::Bottom, box)).offsetY
          == doctest::Approx(60.0f)); // 100 - 40
}

TEST_CASE("vertical alignment: lineSpacing no longer inflates the block height") {
    // The fix: boundingBox.y is the advance box (N * spaced lineHeight). For
    // alignment the block ends at the last line's em box instead, so the leftover
    // leading is not dumped below the text.
    auto font = makeFixtureFont();
    auto layout = text::layoutText(font, "A\nB", 32.0f, 0.0f, 2.0f, 0.0f);

    CHECK(layout.boundingBox.y == doctest::Approx(160.0f)); // 2 lines * 80
    // block = (2 - 1) * 80 + 40 = 120

    auto middle = text::computeAlignedLineOrigins(
        layout, params(text::HAlign::Left, text::VAlign::Middle, {100.0f, 200.0f}, 2.0f));
    CHECK(middle.offsetY == doctest::Approx(40.0f)); // (200 - 120) / 2; was 20

    auto bottom = text::computeAlignedLineOrigins(
        layout, params(text::HAlign::Left, text::VAlign::Bottom, {100.0f, 200.0f}, 2.0f));
    CHECK(bottom.offsetY == doctest::Approx(80.0f)); // 200 - 120; was 40
}

TEST_CASE("vertical alignment: line gap is excluded from the trailing line") {
    auto font = makeFixtureFont();
    font.metadata.lineHeight = 48.0f; // 8 px of line gap on top of the 40 px em box
    auto layout = text::layoutText(font, "A\nB", 32.0f, 0.0f, 1.0f, 0.0f);
    CHECK(layout.boundingBox.y == doctest::Approx(96.0f)); // 2 * 48

    auto m = text::computeLineMetrics(font, 32.0f, 1.0f);
    text::AlignParams p;
    p.horizontal = text::HAlign::Left;
    p.vertical = text::VAlign::Middle;
    p.contentSize = {100.0f, 200.0f};
    p.lineHeight = m.lineHeight;
    p.singleLineHeight = m.singleLineHeight;

    // block = 96 - 48 + 40 = 88
    CHECK(text::computeAlignedLineOrigins(layout, p).offsetY == doctest::Approx(56.0f));
}

TEST_CASE("vertical alignment: single line never collapses to zero height") {
    auto font = makeFixtureFont();
    auto layout = text::layoutText(font, "A", 32.0f, 0.0f, 1.0f, 0.0f);

    auto offsets = text::computeAlignedLineOrigins(
        layout, params(text::HAlign::Left, text::VAlign::Middle, {100.0f, 100.0f}));
    CHECK(offsets.offsetY == doctest::Approx(30.0f)); // (100 - 40) / 2, not 50
}

// ------------------------------------------------------------------
// applyAlignment
// ------------------------------------------------------------------

TEST_CASE("applyAlignment: combined H + V shifts every glyph") {
    auto font = makeFixtureFont();
    auto layout = text::layoutText(font, "AB\nA", 32.0f, 0.0f, 1.0f, 0.0f);

    text::applyAlignment(layout, params(text::HAlign::Center, text::VAlign::Middle,
                                        {100.0f, 200.0f}));

    // block = 80 - 40 + 40 = 80 => offsetY = (200 - 80) / 2 = 60
    REQUIRE(layout.glyphs.size() == 3);
    CHECK(layout.glyphs[0].offset.x == doctest::Approx(42.0f));
    CHECK(layout.glyphs[0].offset.y == doctest::Approx(60.0f));
    CHECK(layout.glyphs[1].offset.x == doctest::Approx(50.0f)); // 8 + 42
    CHECK(layout.glyphs[1].offset.y == doctest::Approx(60.0f));
    CHECK(layout.glyphs[2].offset.x == doctest::Approx(46.0f)); // second line
    CHECK(layout.glyphs[2].offset.y == doctest::Approx(100.0f)); // 40 + 60

    // boundingBox describes the un-aligned layout and is deliberately untouched.
    CHECK(layout.boundingBox.x == doctest::Approx(16.0f));
    CHECK(layout.boundingBox.y == doctest::Approx(80.0f));
}

TEST_CASE("applyAlignment: Left/Top leaves the layout untouched") {
    auto font = makeFixtureFont();
    auto layout = text::layoutText(font, "AB", 32.0f, 0.0f, 1.0f, 0.0f);
    const auto before = layout.glyphs;

    text::applyAlignment(layout, params(text::HAlign::Left, text::VAlign::Top,
                                        {100.0f, 100.0f}));

    REQUIRE(layout.glyphs.size() == before.size());
    for (size_t i = 0; i < before.size(); ++i)
    {
        CHECK(layout.glyphs[i].offset.x == doctest::Approx(before[i].offset.x));
        CHECK(layout.glyphs[i].offset.y == doctest::Approx(before[i].offset.y));
    }
}

TEST_CASE("applyAlignment: empty layout is a no-op") {
    auto font = makeFixtureFont();
    auto layout = text::layoutText(font, "", 32.0f, 0.0f, 1.0f, 0.0f);
    REQUIRE(layout.glyphs.empty());

    auto offsets = text::computeAlignedLineOrigins(
        layout, params(text::HAlign::Center, text::VAlign::Middle, {100.0f, 100.0f}));
    CHECK(offsets.lines.empty());
    CHECK(offsets.lineOffsetX.empty());
    CHECK(offsets.offsetY == doctest::Approx(0.0f));

    text::applyAlignment(layout, params(text::HAlign::Center, text::VAlign::Middle,
                                        {100.0f, 100.0f}));
    CHECK(layout.glyphs.empty());
}

TEST_CASE("applyAlignment: runs after applyEllipsis") {
    auto font = makeFixtureFont();
    // "Barracks" is 64 px; a 32 px rect keeps "Bar" (24 px) + the ellipsis glyph.
    auto layout = text::layoutText(font, "Barracks", 32.0f, 0.0f, 1.0f, 0.0f);
    text::applyEllipsis(layout, font, 32.0f, 32.0f, 0.0f);
    REQUIRE(layout.glyphs.size() == 4);
    CHECK(layout.glyphs.back().codepoint == 0x2026);

    text::applyAlignment(layout, params(text::HAlign::Center, text::VAlign::Top,
                                        {100.0f, 100.0f}));

    // One 32 px line => (100 - 32) / 2 = 34 for every glyph.
    CHECK(layout.glyphs[0].offset.x == doctest::Approx(34.0f));
    CHECK(layout.glyphs[1].offset.x == doctest::Approx(42.0f));
    CHECK(layout.glyphs[2].offset.x == doctest::Approx(50.0f));
    CHECK(layout.glyphs[3].offset.x == doctest::Approx(58.0f));
}

// ------------------------------------------------------------------
// layoutText word wrap (previously untested)
// ------------------------------------------------------------------

TEST_CASE("word wrap: a long word hard-breaks mid-word") {
    auto font = makeFixtureFont();
    // No spaces, so there is no word boundary to wrap at: "abcd" at 8 px each in a
    // 20 px box breaks after "ab".
    auto layout = text::layoutText(font, "abcd", 32.0f, 20.0f, 1.0f, 0.0f);

    REQUIRE(layout.glyphs.size() == 4);
    CHECK(layout.glyphs[0].offset.x == doctest::Approx(0.0f));
    CHECK(layout.glyphs[1].offset.x == doctest::Approx(8.0f));
    CHECK(layout.glyphs[2].offset.x == doctest::Approx(0.0f));
    CHECK(layout.glyphs[3].offset.x == doctest::Approx(8.0f));
    CHECK(layout.glyphs[0].lineY == doctest::Approx(0.0f));
    CHECK(layout.glyphs[1].lineY == doctest::Approx(0.0f));
    CHECK(layout.glyphs[2].lineY == doctest::Approx(40.0f));
    CHECK(layout.glyphs[3].lineY == doctest::Approx(40.0f));
    CHECK(layout.boundingBox.x == doctest::Approx(16.0f));
    CHECK(layout.boundingBox.y == doctest::Approx(80.0f));

    CHECK(text::partitionLines(layout.glyphs).size() == 2);
}

TEST_CASE("word wrap: glyphs moved to the next line carry their new lineY") {
    // "ab cde" in a 34 px box: 'c' is placed on line 0, then 'd' overflows and the
    // whole word moves down. The moved glyph's lineY has to move with it, otherwise
    // partitionLines (and applyEllipsis, and alignment) groups it with line 0.
    auto font = makeFixtureFont();
    auto layout = text::layoutText(font, "ab cde", 32.0f, 34.0f, 1.0f, 0.0f);

    REQUIRE(layout.glyphs.size() == 6); // a b <space> c d e
    CHECK(layout.glyphs[2].codepoint == ' ');

    CHECK(layout.glyphs[3].offset.y == doctest::Approx(40.0f));
    CHECK(layout.glyphs[3].lineY == doctest::Approx(40.0f)); // the moved 'c'
    CHECK(layout.glyphs[4].lineY == doctest::Approx(40.0f));
    CHECK(layout.glyphs[5].lineY == doctest::Approx(40.0f));

    auto lines = text::partitionLines(layout.glyphs);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].count == 3);
    CHECK(lines[1].count == 3);
    CHECK(lines[1].start == 3);

    // Known, out-of-scope quirk kept as-is: the wrapped line is indented by the
    // width of the trailing space it wrapped after ('c' lands at 8, not 0).
    CHECK(layout.glyphs[3].offset.x == doctest::Approx(8.0f));
}

TEST_CASE("word wrap: boundingBox.x reports the widest completed line") {
    // Line 1 ("abcdefghij ") is 80 px wide, then "kl" wraps to a 24 px line 2.
    // The old wrap code compared the wrong quantity and reported line 0's 32 px.
    auto font = makeFixtureFont();
    auto layout = text::layoutText(font, "abcd\nabcdefghij kl", 32.0f, 100.0f, 1.0f, 0.0f);

    CHECK(layout.boundingBox.x == doctest::Approx(80.0f));
    CHECK(layout.boundingBox.y == doctest::Approx(120.0f)); // 3 lines * 40

    auto lines = text::partitionLines(layout.glyphs);
    REQUIRE(lines.size() == 3);
    CHECK(lines[0].count == 4);  // abcd
    CHECK(lines[1].count == 11); // abcdefghij + space
    CHECK(lines[2].count == 2);  // kl
    CHECK(lines[2].lineY == doctest::Approx(80.0f));
}

TEST_CASE("word wrap: disabled when maxWidth is zero") {
    auto font = makeFixtureFont();
    auto layout = text::layoutText(font, "abcdefghij", 32.0f, 0.0f, 1.0f, 0.0f);

    CHECK(text::partitionLines(layout.glyphs).size() == 1);
    CHECK(layout.boundingBox.x == doctest::Approx(80.0f));
}

TEST_CASE("word wrap: wrapped lines align independently") {
    auto font = makeFixtureFont();
    auto layout = text::layoutText(font, "abcd", 32.0f, 20.0f, 1.0f, 0.0f);

    text::applyAlignment(layout, params(text::HAlign::Right, text::VAlign::Top,
                                        {100.0f, 100.0f}));

    // Both wrapped lines are 16 px of ink => both shift by 100 - 16 = 84.
    CHECK(layout.glyphs[0].offset.x == doctest::Approx(84.0f));
    CHECK(layout.glyphs[1].offset.x == doctest::Approx(92.0f));
    CHECK(layout.glyphs[2].offset.x == doctest::Approx(84.0f));
    CHECK(layout.glyphs[3].offset.x == doctest::Approx(92.0f));
}

}
