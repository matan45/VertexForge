#include <doctest.h>
#include <text/RichTextParser.hpp>
#include <text/TextLayout.hpp>
#include <resource/Types.hpp>
#include <algorithm>

// Rich text markup parsing (BBCode subset) + LayoutGlyph::charIndex mapping.
// Uses the same synthetic uniform-width font fixture approach as the
// ellipsis tests: baseFontSize 32, every glyph advances 8 px.

namespace
{
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

        for (uint32_t cp = 0x20; cp < 0x7F; ++cp)
        {
            pushGlyph(cp);
        }
        pushGlyph(0x2026);

        std::sort(font.glyphs.begin(), font.glyphs.end(),
                  [](const resource::GlyphData& a, const resource::GlyphData& b) {
                      return a.codepoint < b.codepoint;
                  });

        return font;
    }
}

TEST_SUITE("RichTextParser") {

TEST_CASE("plain text passes through with default style") {
    auto result = text::parseRichText("Hello world");
    CHECK(result.strippedText == "Hello world");
    REQUIRE(result.perCodepoint.size() == 11);
    for (const auto& s : result.perCodepoint)
    {
        CHECK(s.styleFlags == 0);
        CHECK_FALSE(s.hasColor);
    }
}

TEST_CASE("bold and italic spans set styleFlags") {
    auto result = text::parseRichText("a[b]b[/b]c[i]d[/i]");
    CHECK(result.strippedText == "abcd");
    REQUIRE(result.perCodepoint.size() == 4);
    CHECK(result.perCodepoint[0].styleFlags == 0);
    CHECK(result.perCodepoint[1].styleFlags == 0x1);
    CHECK(result.perCodepoint[2].styleFlags == 0);
    CHECK(result.perCodepoint[3].styleFlags == 0x2);
}

TEST_CASE("color span parses #RRGGBB and #RRGGBBAA") {
    auto result = text::parseRichText("[color=#FF0000]r[/color][color=#00FF0080]g[/color]");
    CHECK(result.strippedText == "rg");
    REQUIRE(result.perCodepoint.size() == 2);
    CHECK(result.perCodepoint[0].hasColor);
    CHECK(result.perCodepoint[0].color.r == doctest::Approx(1.0f));
    CHECK(result.perCodepoint[0].color.g == doctest::Approx(0.0f));
    CHECK(result.perCodepoint[0].color.a == doctest::Approx(1.0f));
    CHECK(result.perCodepoint[1].hasColor);
    CHECK(result.perCodepoint[1].color.g == doctest::Approx(1.0f));
    CHECK(result.perCodepoint[1].color.a == doctest::Approx(128.0f / 255.0f));
}

TEST_CASE("tags nest and unwind correctly") {
    auto result = text::parseRichText("[b][color=#FF0000]x[/color]y[/b]z");
    CHECK(result.strippedText == "xyz");
    REQUIRE(result.perCodepoint.size() == 3);
    CHECK(result.perCodepoint[0].styleFlags == 0x1);
    CHECK(result.perCodepoint[0].hasColor);
    CHECK(result.perCodepoint[1].styleFlags == 0x1);
    CHECK_FALSE(result.perCodepoint[1].hasColor);
    CHECK(result.perCodepoint[2].styleFlags == 0);
}

TEST_CASE("unmatched close tag renders literally") {
    auto result = text::parseRichText("a[/b]c");
    CHECK(result.strippedText == "a[/b]c");
    CHECK(result.perCodepoint.size() == 6);
}

TEST_CASE("unknown and malformed tags render literally") {
    CHECK(text::parseRichText("[blah]x[/blah]").strippedText == "[blah]x[/blah]");
    CHECK(text::parseRichText("[color=red]x[/color]").strippedText == "[color=red]x[/color]");
    CHECK(text::parseRichText("[color=#GG0000]x").strippedText == "[color=#GG0000]x");
    CHECK(text::parseRichText("trailing [").strippedText == "trailing [");
}

TEST_CASE("[[ escapes a literal bracket") {
    auto result = text::parseRichText("[[b]");
    CHECK(result.strippedText == "[b]");
    CHECK(result.perCodepoint.size() == 3);
}

TEST_CASE("[icon=...] is consumed without output") {
    auto result = text::parseRichText("a[icon=sword]b");
    CHECK(result.strippedText == "ab");
    CHECK(result.perCodepoint.size() == 2);
}

// ---- VK-1635 effect tags ----

TEST_CASE("[outline] carries a colour and an optional width") {
    auto result = text::parseRichText("a[outline=#FF0000,2.5]b[/outline]c");
    CHECK(result.strippedText == "abc");
    REQUIRE(result.perCodepoint.size() == 3);

    CHECK_FALSE(result.perCodepoint[0].hasOutline);
    CHECK(result.perCodepoint[1].hasOutline);
    CHECK(result.perCodepoint[1].outlineColor.r == doctest::Approx(1.0f));
    CHECK(result.perCodepoint[1].outlineColor.g == doctest::Approx(0.0f));
    CHECK(result.perCodepoint[1].outlineColor.a == doctest::Approx(1.0f));
    CHECK(result.perCodepoint[1].outlineWidth == doctest::Approx(2.5f));
    CHECK_FALSE(result.perCodepoint[2].hasOutline);

    // Width omitted: left at 0 so applyTo() can inherit the label's own width.
    auto noWidth = text::parseRichText("[outline=#00FF0080]x[/outline]");
    REQUIRE(noWidth.perCodepoint.size() == 1);
    CHECK(noWidth.perCodepoint[0].hasOutline);
    CHECK(noWidth.perCodepoint[0].outlineWidth == doctest::Approx(0.0f));
    CHECK(noWidth.perCodepoint[0].outlineColor.a == doctest::Approx(128.0f / 255.0f));
}

TEST_CASE("[shadow] works bare and with a colour plus offset") {
    auto bare = text::parseRichText("a[shadow]b[/shadow]");
    CHECK(bare.strippedText == "ab");
    REQUIRE(bare.perCodepoint.size() == 2);
    CHECK_FALSE(bare.perCodepoint[0].hasShadow);
    CHECK(bare.perCodepoint[1].hasShadow);
    // Nothing given, so applyTo() resolves both colour and offset later.
    CHECK(bare.perCodepoint[1].shadowOffset.x == doctest::Approx(0.0f));
    CHECK(bare.perCodepoint[1].shadowOffset.y == doctest::Approx(0.0f));

    auto full = text::parseRichText("[shadow=#000000FF,2,-3]x[/shadow]");
    REQUIRE(full.perCodepoint.size() == 1);
    CHECK(full.perCodepoint[0].hasShadow);
    CHECK(full.perCodepoint[0].shadowColor.a == doctest::Approx(1.0f));
    CHECK(full.perCodepoint[0].shadowOffset.x == doctest::Approx(2.0f));
    CHECK(full.perCodepoint[0].shadowOffset.y == doctest::Approx(-3.0f));
}

TEST_CASE("[glow] carries a colour and an optional range") {
    auto result = text::parseRichText("[glow=#FFFF00,4]x[/glow]y");
    CHECK(result.strippedText == "xy");
    REQUIRE(result.perCodepoint.size() == 2);
    CHECK(result.perCodepoint[0].hasGlow);
    CHECK(result.perCodepoint[0].glowColor.r == doctest::Approx(1.0f));
    CHECK(result.perCodepoint[0].glowColor.b == doctest::Approx(0.0f));
    CHECK(result.perCodepoint[0].glowRange == doctest::Approx(4.0f));
    CHECK_FALSE(result.perCodepoint[1].hasGlow);
}

TEST_CASE("effect tags nest independently of each other and of [b]/[color]") {
    auto result = text::parseRichText("[outline=#FF0000,1][b]a[shadow]b[/shadow]c[/b][/outline]d");
    CHECK(result.strippedText == "abcd");
    REQUIRE(result.perCodepoint.size() == 4);

    for (int i = 0; i < 3; ++i)
    {
        CAPTURE(i);
        CHECK(result.perCodepoint[i].hasOutline);
        CHECK(result.perCodepoint[i].styleFlags == 0x1);
    }
    CHECK_FALSE(result.perCodepoint[0].hasShadow);
    CHECK(result.perCodepoint[1].hasShadow);
    CHECK_FALSE(result.perCodepoint[2].hasShadow);
    CHECK_FALSE(result.perCodepoint[3].hasOutline);
    CHECK(result.perCodepoint[3].styleFlags == 0);

    // Inner outline wins while it is open, then the outer one comes back.
    auto nested = text::parseRichText("[outline=#FF0000,1]a[outline=#0000FF,3]b[/outline]c[/outline]");
    REQUIRE(nested.perCodepoint.size() == 3);
    CHECK(nested.perCodepoint[0].outlineWidth == doctest::Approx(1.0f));
    CHECK(nested.perCodepoint[1].outlineWidth == doctest::Approx(3.0f));
    CHECK(nested.perCodepoint[1].outlineColor.b == doctest::Approx(1.0f));
    CHECK(nested.perCodepoint[2].outlineWidth == doctest::Approx(1.0f));
}

TEST_CASE("malformed effect tags render literally") {
    // A number that only partly parses must be rejected outright rather than read as 2 -
    // silently dropping the "px" would give an outline the author never asked for.
    CHECK(text::parseRichText("[outline=#FF0000,2px]x").strippedText == "[outline=#FF0000,2px]x");
    CHECK(text::parseRichText("[outline=red,2]x").strippedText == "[outline=red,2]x");
    CHECK(text::parseRichText("[outline=#FF0000,]x").strippedText == "[outline=#FF0000,]x");
    // One number too many for the tag.
    CHECK(text::parseRichText("[outline=#FF0000,1,2]x").strippedText == "[outline=#FF0000,1,2]x");
    CHECK(text::parseRichText("[glow=#FF0000,1,2]x").strippedText == "[glow=#FF0000,1,2]x");
    CHECK(text::parseRichText("[shadow=#FF0000,1,2,3]x").strippedText == "[shadow=#FF0000,1,2,3]x");
    // Unmatched closers behave like [/b] does.
    CHECK(text::parseRichText("a[/outline]b").strippedText == "a[/outline]b");
    CHECK(text::parseRichText("a[/shadow]b").strippedText == "a[/shadow]b");
    CHECK(text::parseRichText("a[/glow]b").strippedText == "a[/glow]b");
}

TEST_CASE("multibyte UTF-8 counts one style entry per codepoint") {
    // U+00E9 (e-acute) is 2 bytes; U+2026 is 3 bytes
    std::string markup = "[b]\xC3\xA9\xE2\x80\xA6[/b]x";
    auto result = text::parseRichText(markup);
    CHECK(result.strippedText == "\xC3\xA9\xE2\x80\xA6x");
    REQUIRE(result.perCodepoint.size() == 3);
    CHECK(result.perCodepoint[0].styleFlags == 0x1);
    CHECK(result.perCodepoint[1].styleFlags == 0x1);
    CHECK(result.perCodepoint[2].styleFlags == 0);
}

TEST_CASE("layoutText charIndex matches parser codepoint enumeration") {
    auto font = makeFixtureFont();
    auto rich = text::parseRichText("ab[b]cd[/b]");
    REQUIRE(rich.strippedText == "abcd");

    auto layout = text::layoutText(font, rich.strippedText, 32.0f, 0.0f, 1.0f, 0.0f);
    REQUIRE(layout.glyphs.size() == 4);
    for (size_t i = 0; i < layout.glyphs.size(); ++i)
    {
        CHECK(layout.glyphs[i].charIndex == i);
        const auto& span = rich.perCodepoint[layout.glyphs[i].charIndex];
        bool expectBold = (i >= 2);
        CHECK((span.styleFlags & 0x1u) == (expectBold ? 0x1u : 0x0u));
    }
}

TEST_CASE("charIndex counts whitespace and newline codepoints") {
    auto font = makeFixtureFont();
    // "a b\nc": space is a glyph in the fixture, '\n' is consumed by layout
    auto layout = text::layoutText(font, "a b\nc", 32.0f, 0.0f, 1.0f, 0.0f);
    REQUIRE(layout.glyphs.size() == 4); // 'a', ' ', 'b', 'c'
    CHECK(layout.glyphs[0].charIndex == 0);
    CHECK(layout.glyphs[1].charIndex == 1);
    CHECK(layout.glyphs[2].charIndex == 2);
    CHECK(layout.glyphs[3].charIndex == 4); // '\n' consumed index 3
}

TEST_CASE("ellipsis glyph gets the sentinel charIndex") {
    auto font = makeFixtureFont();
    // 8 glyphs = 64 px at size 32; truncate to 40 px
    auto layout = text::layoutText(font, "Barracks", 32.0f, 0.0f, 1.0f, 0.0f);
    REQUIRE(layout.glyphs.size() == 8);

    text::applyEllipsis(layout, font, 32.0f, 40.0f, 0.0f);
    REQUIRE(!layout.glyphs.empty());
    CHECK(layout.glyphs.back().codepoint == 0x2026);
    CHECK(layout.glyphs.back().charIndex == UINT32_MAX);
    // surviving glyphs keep their original indices
    for (size_t i = 0; i + 1 < layout.glyphs.size(); ++i)
    {
        CHECK(layout.glyphs[i].charIndex == i);
    }
}

}
