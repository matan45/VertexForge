#include <doctest.h>
#include <text/TextLayout.hpp>
#include <text/FontStyleFace.hpp>
#include <resource/Types.hpp>
#include <algorithm>
#include <string>
#include <vector>

// ============================================================
// VK-1636 — multi-face text layout.
//
// layoutText used to take exactly one FontData, so a [b] span could only ever be
// a shader trick. layoutTextStyled lets each codepoint pick a face out of a
// FaceSet indexed by the style bits (0 regular, 1 bold, 2 italic, 3 bold-italic).
//
// Two properties carry the whole feature:
//   1. A one-face set must reproduce the legacy layout EXACTLY. Every label in
//      the engine goes through this path, so an ulp of drift here is an engine-wide
//      regression that no GPU test would attribute correctly.
//   2. Faces in one block share one baseline and one line pitch. Letting each face
//      use its own ascender would jog the text vertically at every [b] boundary.
//
// Fixture arithmetic (mirrors test_textlayout_alignment.cpp):
//   baseFontSize 32, lineHeight 40, ascender 32, descender -8
//   every glyph: advanceX 8, bearingX 0, bearingY 32, atlas 8 x 32
// ============================================================

namespace
{
    void pushGlyph(resource::FontData& font, uint32_t cp, float advanceX, uint32_t atlasWidth)
    {
        resource::GlyphData g;
        g.codepoint = cp;
        g.advanceX = advanceX;
        g.advanceY = 0.0f;
        g.bearingX = 0.0f;
        g.bearingY = 32.0f;
        g.glyphWidth = static_cast<float>(atlasWidth);
        g.glyphHeight = 32.0f;
        g.atlasX = 0;
        g.atlasY = 0;
        g.atlasWidth = atlasWidth;
        g.atlasHeight = 32;
        font.glyphs.push_back(g);
    }

    void sortGlyphs(resource::FontData& font)
    {
        std::sort(font.glyphs.begin(), font.glyphs.end(),
                  [](const resource::GlyphData& a, const resource::GlyphData& b) {
                      return a.codepoint < b.codepoint;
                  });
    }

    resource::FontData makeFace(float advanceX = 8.0f, uint32_t atlasSize = 256)
    {
        resource::FontData font;
        font.metadata.baseFontSize = 32;
        font.metadata.lineHeight = 40.0f;
        font.metadata.ascender = 32.0f;
        font.metadata.descender = -8.0f;

        font.atlas.width = atlasSize;
        font.atlas.height = atlasSize;
        font.atlas.format = resource::FontAtlasFormat::GRAYSCALE_8;

        for (uint32_t cp = 0x20; cp < 0x7F; ++cp)
        {
            pushGlyph(font, cp, advanceX, 8);
        }
        pushGlyph(font, 0x2026, advanceX, 8);
        sortGlyphs(font);
        return font;
    }

    // Per-codepoint face selection for a whole string. Takes uint32_t so the
    // text::STYLE_* constants pass without a narrowing warning at every call site.
    std::vector<uint8_t> allFace(size_t count, uint32_t face)
    {
        return std::vector<uint8_t>(count, static_cast<uint8_t>(face));
    }
}

// ------------------------------------------------------------
// 1. Legacy identity
// ------------------------------------------------------------

TEST_CASE("a one-face set reproduces layoutText bit for bit")
{
    const resource::FontData face = makeFace();
    text::FaceSet set;
    set.faces[0] = &face;

    const std::string sample = "Hello wrapped world\nsecond line";

    for (const float maxWidth : {0.0f, 60.0f, 1000.0f})
    {
        for (const float lineSpacing : {1.0f, 1.35f})
        {
            for (const float letterSpacing : {0.0f, 1.5f})
            {
                const auto legacy = text::layoutText(face, sample, 32.0f, maxWidth,
                                                     lineSpacing, letterSpacing);
                const auto styled = text::layoutTextStyled(set, sample, {}, 32.0f, maxWidth,
                                                           lineSpacing, letterSpacing);

                REQUIRE(styled.glyphs.size() == legacy.glyphs.size());
                // Exact equality, not Approx: the whole point is that no arithmetic
                // changed shape. (a - b) * s and a * s - b * s differ in the last ulp,
                // which is why the baseline shift is an additive 0.0f rather than a
                // recomputed product.
                CHECK(styled.boundingBox.x == legacy.boundingBox.x);
                CHECK(styled.boundingBox.y == legacy.boundingBox.y);

                for (size_t i = 0; i < legacy.glyphs.size(); ++i)
                {
                    const auto& a = legacy.glyphs[i];
                    const auto& b = styled.glyphs[i];
                    CHECK(b.offset.x == a.offset.x);
                    CHECK(b.offset.y == a.offset.y);
                    CHECK(b.size.x == a.size.x);
                    CHECK(b.size.y == a.size.y);
                    CHECK(b.uvRect == a.uvRect);
                    CHECK(b.lineY == a.lineY);
                    CHECK(b.codepoint == a.codepoint);
                    CHECK(b.charIndex == a.charIndex);
                    CHECK(b.faceIndex == 0);
                }
            }
        }
    }
}

