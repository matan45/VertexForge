#include <doctest.h>
#include <text/TextLayout.hpp>
#include <resource/Types.hpp>
#include <algorithm>

// ============================================================
// VK-1327: TextLayout ellipsis truncation unit tests
// ============================================================
//
// These tests exercise text::applyEllipsis with a synthetic FontData
// fixture so we don't depend on Vulkan, the asset DB, or any real font.
// Glyphs are uniform-width "boxes" to keep arithmetic obvious:
//   baseFontSize = 32, every glyph advanceX = 8 px (atlasWidth = 8 px).
// At fontSize = 32, one glyph advances 8 px. "Barracks" = 8 glyphs = 64 px.

namespace
{
    resource::FontData makeFixtureFont(bool includeEllipsisCodepoint)
    {
        resource::FontData font;
        font.metadata.baseFontSize = 32;
        font.metadata.lineHeight = 40.0f;
        font.metadata.ascender = 32.0f;
        font.metadata.descender = -8.0f;

        font.atlas.width = 256;
        font.atlas.height = 256;
        font.atlas.format = resource::FontAtlasFormat::GRAYSCALE_8;

        auto pushGlyph = [&](uint32_t cp) {
            resource::GlyphData g;
            g.codepoint = cp;
            g.advanceX = 8.0f;
            g.advanceY = 0.0f;
            g.bearingX = 0.0f;
            g.bearingY = 32.0f;
            g.glyphWidth = 8.0f;
            g.glyphHeight = 32.0f;
            g.atlasX = 0;
            g.atlasY = 0;
            g.atlasWidth = 8;
            g.atlasHeight = 32;
            font.glyphs.push_back(g);
        };

        // ASCII printable range we care about for these tests.
        for (uint32_t cp = 0x20; cp < 0x7F; ++cp)
        {
            pushGlyph(cp);
        }
        if (includeEllipsisCodepoint)
        {
            pushGlyph(0x2026); // U+2026 HORIZONTAL ELLIPSIS
        }

        // findGlyph relies on glyphs being sorted by codepoint (lower_bound).
        std::sort(font.glyphs.begin(), font.glyphs.end(),
                  [](const resource::GlyphData& a, const resource::GlyphData& b) {
                      return a.codepoint < b.codepoint;
                  });

        return font;
    }
}

