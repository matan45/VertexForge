#include <doctest.h>
#include <text/TextLayout.hpp>
#include <components/UIComponents.hpp>
#include <cmath>
#include <limits>

// ============================================================
// VK-1637: text::resolveTextBox — the world/UI text box policy.
// ============================================================
//
// The rules for "which box, which wrap width, which gate" used to be inlined in
// TextPipeline::setTextDrawList and UITextPipeline::setUITextDrawList. Both are
// Vulkan-bound TUs, so the CPU-only Tests project could never reach them, and the
// two copies had already drifted (the world-space-canvas UILabel path dropped
// overflow and wordWrap entirely). VK-1637 pulled the rules into a pure function;
// these cases are the whole of its contract.
//
// Only TextPipeline is wired for now. clipSupported / requireHeightForVAlign are
// the seams that record where UITextPipeline still disagrees.

namespace
{
    // Defaults matter here: a default-constructed request models a pre-VK-1637
    // TextComponent, and several cases below assert that path is inert.
    text::TextBoxRequest worldRequest()
    {
        text::TextBoxRequest r;
        r.clipSupported = false;
        r.requireHeightForVAlign = true;
        return r;
    }

    constexpr uint8_t OVERFLOW_NONE = 0;
    constexpr uint8_t OVERFLOW_CLIP = 1;
    constexpr uint8_t OVERFLOW_ELLIPSIS = 2;
}