TEST_CASE("an oversized perCodepointFace span with no populated slots changes nothing")
{
    const resource::FontData face = makeFace();
    text::FaceSet set;
    set.faces[0] = &face;

    const std::string sample = "abc";
    const auto wanted = allFace(64, 3); // asks for bold-italic everywhere; none exists
    const auto legacy = text::layoutText(face, sample, 32.0f);
    const auto styled = text::layoutTextStyled(set, sample, wanted, 32.0f);

    REQUIRE(styled.glyphs.size() == legacy.glyphs.size());
    for (size_t i = 0; i < legacy.glyphs.size(); ++i)
    {
        CHECK(styled.glyphs[i].offset.x == legacy.glyphs[i].offset.x);
        CHECK(styled.glyphs[i].faceIndex == 0);
    }
}

// ------------------------------------------------------------
// 2. Face selection
// ------------------------------------------------------------

TEST_CASE("perCodepointFace picks the styled face and its advances")
{
    const resource::FontData regular = makeFace(8.0f);
    const resource::FontData bold = makeFace(12.0f); // wider, as a real Bold is
    text::FaceSet set;
    set.faces[0] = &regular;
    set.faces[text::STYLE_BOLD] = &bold;

    // "ab" regular, "cd" bold.
    const std::vector<uint8_t> wanted = {0, 0, text::STYLE_BOLD, text::STYLE_BOLD};
    const auto layout = text::layoutTextStyled(set, "abcd", wanted, 32.0f);

    REQUIRE(layout.glyphs.size() == 4);
    CHECK(layout.glyphs[0].faceIndex == 0);
    CHECK(layout.glyphs[1].faceIndex == 0);
    CHECK(layout.glyphs[2].faceIndex == text::STYLE_BOLD);
    CHECK(layout.glyphs[3].faceIndex == text::STYLE_BOLD);

    // Advances come from each glyph's own face: 8, 8, then 12.
    CHECK(layout.glyphs[0].offset.x == doctest::Approx(0.0f));
    CHECK(layout.glyphs[1].offset.x == doctest::Approx(8.0f));
    CHECK(layout.glyphs[2].offset.x == doctest::Approx(16.0f));
    CHECK(layout.glyphs[3].offset.x == doctest::Approx(28.0f));
}

TEST_CASE("unpopulated and unusable slots collapse to the base face")
{
    const resource::FontData regular = makeFace();

    SUBCASE("null slot")
    {
        text::FaceSet set;
        set.faces[0] = &regular;
        const auto layout = text::layoutTextStyled(set, "ab", allFace(2, text::STYLE_ITALIC), 32.0f);
        REQUIRE(layout.glyphs.size() == 2);
        CHECK(layout.glyphs[0].faceIndex == 0);
    }

    SUBCASE("non-null but unusable slot must not be emitted")
    {
        // A face whose atlas never uploaded, or whose metadata is degenerate. The
        // index still has to collapse to 0 or the renderer would key this glyph's
        // batch to an atlas that does not exist.
        resource::FontData broken = makeFace();
        broken.metadata.baseFontSize = 0;

        text::FaceSet set;
        set.faces[0] = &regular;
        set.faces[text::STYLE_ITALIC] = &broken;

        const auto layout = text::layoutTextStyled(set, "ab", allFace(2, text::STYLE_ITALIC), 32.0f);
        REQUIRE(layout.glyphs.size() == 2);
        CHECK(layout.glyphs[0].faceIndex == 0);
        CHECK(layout.glyphs[1].faceIndex == 0);
    }

    SUBCASE("an unusable base face yields no glyphs at all, as before")
    {
        resource::FontData broken = makeFace();
        broken.atlas.width = 0;
        text::FaceSet set;
        set.faces[0] = &broken;
        CHECK(text::layoutTextStyled(set, "ab", {}, 32.0f).glyphs.empty());
    }
}