TEST_SUITE("TextLayoutEllipsis") {

TEST_CASE("applyEllipsis: text within rect is unchanged") {
    auto font = makeFixtureFont(true);
    // "Hi" at 32 px = 16 px; rect is 64 px, plenty of room.
    auto layout = text::layoutText(font, "Hi", 32.0f, 0.0f, 1.0f, 0.0f);
    REQUIRE(layout.glyphs.size() == 2);

    const auto before = layout.glyphs;
    text::applyEllipsis(layout, font, 32.0f, 64.0f, 0.0f);

    CHECK(layout.glyphs.size() == before.size());
    CHECK(layout.glyphs.back().codepoint == 'i');
}

TEST_CASE("applyEllipsis: truncates long line and appends U+2026") {
    auto font = makeFixtureFont(true);
    // "Barracks" = 8 glyphs * 8 px = 64 px. Rect = 32 px.
    auto layout = text::layoutText(font, "Barracks", 32.0f, 0.0f, 1.0f, 0.0f);
    REQUIRE(layout.glyphs.size() == 8);

    text::applyEllipsis(layout, font, 32.0f, 32.0f, 0.0f);

    REQUIRE_FALSE(layout.glyphs.empty());
    // Must end with ellipsis codepoint
    CHECK(layout.glyphs.back().codepoint == 0x2026u);
    // Must be shorter than the original
    CHECK(layout.glyphs.size() < 8);
    // Must not exceed the rect: rightmost glyph's right edge <= 32 px
    float rightEdge = layout.glyphs.back().offset.x + layout.glyphs.back().size.x;
    CHECK(rightEdge <= 32.0f + 0.01f);
    // boundingBox.x should also be within rect
    CHECK(layout.boundingBox.x <= 32.0f + 0.01f);
}

TEST_CASE("applyEllipsis: falls back to three dots when U+2026 absent") {
    auto font = makeFixtureFont(false); // no U+2026 in atlas
    auto layout = text::layoutText(font, "Barracks", 32.0f, 0.0f, 1.0f, 0.0f);
    REQUIRE(layout.glyphs.size() == 8);

    text::applyEllipsis(layout, font, 32.0f, 40.0f, 0.0f);

    REQUIRE(layout.glyphs.size() >= 3);
    // Last three glyphs must all be '.'
    size_t n = layout.glyphs.size();
    CHECK(layout.glyphs[n - 1].codepoint == '.');
    CHECK(layout.glyphs[n - 2].codepoint == '.');
    CHECK(layout.glyphs[n - 3].codepoint == '.');
}

TEST_CASE("applyEllipsis: maxWidth = 0 leaves layout untouched") {
    auto font = makeFixtureFont(true);
    auto layout = text::layoutText(font, "Barracks", 32.0f, 0.0f, 1.0f, 0.0f);
    const size_t originalCount = layout.glyphs.size();

    text::applyEllipsis(layout, font, 32.0f, 0.0f, 0.0f);

    CHECK(layout.glyphs.size() == originalCount);
}

TEST_CASE("applyEllipsis: varied per-glyph bearingY still groups as one line") {
    // Regression: a real font's bearingY varies per glyph ('l' tall, 'a' short,
    // 'g' has descender), so glyph.offset.y differs across glyphs on the same
    // line. Partitioning by offset.y (instead of lineY) would split one visual
    // line into many fake lines, defeating truncation entirely.
    resource::FontData font;
    font.metadata.baseFontSize = 32;
    font.metadata.lineHeight = 40.0f;
    font.metadata.ascender = 32.0f;
    font.atlas.width = 256;
    font.atlas.height = 256;

    auto pushGlyph = [&](uint32_t cp, float bearingY) {
        resource::GlyphData g;
        g.codepoint = cp;
        g.advanceX = 8.0f;
        g.bearingY = bearingY;
        g.atlasWidth = 8;
        g.atlasHeight = 32;
        font.glyphs.push_back(g);
    };
    // Mimic real-font bearings: caps tall, x-height mid, descender lower.
    pushGlyph('B', 24.0f);
    pushGlyph('a', 18.0f);
    pushGlyph('r', 18.0f);
    pushGlyph('c', 18.0f);
    pushGlyph('k', 24.0f);
    pushGlyph('s', 18.0f);
    pushGlyph(0x2026, 8.0f);
    std::sort(font.glyphs.begin(), font.glyphs.end(),
              [](const resource::GlyphData& a, const resource::GlyphData& b) {
                  return a.codepoint < b.codepoint;
              });

    auto layout = text::layoutText(font, "Barracks", 32.0f, 0.0f, 1.0f, 0.0f);
    REQUIRE(layout.glyphs.size() == 8);

    // Confirm precondition: offset.y values DO vary across same-line glyphs.
    float minY = layout.glyphs[0].offset.y, maxY = minY;
    for (const auto& g : layout.glyphs) {
        minY = std::min(minY, g.offset.y);
        maxY = std::max(maxY, g.offset.y);
    }
    REQUIRE(maxY - minY > 0.1f);

    // Confirm lineY is uniform across the line.
    for (const auto& g : layout.glyphs) {
        CHECK(g.lineY == doctest::Approx(layout.glyphs[0].lineY));
    }

    text::applyEllipsis(layout, font, 32.0f, 32.0f, 0.0f);

    // Must truncate (would not happen if partitioner mistakenly per-glyph).
    REQUIRE(layout.glyphs.size() < 8);
    CHECK(layout.glyphs.back().codepoint == 0x2026u);
}

TEST_CASE("applyEllipsis: rect too narrow for ellipsis drops entire line") {
    auto font = makeFixtureFont(true);
    auto layout = text::layoutText(font, "Barracks", 32.0f, 0.0f, 1.0f, 0.0f);
    REQUIRE_FALSE(layout.glyphs.empty());

    // Ellipsis glyph advance is 8 px at fontSize=32; ask for only 4 px.
    text::applyEllipsis(layout, font, 32.0f, 4.0f, 0.0f);

    CHECK(layout.glyphs.empty());
}

TEST_CASE("applyEllipsis: multi-line, only the long line is ellipsized") {
    auto font = makeFixtureFont(true);

    // Build a 2-line layout manually so this test isolates applyEllipsis from
    // any quirks in layoutText's word-wrap. Line 0 ("Hi") is 16 px wide and
    // fits in 32 px. Line 1 ("Barracks") is 64 px wide and must be truncated.
    text::LayoutResult layout;
    auto pushGlyph = [&](uint32_t cp, float x, float lineY) {
        const auto* g = font.findGlyph(cp);
        REQUIRE(g != nullptr);
        // Mirror layoutText's offset.y formula so the synthetic layout matches
        // what the real layout would produce, ascender - bearingY apart.
        const float ascender = 32.0f;
        const float scale = 1.0f;
        text::LayoutGlyph lg;
        lg.offset = glm::vec2(x, lineY + (ascender - g->bearingY) * scale);
        lg.size = glm::vec2(g->atlasWidth, g->atlasHeight);
        lg.uvRect = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
        lg.codepoint = cp;
        lg.lineY = lineY;
        layout.glyphs.push_back(lg);
    };
    const float line1Y = 0.0f;
    const float line2Y = 40.0f;
    pushGlyph('H', 0.0f, line1Y);
    pushGlyph('i', 8.0f, line1Y);
    pushGlyph('B', 0.0f, line2Y);
    pushGlyph('a', 8.0f, line2Y);
    pushGlyph('r', 16.0f, line2Y);
    pushGlyph('r', 24.0f, line2Y);
    pushGlyph('a', 32.0f, line2Y);
    pushGlyph('c', 40.0f, line2Y);
    pushGlyph('k', 48.0f, line2Y);
    pushGlyph('s', 56.0f, line2Y);
    layout.boundingBox = glm::vec2(64.0f, line2Y + 40.0f);

    text::applyEllipsis(layout, font, 32.0f, 32.0f, 0.0f);

    // Count lines by lineY (the line origin), not offset.y — offset.y varies
    // per glyph by bearingY even within a single line.
    auto countLines = [](const text::LayoutResult& l) {
        if (l.glyphs.empty()) return size_t{0};
        size_t lines = 1;
        float y = l.glyphs[0].lineY;
        for (const auto& g : l.glyphs)
        {
            if (std::abs(g.lineY - y) > 0.1f) { lines++; y = g.lineY; }
        }
        return lines;
    };

    REQUIRE(countLines(layout) == 2);
    // The first line ("Hi") should be untouched
    CHECK(layout.glyphs[0].codepoint == 'H');
    CHECK(layout.glyphs[1].codepoint == 'i');
    // The last glyph should be the ellipsis (long line truncated)
    CHECK(layout.glyphs.back().codepoint == 0x2026u);
    CHECK(layout.glyphs.back().lineY == doctest::Approx(line2Y));
}

} // TEST_SUITE
