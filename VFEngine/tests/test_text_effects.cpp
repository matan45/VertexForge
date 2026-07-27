#include <doctest.h>
#include <text/TextEffects.hpp>
#include <text/RichTextParser.hpp>
#include <math/MathHelper.hpp>
#include <cmath>
#include <limits>

// VK-1635 outline / drop shadow / glow, CPU side.
//
// Everything the GPU sees is produced by buildTextEffectInstance(), and every one of its
// failure modes is silent on screen: a wrong unit gives an outline that is off by the font's
// scale factor, a wrong colour byte order gives the complementary colour, and a margin that
// is too small clips the effect at the glyph's atlas cell instead of erroring. So it is
// pinned here rather than left to a GPU eyeball.

namespace
{
    // Matches the shader's unpackUnorm4x8 convention: red in the least significant byte.
    constexpr uint32_t packedRGBA(uint32_t r, uint32_t g, uint32_t b, uint32_t a)
    {
        return r | (g << 8) | (b << 16) | (a << 24);
    }

    components::TextEffectSettings outlineOnly(float width)
    {
        components::TextEffectSettings s;
        s.outlineWidth = width;
        s.outlineColor = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        return s;
    }
}

TEST_SUITE("TextEffects")
{
    TEST_CASE("packRGBA8 round-trips every byte and puts red in the low byte")
    {
        for (int i = 0; i <= 255; ++i)
        {
            const float v = static_cast<float>(i) / 255.0f;
            const uint32_t packed = math::packRGBA8(v, v, v, v);
            CAPTURE(i);
            CHECK(packed == packedRGBA(static_cast<uint32_t>(i), static_cast<uint32_t>(i),
                                       static_cast<uint32_t>(i), static_cast<uint32_t>(i)));

            float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
            math::unpackRGBA8(packed, r, g, b, a);
            CHECK(r == doctest::Approx(v));
            CHECK(g == doctest::Approx(v));
            CHECK(b == doctest::Approx(v));
            CHECK(a == doctest::Approx(v));
        }

        // Channel order is the whole ballgame: swapping red and alpha here would tint every
        // outline in the engine and still round-trip cleanly through the test above.
        CHECK(math::packRGBA8(1.0f, 0.0f, 0.0f, 0.0f) == 0x000000FFu);
        CHECK(math::packRGBA8(0.0f, 1.0f, 0.0f, 0.0f) == 0x0000FF00u);
        CHECK(math::packRGBA8(0.0f, 0.0f, 1.0f, 0.0f) == 0x00FF0000u);
        CHECK(math::packRGBA8(0.0f, 0.0f, 0.0f, 1.0f) == 0xFF000000u);

        // Out-of-range input saturates rather than wrapping into a neighbouring channel.
        CHECK(math::packRGBA8(2.0f, -1.0f, 0.0f, 0.0f) == 0x000000FFu);
    }

    TEST_CASE("default settings produce a completely inert instance")
    {
        const components::TextEffectSettings settings;
        CHECK_FALSE(settings.any());

        const auto inst = text::buildTextEffectInstance(settings, 1.0f, true);
        CHECK_FALSE(inst.any());
        CHECK(inst.colors.w == 0u);
        CHECK(inst.params == glm::vec4(0.0f));
        // Zero margin is what keeps effect-free text on exactly the geometry it had before
        // VK-1635 - the vertex shader's expand term collapses to zero.
        CHECK(inst.marginPx == 0.0f);
    }

    TEST_CASE("effects are dropped when the font carries no distance field")
    {
        const auto settings = outlineOnly(2.0f);
        REQUIRE(settings.any());

        // Bitmap and colour-emoji atlases bake spread == edgeValue == 0. There is no
        // distance to band, so the effect is dropped rather than rendered from noise.
        const auto noField = text::buildTextEffectInstance(settings, 1.0f, false);
        CHECK(noField.colors.w == 0u);
        CHECK(noField.marginPx == 0.0f);

        // A degenerate scale would divide by zero on the way to texels.
        const auto zeroScale = text::buildTextEffectInstance(settings, 0.0f, true);
        CHECK(zeroScale.colors.w == 0u);
        const auto negScale = text::buildTextEffectInstance(settings, -1.0f, true);
        CHECK(negScale.colors.w == 0u);
    }

    TEST_CASE("layout pixels convert to atlas texels by the font's scale")
    {
        // scale = fontSize / baseFontSize, i.e. layout pixels per atlas texel. A 2 px
        // outline on text rendered at 2x its bake size is one texel of field.
        const auto doubled = text::buildTextEffectInstance(outlineOnly(2.0f), 2.0f, true);
        CHECK(doubled.params.x == doctest::Approx(1.0f));

        const auto native = text::buildTextEffectInstance(outlineOnly(2.0f), 1.0f, true);
        CHECK(native.params.x == doctest::Approx(2.0f));

        const auto halved = text::buildTextEffectInstance(outlineOnly(2.0f), 0.5f, true);
        CHECK(halved.params.x == doctest::Approx(4.0f));
    }

    TEST_CASE("each effect sets only its own flag bit, colour slot and param slot")
    {
        SUBCASE("outline")
        {
            const auto inst = text::buildTextEffectInstance(outlineOnly(2.0f), 2.0f, true);
            CHECK(inst.colors.w == text::TEXT_EFFECT_OUTLINE);
            CHECK(inst.colors.x == math::packRGBA8(1.0f, 0.0f, 0.0f, 1.0f));
            CHECK(inst.colors.y == 0u);
            CHECK(inst.colors.z == 0u);
            CHECK(inst.params.x == doctest::Approx(1.0f));
            CHECK(inst.params.y == 0.0f);
            CHECK(inst.params.z == 0.0f);
            CHECK(inst.params.w == 0.0f);
            // reach = 1 texel, + 1 texel of AA slack, back to layout pixels at scale 2.
            CHECK(inst.marginPx == doctest::Approx(4.0f));
        }

        SUBCASE("shadow")
        {
            components::TextEffectSettings s;
            s.shadowOffset = glm::vec2(3.0f, -1.0f);
            s.shadowColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.5f);
            const auto inst = text::buildTextEffectInstance(s, 2.0f, true);

            CHECK(inst.colors.w == text::TEXT_EFFECT_SHADOW);
            CHECK(inst.params.y == doctest::Approx(1.5f));
            CHECK(inst.params.z == doctest::Approx(-0.5f));
            // The margin takes the larger axis and must use its MAGNITUDE - a negative
            // offset displaces the shadow just as far, only the other way.
            CHECK(inst.marginPx == doctest::Approx((1.5f + 1.0f) * 2.0f));
        }

        SUBCASE("glow")
        {
            components::TextEffectSettings s;
            s.glowRange = 8.0f;
            s.glowColor = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
            const auto inst = text::buildTextEffectInstance(s, 2.0f, true);

            CHECK(inst.colors.w == text::TEXT_EFFECT_GLOW);
            CHECK(inst.colors.z == math::packRGBA8(0.0f, 1.0f, 0.0f, 1.0f));
            CHECK(inst.params.w == doctest::Approx(4.0f));
            CHECK(inst.marginPx == doctest::Approx((4.0f + 1.0f) * 2.0f));
        }
    }

    TEST_CASE("an effect with zero distance or zero alpha stays off")
    {
        components::TextEffectSettings s;
        s.outlineColor = glm::vec4(1.0f);
        s.outlineWidth = 0.0f;              // colour set but no width
        s.glowColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
        s.glowRange = 4.0f;                 // range set but fully transparent
        s.shadowColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        s.shadowOffset = glm::vec2(0.0f);   // opaque but not displaced

        CHECK_FALSE(s.any());
        const auto inst = text::buildTextEffectInstance(s, 1.0f, true);
        CHECK(inst.colors.w == 0u);
        CHECK(inst.marginPx == 0.0f);
    }

    TEST_CASE("the shadow margin accounts for the outline it is cast by")
    {
        // The shader casts the shadow of the OUTLINED silhouette, so the quad has to hold
        // the offset PLUS the outline width. Budgeting only the offset clips the far corner
        // of the shadow of any outlined label.
        components::TextEffectSettings s;
        s.outlineWidth = 2.0f;
        s.outlineColor = glm::vec4(1.0f);
        s.shadowOffset = glm::vec2(3.0f, 0.0f);
        s.shadowColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

        const auto inst = text::buildTextEffectInstance(s, 1.0f, true);
        CHECK(inst.colors.w == (text::TEXT_EFFECT_OUTLINE | text::TEXT_EFFECT_SHADOW));
        CHECK(inst.marginPx == doctest::Approx(3.0f + 2.0f + 1.0f));

        // ...and it must still dominate the outline-only margin.
        const auto outlineAlone = text::buildTextEffectInstance(outlineOnly(2.0f), 1.0f, true);
        CHECK(inst.marginPx > outlineAlone.marginPx);
    }

    TEST_CASE("margin grows monotonically with every distance")
    {
        const auto small = text::buildTextEffectInstance(outlineOnly(1.0f), 1.0f, true);
        const auto large = text::buildTextEffectInstance(outlineOnly(5.0f), 1.0f, true);
        CHECK(large.marginPx > small.marginPx);

        components::TextEffectSettings nearShadow;
        nearShadow.shadowOffset = glm::vec2(1.0f, 1.0f);
        nearShadow.shadowColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        components::TextEffectSettings farShadow = nearShadow;
        farShadow.shadowOffset = glm::vec2(1.0f, 6.0f);

        CHECK(text::buildTextEffectInstance(farShadow, 1.0f, true).marginPx >
              text::buildTextEffectInstance(nearShadow, 1.0f, true).marginPx);
    }

    // Scripts and scene JSON both reach these paths without validating anything, so a
    // non-finite value is a real input, not a hypothetical. Every one of these would be
    // silent: NaN fails every comparison, so it slips through ordinary `<= 0` guards and
    // then poisons the instance all the way to the vertex shader.
    TEST_CASE("non-finite input never reaches the GPU")
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();

        SUBCASE("packRGBA8 must not invoke UB on NaN")
        {
            // clamp() would return NaN here (std::min/std::max propagate it) and the
            // float->uint32 cast of a NaN is undefined behaviour.
            CHECK(math::packRGBA8(nan, nan, nan, nan) == 0u);
            CHECK(math::packRGBA8(nan, 1.0f, nan, 1.0f) == 0xFF00FF00u);
            // Infinities saturate rather than wrapping.
            CHECK(math::packRGBA8(inf, -inf, inf, -inf) == 0x00FF00FFu);
        }

        SUBCASE("a NaN scale is rejected, not divided by")
        {
            // `scale <= 0.0f` is FALSE for NaN - the guard has to be !(scale > 0).
            const auto inst = text::buildTextEffectInstance(outlineOnly(2.0f), nan, true);
            CHECK(inst.colors.w == 0u);
            CHECK(inst.marginPx == 0.0f);
        }

        SUBCASE("a NaN shadow offset is the one distance the enable rule lets through")
        {
            // hasOutline()/hasGlow() test `distance > 0`, which NaN fails - those switch
            // off on their own. hasShadow() tests `offset != 0`, which NaN PASSES, so the
            // conversion itself has to be the backstop.
            components::TextEffectSettings s;
            s.shadowColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            s.shadowOffset = glm::vec2(nan, 2.0f);
            CHECK(s.hasShadow());

            const auto inst = text::buildTextEffectInstance(s, 1.0f, true);
            CHECK(inst.params.y == 0.0f);
            CHECK(inst.params.z == doctest::Approx(2.0f));
            CHECK(std::isfinite(inst.marginPx));
        }

        SUBCASE("absurd distances are bounded instead of exploding the quad")
        {
            components::TextEffectSettings s;
            s.shadowColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            s.shadowOffset = glm::vec2(1.0e9f, -1.0e9f);

            const auto inst = text::buildTextEffectInstance(s, 1.0f, true);
            CHECK(inst.params.y == doctest::Approx(256.0f));
            CHECK(inst.params.z == doctest::Approx(-256.0f));
            // The margin drives the vertex shader's quad inflation; unbounded here means
            // a single glyph covering the whole framebuffer.
            CHECK(std::isfinite(inst.marginPx));
            CHECK(inst.marginPx <= 600.0f);

            s.shadowOffset = glm::vec2(inf, 0.0f);
            const auto infInst = text::buildTextEffectInstance(s, 1.0f, true);
            CHECK(infInst.params.y == doctest::Approx(256.0f));
            CHECK(std::isfinite(infInst.marginPx));
        }
    }

    TEST_CASE("scaledBy scales distances but never colours")
    {
        components::TextEffectSettings s;
        s.outlineWidth = 2.0f;
        s.outlineColor = glm::vec4(0.25f, 0.5f, 0.75f, 1.0f);
        s.shadowOffset = glm::vec2(1.0f, -2.0f);
        s.glowRange = 4.0f;

        const auto scaled = s.scaledBy(2.0f);
        CHECK(scaled.outlineWidth == doctest::Approx(4.0f));
        CHECK(scaled.shadowOffset.x == doctest::Approx(2.0f));
        CHECK(scaled.shadowOffset.y == doctest::Approx(-4.0f));
        CHECK(scaled.glowRange == doctest::Approx(8.0f));
        CHECK(scaled.outlineColor == s.outlineColor);
    }

    TEST_CASE("a rich-text span layers onto the label without disturbing other effects")
    {
        components::TextEffectSettings base;
        base.shadowOffset = glm::vec2(2.0f, 2.0f);
        base.shadowColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

        text::RichTextSpanStyle span;
        span.hasOutline = true;
        span.outlineColor = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        span.outlineWidth = 3.0f;

        const auto merged = span.applyTo(base);
        CHECK(merged.outlineWidth == doctest::Approx(3.0f));
        CHECK(merged.outlineColor.r == doctest::Approx(1.0f));
        // The span said nothing about the shadow, so the label's survives.
        CHECK(merged.shadowOffset.x == doctest::Approx(2.0f));
        CHECK(merged.shadowColor.a == doctest::Approx(1.0f));
    }

    TEST_CASE("a bare effect tag inherits the label's distance, else falls back to a default")
    {
        text::RichTextSpanStyle span;
        span.hasOutline = true;
        span.outlineColor = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
        span.outlineWidth = 0.0f;   // [outline=#00FF00] with no width

        components::TextEffectSettings withOutline;
        withOutline.outlineWidth = 5.0f;
        withOutline.outlineColor = glm::vec4(1.0f);
        CHECK(span.applyTo(withOutline).outlineWidth == doctest::Approx(5.0f));

        // On a label with no outline of its own the tag must still show something -
        // resolving to width 0 would make the tag a silent no-op.
        const components::TextEffectSettings none;
        CHECK(span.applyTo(none).outlineWidth ==
              doctest::Approx(text::RICH_TEXT_DEFAULT_OUTLINE_WIDTH));
        CHECK(span.applyTo(none).any());

        text::RichTextSpanStyle shadowSpan;
        shadowSpan.hasShadow = true;
        CHECK(shadowSpan.applyTo(none).shadowOffset.x ==
              doctest::Approx(text::RICH_TEXT_DEFAULT_SHADOW_OFFSET));
        CHECK(shadowSpan.applyTo(none).any());

        components::TextEffectSettings withShadow;
        withShadow.shadowOffset = glm::vec2(4.0f, 1.0f);
        withShadow.shadowColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        CHECK(shadowSpan.applyTo(withShadow).shadowOffset.x == doctest::Approx(4.0f));

        text::RichTextSpanStyle glowSpan;
        glowSpan.hasGlow = true;
        CHECK(glowSpan.applyTo(none).glowRange ==
              doctest::Approx(text::RICH_TEXT_DEFAULT_GLOW_RANGE));
    }
}