TEST_CASE("a codepoint the styled face lacks is borrowed from the base face")
{
    // Realistic: the family's Bold was imported without Latin-1 Supplement. Dropping
    // the glyph would punch holes in bold text; falling back renders it unstyled.
    resource::FontData regular = makeFace();
    pushGlyph(regular, 0x00E9, 8.0f, 8); // é
    sortGlyphs(regular);

    const resource::FontData bold = makeFace(12.0f); // no é

    text::FaceSet set;
    set.faces[0] = &regular;
    set.faces[text::STYLE_BOLD] = &bold;

    const auto layout = text::layoutTextStyled(set, "a\xC3\xA9", allFace(2, text::STYLE_BOLD), 32.0f);
    REQUIRE(layout.glyphs.size() == 2);
    CHECK(layout.glyphs[0].faceIndex == text::STYLE_BOLD);
    CHECK(layout.glyphs[1].codepoint == 0x00E9);
    CHECK(layout.glyphs[1].faceIndex == 0);
}

// ------------------------------------------------------------
// 3. Shared baseline and line metrics
// ------------------------------------------------------------

TEST_CASE("all faces share one baseline, taken from the tallest ascender")
{
    resource::FontData regular = makeFace();
    resource::FontData tallBold = makeFace();
    tallBold.metadata.ascender = 40.0f; // 8px taller at scale 1
    tallBold.metadata.lineHeight = 48.0f;

    text::FaceSet set;
    set.faces[0] = &regular;
    set.faces[text::STYLE_BOLD] = &tallBold;

    const std::vector<uint8_t> wanted = {0, text::STYLE_BOLD};
    const auto layout = text::layoutTextStyled(set, "ab", wanted, 32.0f);
    REQUIRE(layout.glyphs.size() == 2);

    // Both glyphs have bearingY 32. Baseline sits at max ascender = 40.
    //   regular glyph: 40 - 32 = 8   (its own ascender would have given 0)
    //   bold glyph:    40 - 32 = 8
    // Equal offsets is the assertion that matters: unequal means the text jogs
    // vertically at the style boundary.
    CHECK(layout.glyphs[0].offset.y == doctest::Approx(8.0f));
    CHECK(layout.glyphs[1].offset.y == doctest::Approx(8.0f));

    SUBCASE("baselineShiftForFaceSet reports the same displacement")
    {
        CHECK(text::baselineShiftForFaceSet(set, 32.0f) == doctest::Approx(8.0f));
    }

    SUBCASE("line pitch is the max too")
    {
        const auto metrics = text::computeLineMetrics(set, 32.0f, 1.0f);
        CHECK(metrics.lineHeight == doctest::Approx(48.0f));
    }
}

TEST_CASE("the baseline shift is exactly zero when the base face is tallest")
{
    // This is the property the legacy-identity test depends on: a zero shift is
    // added as `+ 0.0f`, which is exact, so single-face layout cannot drift.
    const resource::FontData regular = makeFace();
    resource::FontData shortBold = makeFace();
    shortBold.metadata.ascender = 20.0f;

    text::FaceSet baseOnly;
    baseOnly.faces[0] = &regular;
    CHECK(text::baselineShiftForFaceSet(baseOnly, 32.0f) == 0.0f);
    CHECK(text::baselineShiftForFaceSet(baseOnly, 11.0f) == 0.0f);

    text::FaceSet withShort;
    withShort.faces[0] = &regular;
    withShort.faces[text::STYLE_BOLD] = &shortBold;
    CHECK(text::baselineShiftForFaceSet(withShort, 32.0f) == 0.0f);
}

TEST_CASE("computeLineMetrics over a one-face set equals the single-face overload")
{
    const resource::FontData face = makeFace();
    text::FaceSet set;
    set.faces[0] = &face;

    for (const float fontSize : {11.0f, 32.0f, 96.0f})
    {
        for (const float lineSpacing : {1.0f, 1.35f})
        {
            const auto single = text::computeLineMetrics(face, fontSize, lineSpacing);
            const auto multi = text::computeLineMetrics(set, fontSize, lineSpacing);
            CHECK(multi.lineHeight == single.lineHeight);
            CHECK(multi.singleLineHeight == single.singleLineHeight);
        }
    }
}

