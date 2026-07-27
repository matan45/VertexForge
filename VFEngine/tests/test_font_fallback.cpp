#include <doctest.h>

#include <resource/DefaultFont.hpp>
#include <resource/Types.hpp>
#include <text/FontFallback.hpp>
#include <text/FontStyleFace.hpp>
#include <text/TextLayout.hpp>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace
{
    void appendUTF8(std::string& output, uint32_t codepoint)
    {
        if (codepoint <= 0x7F)
        {
            output.push_back(static_cast<char>(codepoint));
        }
        else if (codepoint <= 0x7FF)
        {
            output.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
        else if (codepoint <= 0xFFFF)
        {
            output.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
        else
        {
            output.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
    }

    std::string utf8(std::initializer_list<uint32_t> codepoints)
    {
        std::string result;
        for (const uint32_t codepoint : codepoints)
        {
            appendUTF8(result, codepoint);
        }
        return result;
    }

    resource::FontData makeFace(
        std::initializer_list<uint32_t> codepoints,
        float advance = 8.0f,
        uint32_t baseFontSize = 32,
        float lineHeight = 40.0f,
        float ascender = 32.0f,
        uint32_t atlasSize = 64)
    {
        resource::FontData face;
        face.metadata.baseFontSize = baseFontSize;
        face.metadata.lineHeight = lineHeight;
        face.metadata.ascender = ascender;
        face.metadata.descender = -8.0f;
        face.atlas.width = atlasSize;
        face.atlas.height = atlasSize;

        uint32_t cell = 0;
        for (const uint32_t codepoint : codepoints)
        {
            resource::GlyphData glyph;
            glyph.codepoint = codepoint;
            glyph.advanceX = advance;
            glyph.bearingY = ascender;
            glyph.glyphWidth = 6.0f;
            glyph.glyphHeight = 10.0f;
            glyph.atlasX = cell++ * 8;
            glyph.atlasWidth = 6;
            glyph.atlasHeight = 10;
            face.glyphs.push_back(glyph);
        }

        std::sort(
            face.glyphs.begin(),
            face.glyphs.end(),
            [](const resource::GlyphData& lhs, const resource::GlyphData& rhs)
            {
                return lhs.codepoint < rhs.codepoint;
            });
        return face;
    }

    void checkGlyphEqual(const text::LayoutGlyph& lhs, const text::LayoutGlyph& rhs)
    {
        CHECK(lhs.offset == rhs.offset);
        CHECK(lhs.size == rhs.size);
        CHECK(lhs.uvRect == rhs.uvRect);
        CHECK(lhs.codepoint == rhs.codepoint);
        CHECK(lhs.lineY == rhs.lineY);
        CHECK(lhs.charIndex == rhs.charIndex);
        CHECK(lhs.faceIndex == rhs.faceIndex);
    }
}

static_assert(text::missingClass(0x0008) == text::MissingClass::ZeroWidth);
static_assert(text::missingClass(0x0009) == text::MissingClass::SpaceLike);
static_assert(text::missingClass(0x001F) == text::MissingClass::ZeroWidth);
static_assert(text::missingClass(0x0020) == text::MissingClass::SpaceLike);
static_assert(text::missingClass(0x007E) == text::MissingClass::Printing);
static_assert(text::missingClass(0x007F) == text::MissingClass::ZeroWidth);
static_assert(text::missingClass(0x009F) == text::MissingClass::ZeroWidth);
static_assert(text::missingClass(0x00A0) == text::MissingClass::SpaceLike);
static_assert(text::missingClass(0x200A) == text::MissingClass::SpaceLike);
static_assert(text::missingClass(0x200B) == text::MissingClass::ZeroWidth);
static_assert(text::missingClass(0x2010) == text::MissingClass::Printing);
static_assert(text::missingClass(0xFE0F) == text::MissingClass::ZeroWidth);
static_assert(text::missingClass(0xFEFF) == text::MissingClass::ZeroWidth);
static_assert(text::missingClass(0xFFFD) == text::MissingClass::Printing);
static_assert((text::STYLE_MASK & text::STYLE_TOFU) == 0);

TEST_SUITE("FontFallback")
{
    TEST_CASE("normalization is ordered, deduplicated, self-skipping, and default-tailed")
    {
        const std::vector<std::string> authored{
            "  Fonts\\CJK.vfFont  ",
            "Fonts/CJK.vfFont",
            "Fonts/Primary.vfFont",
            "",
            resource::DEFAULT_FONT_SENTINEL,
            "Fonts/Emoji.vfFont",
            "Fonts/Symbols.vfFont",
            "Fonts/TooLate.vfFont"};

        const auto chain =
            text::normalizeFallbackChain(authored, "Fonts\\Primary.vfFont");
        REQUIRE(chain.count == text::MAX_FALLBACK_FACES);
        CHECK(chain.paths[0] == "Fonts\\CJK.vfFont");
        CHECK(chain.paths[1] == "Fonts/Emoji.vfFont");
        CHECK(chain.paths[2] == "Fonts/Symbols.vfFont");
        CHECK(chain.paths[3] == resource::DEFAULT_FONT_SENTINEL);
    }

    TEST_CASE("empty normalization still contains the mandatory default tail")
    {
        const std::vector<std::string> authored;
        const auto chain = text::normalizeFallbackChain(authored);
        REQUIRE(chain.count == 1);
        CHECK(chain.paths[0] == resource::DEFAULT_FONT_SENTINEL);
    }

    TEST_CASE("unused fallbacks do not alter present-glyph layout or metrics")
    {
        const resource::FontData primary = makeFace({'a', 'b'});
        const resource::FontData tallFallback =
            makeFace({0x4E2D}, 16.0f, 32, 80.0f, 64.0f, 128);

        const auto legacy = text::layoutText(primary, "ab", 32.0f, 0.0f, 1.0f, 1.0f);

        text::FaceSet set;
        set.faces[0] = &primary;
        set.fallback[0] = &tallFallback;
        const auto withFallback =
            text::layoutTextStyled(set, "ab", {}, 32.0f, 0.0f, 1.0f, 1.0f);

        REQUIRE(withFallback.glyphs.size() == legacy.glyphs.size());
        for (size_t i = 0; i < legacy.glyphs.size(); ++i)
        {
            checkGlyphEqual(withFallback.glyphs[i], legacy.glyphs[i]);
        }
        CHECK(withFallback.boundingBox == legacy.boundingBox);

        const auto singleMetrics = text::computeLineMetrics(primary, 32.0f, 1.0f);
        const auto setMetrics = text::computeLineMetrics(set, 32.0f, 1.0f);
        CHECK(setMetrics.lineHeight == singleMetrics.lineHeight);
        CHECK(setMetrics.singleLineHeight == singleMetrics.singleLineHeight);
        CHECK(text::baselineShiftForFaceSet(set, 32.0f) == 0.0f);
    }

    TEST_CASE("fallback resolution is ordered and shares the primary baseline")
    {
        const resource::FontData primary = makeFace({'a'});
        const resource::FontData first = makeFace({0x4E2D}, 12.0f, 64, 80.0f, 64.0f, 128);
        const resource::FontData second = makeFace({0x4E2D, 0x6587}, 20.0f);

        text::FaceSet set;
        set.faces[0] = &primary;
        set.fallback[0] = &first;
        set.fallback[1] = &second;

        const auto layout =
            text::layoutTextStyled(set, utf8({0x4E2D, 0x6587}), {}, 32.0f);
        REQUIRE(layout.glyphs.size() == 2);
        CHECK(layout.glyphs[0].faceIndex == 4);
        CHECK(layout.glyphs[1].faceIndex == 5);
        CHECK(layout.glyphs[0].offset.y == doctest::Approx(0.0f));
        CHECK(layout.glyphs[1].offset.y == doctest::Approx(0.0f));
        CHECK(layout.boundingBox.y == doctest::Approx(40.0f));
    }

    TEST_CASE("style then base then fallback then tofu precedence is explicit")
    {
        const resource::FontData primary = makeFace({'p', 'b'});
        const resource::FontData bold = makeFace({'b'});
        const resource::FontData fallback = makeFace({'p', 'f'});

        text::FaceSet set;
        set.faces[0] = &primary;
        set.faces[text::STYLE_BOLD] = &bold;
        set.fallback[0] = &fallback;

        const std::vector<uint8_t> boldRequest(4, text::STYLE_BOLD);
        const auto layout =
            text::layoutTextStyled(set, "bpfx", boldRequest, 32.0f);
        REQUIRE(layout.glyphs.size() == 4);
        CHECK(layout.glyphs[0].faceIndex == text::STYLE_BOLD);
        CHECK(layout.glyphs[1].faceIndex == 0);
        CHECK(layout.glyphs[2].faceIndex == 4);
        CHECK(layout.glyphs[3].faceIndex == text::TOFU_FACE_INDEX);
    }

    TEST_CASE("missing printing codepoints emit procedural tofu with drawn-glyph advance")
    {
        const resource::FontData primary = makeFace({'a'});
        const auto layout =
            text::layoutText(primary, utf8({0x4E2D, 0x6587}), 32.0f, 0.0f, 1.0f, 2.0f);

        REQUIRE(layout.glyphs.size() == 2);
        for (const text::LayoutGlyph& glyph : layout.glyphs)
        {
            CHECK(glyph.faceIndex == text::TOFU_FACE_INDEX);
            CHECK(glyph.uvRect == glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
            CHECK(glyph.size.x == doctest::Approx(12.8f));
            CHECK(glyph.size.y == doctest::Approx(23.04f));
        }
        CHECK(layout.glyphs[1].offset.x - layout.glyphs[0].offset.x ==
              doctest::Approx(18.0f));
        CHECK(layout.boundingBox.x == doctest::Approx(36.0f));
    }

    TEST_CASE("spaces stay invisible and format controls become zero width")
    {
        const resource::FontData primary = makeFace({'a'});
        const std::string value =
            utf8({0x0009, 0x0020, 0x00A0, 0x200B, 0xFEFF, 0x00AD, 0xFE0F});
        const auto layout = text::layoutText(primary, value, 32.0f);

        CHECK(layout.glyphs.empty());
        CHECK(layout.boundingBox.x == doctest::Approx(48.0f));
    }

    TEST_CASE("kerning is suppressed across fallback and tofu boundaries")
    {
        resource::FontData primary = makeFace({'a', 'b'});
        primary.formatFlags = resource::FontFormatFlags::SDF_ENABLED |
                              resource::FontFormatFlags::KERNING_ENABLED;
        primary.kerningPairs.push_back({'a', 0x4E2D, -4.0f});
        primary.kerningPairs.push_back({0x6587, 'b', -4.0f});
        const resource::FontData fallback = makeFace({0x4E2D});

        text::FaceSet set;
        set.faces[0] = &primary;
        set.fallback[0] = &fallback;
        const auto layout =
            text::layoutTextStyled(set, utf8({'a', 0x4E2D, 0x6587, 'b'}), {}, 32.0f);

        REQUIRE(layout.glyphs.size() == 4);
        CHECK(layout.glyphs[1].offset.x == doctest::Approx(8.0f));
        CHECK(layout.glyphs[2].faceIndex == text::TOFU_FACE_INDEX);
        CHECK(layout.glyphs[3].offset.x == doctest::Approx(32.0f));
    }

    TEST_CASE("tofu participates in wrapping and remains compatible with ellipsis")
    {
        // Pin tofu to the existing unbroken-word wrapping policy rather than
        // inventing a second hard-wrap policy for missing glyphs. In particular,
        // a real glyph with the same advance must land on the same line.
        const resource::FontData primary = makeFace({'a', 0x2026}, 16.0f);
        auto wrapped =
            text::layoutText(primary, utf8({0x4E2D, 0x6587, 0x4E09}), 32.0f, 20.0f);
        const auto wrappedPresent =
            text::layoutText(primary, "aaa", 32.0f, 20.0f);
        REQUIRE(wrapped.glyphs.size() == 3);
        REQUIRE(wrappedPresent.glyphs.size() == wrapped.glyphs.size());
        for (size_t i = 0; i < wrapped.glyphs.size(); ++i)
        {
            CHECK(wrapped.glyphs[i].lineY == wrappedPresent.glyphs[i].lineY);
        }
        CHECK(wrapped.glyphs[1].lineY > wrapped.glyphs[0].lineY);

        auto truncated =
            text::layoutText(primary, utf8({0x4E2D, 0x6587, 0x4E09}), 32.0f);
        text::applyEllipsis(truncated, primary, 32.0f, 20.0f, 0.0f);
        REQUIRE_FALSE(truncated.glyphs.empty());
        CHECK(truncated.glyphs.back().codepoint == 0x2026);
        CHECK(truncated.glyphs.back().faceIndex == 0);
    }
}