TEST_SUITE("TextBoxPolicy") {

// ------------------------------------------------------------------
// Enum bridging (mirrors the HAlign/VAlign asserts in
// test_textlayout_alignment.cpp — TextLayout.hpp deliberately cannot include
// components/UIComponents.hpp, so the mapping is locked from a test instead).
// ------------------------------------------------------------------

TEST_CASE("OverflowMode mirrors components::TextOverflow") {
    static_assert(static_cast<uint8_t>(text::OverflowMode::None) ==
                  static_cast<uint8_t>(components::TextOverflow::Overflow));
    static_assert(static_cast<uint8_t>(text::OverflowMode::Clip) ==
                  static_cast<uint8_t>(components::TextOverflow::Clip));
    static_assert(static_cast<uint8_t>(text::OverflowMode::Ellipsis) ==
                  static_cast<uint8_t>(components::TextOverflow::Ellipsis));

    CHECK(text::toOverflowMode(0) == text::OverflowMode::None);
    CHECK(text::toOverflowMode(1) == text::OverflowMode::Clip);
    CHECK(text::toOverflowMode(2) == text::OverflowMode::Ellipsis);

    // Out of range falls back to None, matching toHAlign / toVAlign's convention.
    CHECK(text::toOverflowMode(3) == text::OverflowMode::None);
    CHECK(text::toOverflowMode(255) == text::OverflowMode::None);
}

// ------------------------------------------------------------------
// The legacy path. This is the pixel-identity claim: every pre-VK-1637
// TextComponent deserializes to these values, and they must resolve to the
// behaviour TextPipeline had before the ticket.
// ------------------------------------------------------------------

TEST_CASE("default request is inert") {
    const text::TextBoxPolicy p = text::resolveTextBox(worldRequest());

    CHECK(p.wrapWidth == 0.0f);
    CHECK(p.ellipsis == false);
    CHECK(p.clip == false);
    CHECK(p.horizontal == text::HAlign::Left);
    CHECK(p.vertical == text::VAlign::Top);
    CHECK(p.alignToInkWidth == true);   // no box -> caller uses boundingBox.x
    CHECK(p.alignHeight == 0.0f);
}

TEST_CASE("legacy maxWidth still wraps") {
    // wordWrap defaults true precisely so a scene authored with maxWidth=200 keeps
    // wrapping. wrapWidth must be a *copy* of maxWidth, not a recomputation, or the
    // float handed to layoutText could differ in the last bit.
    text::TextBoxRequest r = worldRequest();
    r.maxWidth = 200.0f;

    const text::TextBoxPolicy p = text::resolveTextBox(r);
    CHECK(p.wrapWidth == 200.0f);
    CHECK(p.alignToInkWidth == false);
    CHECK(p.alignWidth == 200.0f);
}

// ------------------------------------------------------------------
// Wrap toggle
// ------------------------------------------------------------------

TEST_CASE("wordWrap gates the wrap width only, never the alignment box") {
    text::TextBoxRequest r = worldRequest();
    r.maxWidth = 300.0f;
    r.horizontal = 1; // Center
    r.wordWrap = false;

    const text::TextBoxPolicy p = text::resolveTextBox(r);
    CHECK(p.wrapWidth == 0.0f);            // layoutText must not wrap
    CHECK(p.alignToInkWidth == false);     // ... but the box still boxes alignment
    CHECK(p.alignWidth == 300.0f);
    CHECK(p.horizontal == text::HAlign::Center);
}

TEST_CASE("wordWrap true with no box is still no wrap") {
    text::TextBoxRequest r = worldRequest();
    r.wordWrap = true;
    r.maxWidth = 0.0f;

    CHECK(text::resolveTextBox(r).wrapWidth == 0.0f);
}

// ------------------------------------------------------------------
// Ellipsis
// ------------------------------------------------------------------

TEST_CASE("ellipsis needs a width to truncate to") {
    text::TextBoxRequest r = worldRequest();
    r.overflow = OVERFLOW_ELLIPSIS;

    SUBCASE("no box -> no ellipsis") {
        r.maxWidth = 0.0f;
        const text::TextBoxPolicy p = text::resolveTextBox(r);
        // applyEllipsis also early-returns on maxWidth <= 0; this gate is what lets
        // the Max Width tooltip promise "0 = no ellipsis" rather than describe it.
        CHECK(p.ellipsis == false);
    }

    SUBCASE("with a box -> ellipsis at that width") {
        r.maxWidth = 120.0f;
        const text::TextBoxPolicy p = text::resolveTextBox(r);
        CHECK(p.ellipsis == true);
        CHECK(p.ellipsisWidth == 120.0f);
    }
}

TEST_CASE("ellipsis applies with wrapping off") {
    // The combination most likely to be got wrong: nothing wraps, so applyEllipsis
    // truncates the single long line at maxWidth. applyEllipsis is per-line, so the
    // same policy covers both wrap states.
    text::TextBoxRequest r = worldRequest();
    r.overflow = OVERFLOW_ELLIPSIS;
    r.maxWidth = 120.0f;
    r.wordWrap = false;

    const text::TextBoxPolicy p = text::resolveTextBox(r);
    CHECK(p.wrapWidth == 0.0f);
    CHECK(p.ellipsis == true);
    CHECK(p.ellipsisWidth == 120.0f);
    CHECK(p.alignWidth == 120.0f);
}

TEST_CASE("Overflow never ellipsizes") {
    text::TextBoxRequest r = worldRequest();
    r.overflow = OVERFLOW_NONE;
    r.maxWidth = 120.0f;

    CHECK(text::resolveTextBox(r).ellipsis == false);
}

// ------------------------------------------------------------------
// Clip degradation — VK-1637's central decision
// ------------------------------------------------------------------

TEST_CASE("Clip degrades to Overflow when the pipeline has no scissor") {
    text::TextBoxRequest r = worldRequest();  // clipSupported = false
    r.overflow = OVERFLOW_CLIP;
    r.maxWidth = 120.0f;

    const text::TextBoxPolicy p = text::resolveTextBox(r);
    CHECK(p.clip == false);
    // Specifically NOT reinterpreted as ellipsis: Clip means "cut the pixels", and
    // silently truncating with a glyph instead would be a different feature.
    CHECK(p.ellipsis == false);
    // The box itself is untouched, so alignment and wrapping behave normally.
    CHECK(p.wrapWidth == 120.0f);
    CHECK(p.alignWidth == 120.0f);
}

TEST_CASE("Clip survives when the pipeline can scissor") {
    // Pins the contract for the follow-up that converges UITextPipeline onto this.
    text::TextBoxRequest r = worldRequest();
    r.clipSupported = true;
    r.overflow = OVERFLOW_CLIP;
    r.maxWidth = 120.0f;

    const text::TextBoxPolicy p = text::resolveTextBox(r);
    CHECK(p.clip == true);
    CHECK(p.ellipsis == false);
}

TEST_CASE("clipSupported does not resurrect Clip for other modes") {
    text::TextBoxRequest r = worldRequest();
    r.clipSupported = true;
    r.maxWidth = 120.0f;

    r.overflow = OVERFLOW_NONE;
    CHECK(text::resolveTextBox(r).clip == false);

    r.overflow = OVERFLOW_ELLIPSIS;
    const text::TextBoxPolicy p = text::resolveTextBox(r);
    CHECK(p.clip == false);
    CHECK(p.ellipsis == true);
}

// ------------------------------------------------------------------
// Alignment gating
// ------------------------------------------------------------------

TEST_CASE("vertical alignment needs a height") {
    text::TextBoxRequest r = worldRequest();
    r.vertical = 1; // Middle

    SUBCASE("no height -> forced Top") {
        r.rectHeight = 0.0f;
        CHECK(text::resolveTextBox(r).vertical == text::VAlign::Top);
    }

    SUBCASE("with a height -> honoured") {
        r.rectHeight = 100.0f;
        const text::TextBoxPolicy p = text::resolveTextBox(r);
        CHECK(p.vertical == text::VAlign::Middle);
        CHECK(p.alignHeight == 100.0f);
    }
}

TEST_CASE("requireHeightForVAlign records the UITextPipeline seam") {
    // UITextPipeline does not gate: a UIRect always has a height, so it passes
    // label.size raw. Converging the two would change behaviour for degenerate
    // zero-height rects, which is a separate ticket - this flag keeps the
    // disagreement expressible instead of silently resolving it.
    text::TextBoxRequest r = worldRequest();
    r.requireHeightForVAlign = false;
    r.vertical = 2; // Bottom
    r.rectHeight = 0.0f;

    CHECK(text::resolveTextBox(r).vertical == text::VAlign::Bottom);
}

TEST_CASE("horizontal alignment is never gated") {
    // With no box the caller substitutes boundingBox.x, so Center/Right still do
    // something useful - they align each line against the widest line. The Max Width
    // tooltip must not claim otherwise.
    text::TextBoxRequest r = worldRequest();
    r.horizontal = 2; // Right
    r.maxWidth = 0.0f;

    const text::TextBoxPolicy p = text::resolveTextBox(r);
    CHECK(p.horizontal == text::HAlign::Right);
    CHECK(p.alignToInkWidth == true);
}

TEST_CASE("out-of-range alignment ordinals fall back") {
    text::TextBoxRequest r = worldRequest();
    r.horizontal = 200;
    r.vertical = 200;
    r.rectHeight = 100.0f;

    const text::TextBoxPolicy p = text::resolveTextBox(r);
    CHECK(p.horizontal == text::HAlign::Left);
    CHECK(p.vertical == text::VAlign::Top);
}

// ------------------------------------------------------------------
// Hostile input. Both text paths take unvalidated script input, and NaN defeats
// the obvious `x <= 0` guard because that comparison is false for NaN. VK-1635 hit
// exactly this in buildTextEffectInstance.
// ------------------------------------------------------------------

TEST_CASE("NaN box dimensions fall to the no-box branch") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(std::isnan(nan));

    SUBCASE("NaN maxWidth") {
        text::TextBoxRequest r = worldRequest();
        r.maxWidth = nan;
        r.overflow = OVERFLOW_ELLIPSIS;
        r.horizontal = 1;

        const text::TextBoxPolicy p = text::resolveTextBox(r);
        CHECK(p.ellipsis == false);          // no truncation against NaN
        // alignToInkWidth routes the caller to boundingBox.x, so the NaN in
        // alignWidth is never read.
        CHECK(p.alignToInkWidth == true);
    }

    SUBCASE("NaN rectHeight") {
        text::TextBoxRequest r = worldRequest();
        r.rectHeight = nan;
        r.vertical = 1;

        // VAlign::Top means computeAlignedLineOrigins never reads contentSize.y, so
        // the NaN in alignHeight cannot reach the arithmetic.
        CHECK(text::resolveTextBox(r).vertical == text::VAlign::Top);
    }
}

TEST_CASE("negative box dimensions behave as no box") {
    text::TextBoxRequest r = worldRequest();
    r.maxWidth = -50.0f;
    r.rectHeight = -50.0f;
    r.overflow = OVERFLOW_ELLIPSIS;
    r.vertical = 1;

    const text::TextBoxPolicy p = text::resolveTextBox(r);
    CHECK(p.ellipsis == false);
    CHECK(p.alignToInkWidth == true);
    CHECK(p.vertical == text::VAlign::Top);
}

}