TEST_CASE("a face baked at a different size still lays out at the requested size")
{
    // A family is not guaranteed to be baked uniformly; scale is per face.
    resource::FontData regular = makeFace(8.0f);
    resource::FontData bigBold = makeFace(16.0f);
    bigBold.metadata.baseFontSize = 64; // baked twice as large
    bigBold.metadata.lineHeight = 80.0f;
    bigBold.metadata.ascender = 64.0f;

    text::FaceSet set;
    set.faces[0] = &regular;
    set.faces[text::STYLE_BOLD] = &bigBold;

    // At fontSize 32 the bold face scales by 0.5, so its 16px advance becomes 8px,
    // its 64 ascender becomes 32 — identical to regular. Nothing should shift.
    CHECK(text::baselineShiftForFaceSet(set, 32.0f) == doctest::Approx(0.0f));

    const std::vector<uint8_t> wanted = {0, text::STYLE_BOLD, 0};
    const auto layout = text::layoutTextStyled(set, "aba", wanted, 32.0f);
    REQUIRE(layout.glyphs.size() == 3);
    CHECK(layout.glyphs[1].offset.x == doctest::Approx(8.0f));
    // The third glyph proves the bold advance was scaled: an unscaled 16px advance
    // would put it at 24.
    CHECK(layout.glyphs[2].offset.x == doctest::Approx(16.0f));
}

// ------------------------------------------------------------
// 4. Kerning across a style boundary
// ------------------------------------------------------------

TEST_CASE("kerning is suppressed across a face change")
{
    resource::FontData regular = makeFace();
    regular.formatFlags = resource::FontFormatFlags::SDF_ENABLED |
                          resource::FontFormatFlags::KERNING_ENABLED;
    regular.kerningPairs.push_back({'a', 'b', -4.0f});

    resource::FontData bold = makeFace();
    bold.formatFlags = regular.formatFlags;
    bold.kerningPairs.push_back({'a', 'b', -4.0f});

    text::FaceSet set;
    set.faces[0] = &regular;
    set.faces[text::STYLE_BOLD] = &bold;

    SUBCASE("same face keeps the pair")
    {
        const auto layout = text::layoutTextStyled(set, "ab", allFace(2, 0), 32.0f);
        REQUIRE(layout.glyphs.size() == 2);
        CHECK(layout.glyphs[1].offset.x == doctest::Approx(4.0f)); // 8 advance - 4 kern
    }

    SUBCASE("crossing into the bold face drops it")
    {
        // The pair describes two glyphs from one designed face. Applying a Regular
        // pair to a Regular/Bold junction is meaningless and visibly wrong on tight
        // pairs like "AV".
        const std::vector<uint8_t> wanted = {0, text::STYLE_BOLD};
        const auto layout = text::layoutTextStyled(set, "ab", wanted, 32.0f);
        REQUIRE(layout.glyphs.size() == 2);
        CHECK(layout.glyphs[1].offset.x == doctest::Approx(8.0f));
    }
}

// ------------------------------------------------------------
// 5. Ellipsis interaction
// ------------------------------------------------------------

TEST_CASE("applyEllipsis defaults to no baseline shift, so single-font callers are unchanged")
{
    const resource::FontData face = makeFace();

    auto withDefault = text::layoutText(face, "abcdefghij", 32.0f);
    text::applyEllipsis(withDefault, face, 32.0f, 40.0f, 0.0f);

    auto withExplicitZero = text::layoutText(face, "abcdefghij", 32.0f);
    text::applyEllipsis(withExplicitZero, face, 32.0f, 40.0f, 0.0f, 0.0f);

    REQUIRE(withDefault.glyphs.size() == withExplicitZero.glyphs.size());
    for (size_t i = 0; i < withDefault.glyphs.size(); ++i)
    {
        CHECK(withDefault.glyphs[i].offset.y == withExplicitZero.glyphs[i].offset.y);
    }
}

TEST_CASE("the ellipsis follows the block baseline and stays on the base face")
{
    resource::FontData regular = makeFace();
    resource::FontData tallBold = makeFace();
    tallBold.metadata.ascender = 40.0f;

    text::FaceSet set;
    set.faces[0] = &regular;
    set.faces[text::STYLE_BOLD] = &tallBold;

    const float shift = text::baselineShiftForFaceSet(set, 32.0f);
    REQUIRE(shift == doctest::Approx(8.0f));

    auto layout = text::layoutTextStyled(set, "abcdefghij", allFace(10, text::STYLE_BOLD), 32.0f);
    text::applyEllipsis(layout, regular, 32.0f, 40.0f, 0.0f, shift);

    REQUIRE_FALSE(layout.glyphs.empty());
    const auto& appended = layout.glyphs.back();
    CHECK(appended.charIndex == UINT32_MAX);
    // Synthesized glyphs render with the base style, so face 0 — and it must sit on
    // the same baseline as the bold glyphs it is truncating.
    CHECK(appended.faceIndex == 0);
    CHECK(appended.offset.y == doctest::Approx(8.0f));
}
