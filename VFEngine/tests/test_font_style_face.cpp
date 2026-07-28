#include <doctest.h>
#include <text/FontStyleFace.hpp>
#include <resource/DefaultFont.hpp>
#include <components/UIComponents.hpp>
#include <algorithm>
#include <set>
#include <string>
#include <vector>

// ============================================================
// VK-1636 — sibling style-face resolution policy.
//
// Bold and italic have always been synthesized. This ticket lets a real
// "Roboto-Bold.vfFont" next to "Roboto.vfFont" take over, falling back to the
// synthesis when there is no such file. Everything worth getting wrong lives in
// these three pure functions: which file names to look for, which order to give
// up in, and which style bits the shader must still fake once a face is chosen.
//
// The probing itself (filesystem + atlas residency) is TextFontCache's job and
// needs a Vulkan device, so it is not covered here.
// ============================================================

using namespace text;

namespace
{
    bool contains(const std::vector<std::string>& haystack, const std::string& needle)
    {
        return std::find(haystack.begin(), haystack.end(), needle) != haystack.end();
    }
}

// ------------------------------------------------------------
// styleBitsFromFontStyle
// ------------------------------------------------------------

TEST_CASE("style bits match the components::FontStyle numbering")
{
    // The header takes a raw uint8_t so it can stay free of <entt/entt.hpp>. That
    // only works while the enum keeps this numbering, which the DTOs also document
    // ("0=Normal, 1=Bold, 2=Italic, 3=BoldItalic"). Pin it here rather than trusting
    // the comment.
    static_assert(static_cast<uint8_t>(components::FontStyle::Normal) == 0);
    static_assert(static_cast<uint8_t>(components::FontStyle::Bold) == 1);
    static_assert(static_cast<uint8_t>(components::FontStyle::Italic) == 2);
    static_assert(static_cast<uint8_t>(components::FontStyle::BoldItalic) == 3);

    CHECK(styleBitsFromFontStyle(static_cast<uint8_t>(components::FontStyle::Normal)) == 0u);
    CHECK(styleBitsFromFontStyle(static_cast<uint8_t>(components::FontStyle::Bold)) == STYLE_BOLD);
    CHECK(styleBitsFromFontStyle(static_cast<uint8_t>(components::FontStyle::Italic)) == STYLE_ITALIC);
    CHECK(styleBitsFromFontStyle(static_cast<uint8_t>(components::FontStyle::BoldItalic)) ==
          (STYLE_BOLD | STYLE_ITALIC));

    SUBCASE("an out-of-range value degrades to Normal rather than aliasing a style")
    {
        // Scene data and script natives both write this field as a raw integer
        // (UIButtonLabelAPI.cpp casts an int64 straight to FontStyle), so a bogus
        // value is reachable. It must mean "no styling", not "bold" via a stray bit.
        CHECK(styleBitsFromFontStyle(4) == 0u);
        CHECK(styleBitsFromFontStyle(255) == 0u);
    }

    SUBCASE("the bits are the shader's, not ours")
    {
        // ui_text.glsl:123 and text.glsl:161 test (vStyleFlags & 1u) for bold;
        // the vertex stages test bit 1 for the italic shear.
        CHECK(STYLE_BOLD == 0x1u);
        CHECK(STYLE_ITALIC == 0x2u);
    }
}

// ------------------------------------------------------------
// styleDowngradeOrder
// ------------------------------------------------------------

TEST_CASE("BoldItalic degrades through Bold before Italic")
{
    const auto order = styleDowngradeOrder(STYLE_BOLD | STYLE_ITALIC);
    REQUIRE(order.size() == 3);
    CHECK(order[0] == (STYLE_BOLD | STYLE_ITALIC));

    // Bold before Italic is a deliberate quality call: a real Bold plus the 12-degree
    // shear reads as bold-italic, whereas a real Italic plus a thickened Regular does
    // not read as bold. Flipping these two lines is a visible regression.
    CHECK(order[1] == STYLE_BOLD);
    CHECK(order[2] == STYLE_ITALIC);
}

TEST_CASE("single-axis styles have nothing to degrade to")
{
    CHECK(styleDowngradeOrder(STYLE_BOLD) == std::vector<uint32_t>{STYLE_BOLD});
    CHECK(styleDowngradeOrder(STYLE_ITALIC) == std::vector<uint32_t>{STYLE_ITALIC});

    // The base face is the implicit fallback and is never probed, so it must not
    // appear in the list — a zero entry would make the caller look for a file whose
    // name is the base path and request it a second time.
    const auto boldOrder = styleDowngradeOrder(STYLE_BOLD);
    CHECK(std::find(boldOrder.begin(), boldOrder.end(), 0u) == boldOrder.end());

    CHECK(styleDowngradeOrder(0).empty());
}

// ------------------------------------------------------------
// styledPathCandidates
// ------------------------------------------------------------

TEST_CASE("the idiomatic hyphenated sibling is offered first")
{
    const auto bold = styledPathCandidates("assets/fonts/Roboto.vfFont", STYLE_BOLD);
    REQUIRE_FALSE(bold.empty());
    CHECK(bold[0] == "assets/fonts/Roboto-Bold.vfFont");
    CHECK(contains(bold, "assets/fonts/Roboto_Bold.vfFont"));
    CHECK(contains(bold, "assets/fonts/RobotoBold.vfFont"));

    const auto italic = styledPathCandidates("assets/fonts/Roboto.vfFont", STYLE_ITALIC);
    REQUIRE_FALSE(italic.empty());
    CHECK(italic[0] == "assets/fonts/Roboto-Italic.vfFont");
    // Plenty of families ship an Oblique instead of an Italic.
    CHECK(contains(italic, "assets/fonts/Roboto-Oblique.vfFont"));

    const auto both = styledPathCandidates("assets/fonts/Roboto.vfFont", STYLE_BOLD | STYLE_ITALIC);
    REQUIRE_FALSE(both.empty());
    CHECK(both[0] == "assets/fonts/Roboto-BoldItalic.vfFont");
    CHECK(contains(both, "assets/fonts/Roboto-Bold-Italic.vfFont"));
    CHECK(contains(both, "assets/fonts/Roboto-BoldOblique.vfFont"));
}

TEST_CASE("the base path's extension spelling is preserved verbatim")
{
    // The importer writes ".vfFont", the drag-drop extension filter matches
    // ".vffont", and Windows resolves either. Rewriting the case would still open
    // the file on Windows but would produce a DIFFERENT cache key than the base
    // font's, which is how a family ends up loading the same atlas twice.
    CHECK(styledPathCandidates("a/Roboto.vfFont", STYLE_BOLD)[0] == "a/Roboto-Bold.vfFont");
    CHECK(styledPathCandidates("a/Roboto.vffont", STYLE_BOLD)[0] == "a/Roboto-Bold.vffont");
    CHECK(styledPathCandidates("a/Roboto.VFFONT", STYLE_BOLD)[0] == "a/Roboto-Bold.VFFONT");
}

TEST_CASE("path shapes that could confuse stem/extension splitting")
{
    SUBCASE("Windows separators")
    {
        CHECK(styledPathCandidates("C:\\proj\\assets\\Roboto.vfFont", STYLE_BOLD)[0] ==
              "C:\\proj\\assets\\Roboto-Bold.vfFont");
    }

    SUBCASE("a directory containing a dot must not be mistaken for the extension")
    {
        // Only a dot after the last separator counts. Getting this wrong would emit
        // "assets/v1-Bold.2/Roboto".
        CHECK(styledPathCandidates("assets/v1.2/Roboto.vfFont", STYLE_BOLD)[0] ==
              "assets/v1.2/Roboto-Bold.vfFont");
    }

    SUBCASE("an extensionless path appends the suffix at the end")
    {
        CHECK(styledPathCandidates("assets/Roboto", STYLE_BOLD)[0] == "assets/Roboto-Bold");
        CHECK(styledPathCandidates("assets.v2/Roboto", STYLE_BOLD)[0] == "assets.v2/Roboto-Bold");
    }

    SUBCASE("a dotfile name is all extension, so the suffix still appends")
    {
        CHECK(styledPathCandidates("assets/.vfFont", STYLE_BOLD)[0] == "assets/.vfFont-Bold");
    }

    SUBCASE("a stem that already carries a style name is suffixed anyway")
    {
        // "Roboto-Bold.vfFont" assigned directly with fontStyle=Bold. We do not try
        // to be clever and strip the existing suffix: the probe simply misses and the
        // shader keeps synthesizing, which is the honest answer. The user gets real
        // bold by assigning the Regular face and setting the style.
        CHECK(styledPathCandidates("a/Roboto-Bold.vfFont", STYLE_BOLD)[0] ==
              "a/Roboto-Bold-Bold.vfFont");
    }
}

TEST_CASE("no candidates without a style or without a path")
{
    // styleBits == 0 must never probe: an unstyled label is the overwhelmingly common
    // case and has to cost exactly nothing.
    CHECK(styledPathCandidates("assets/Roboto.vfFont", 0).empty());
    CHECK(styledPathCandidates("", STYLE_BOLD).empty());
}

// ------------------------------------------------------------
// chooseStyleFace
// ------------------------------------------------------------

TEST_CASE("an unstyled request never picks a face")
{
    const StyleFaceProbe probes[] = {{STYLE_BOLD, true, true}};
    const StyleChoice choice = chooseStyleFace(0, probes);
    CHECK(choice.index == -1);
    CHECK(choice.synthesizedBits == 0u);
}

TEST_CASE("an exact resident face satisfies the whole request")
{
    const StyleFaceProbe probes[] = {{STYLE_BOLD | STYLE_ITALIC, true, true}};
    const StyleChoice choice = chooseStyleFace(STYLE_BOLD | STYLE_ITALIC, probes);
    CHECK(choice.index == 0);
    CHECK(choice.synthesizedBits == 0u);
}

TEST_CASE("a partial family keeps synthesizing only the missing axis")
{
    // Family ships Bold but no BoldItalic — the realistic half-imported case.
    const StyleFaceProbe probes[] = {
        {STYLE_BOLD | STYLE_ITALIC, false, false},  // Roboto-BoldItalic.vfFont: absent
        {STYLE_BOLD, true, true},                   // Roboto-Bold.vfFont: present
    };
    const StyleChoice choice = chooseStyleFace(STYLE_BOLD | STYLE_ITALIC, probes);
    CHECK(choice.index == 1);
    CHECK(choice.synthesizedBits == STYLE_ITALIC);
}

TEST_CASE("nothing on disk falls back to the base face and full synthesis")
{
    const StyleFaceProbe probes[] = {
        {STYLE_BOLD | STYLE_ITALIC, false, false},
        {STYLE_BOLD, false, false},
        {STYLE_ITALIC, false, false},
    };
    const StyleChoice choice = chooseStyleFace(STYLE_BOLD | STYLE_ITALIC, probes);
    CHECK(choice.index == -1);
    CHECK(choice.synthesizedBits == (STYLE_BOLD | STYLE_ITALIC));

    // The no-probes case is the same answer, and is what a caller passes when the
    // font is the sentinel default with no styled siblings installed.
    const StyleChoice none = chooseStyleFace(STYLE_BOLD, {});
    CHECK(none.index == -1);
    CHECK(none.synthesizedBits == STYLE_BOLD);
}

TEST_CASE("a face that exists but is still loading keeps full synthesis")
{
    // This is the frame-race that matters. Returning the styled key before its atlas
    // is resident would break TextFontCache's rule that a descriptor is never keyed
    // by a non-resident path — the descriptor would be built against the default
    // atlas and cached there forever. Falling back to the base face WITHOUT dropping
    // the synthesized bits also stops the glyphs popping Regular -> Bold when the
    // load completes: they read bold the whole way through.
    const StyleFaceProbe probes[] = {{STYLE_BOLD, true, false}};
    const StyleChoice choice = chooseStyleFace(STYLE_BOLD, probes);
    CHECK(choice.index == -1);
    CHECK(choice.synthesizedBits == STYLE_BOLD);
}

TEST_CASE("a resident lower-preference face beats a non-resident exact one")
{
    // BoldItalic is on disk but still uploading while Bold is ready. Taking Bold now
    // is strictly better than the base face, and the next frame that finishes the
    // BoldItalic upload promotes to it — one transition, never a downgrade.
    const StyleFaceProbe probes[] = {
        {STYLE_BOLD | STYLE_ITALIC, true, false},
        {STYLE_BOLD, true, true},
    };
    const StyleChoice choice = chooseStyleFace(STYLE_BOLD | STYLE_ITALIC, probes);
    CHECK(choice.index == 1);
    CHECK(choice.synthesizedBits == STYLE_ITALIC);
}

TEST_CASE("probe order is honoured strictly")
{
    // chooseStyleFace applies no policy of its own — the caller flattens
    // styleDowngradeOrder x styledPathCandidates and the first exists+resident entry
    // wins. If this ever picks index 1 the two layers have started disagreeing about
    // who owns preference.
    const StyleFaceProbe probes[] = {
        {STYLE_BOLD, true, true},
        {STYLE_BOLD, true, true},
    };
    CHECK(chooseStyleFace(STYLE_BOLD, probes).index == 0);
}

// ------------------------------------------------------------
// The default font's styled faces
// ------------------------------------------------------------

TEST_CASE("every default-font cache key is recognised as a sentinel")
{
    // This is the guard that keeps a sentinel out of asset::AssetRef::fromPath, which
    // normalizes against the project root and writes a .vfmeta next to unknown files.
    // VK-1628 only had to cover one key; missing any of VK-1636's three would litter
    // the project with metadata for files named "__default_font_bold__".
    CHECK(resource::isDefaultFontSentinel(resource::DEFAULT_FONT_SENTINEL));
    CHECK(resource::isDefaultFontSentinel(resource::DEFAULT_FONT_BOLD_SENTINEL));
    CHECK(resource::isDefaultFontSentinel(resource::DEFAULT_FONT_ITALIC_SENTINEL));
    CHECK(resource::isDefaultFontSentinel(resource::DEFAULT_FONT_BOLD_ITALIC_SENTINEL));

    CHECK_FALSE(resource::isDefaultFontSentinel(""));
    CHECK_FALSE(resource::isDefaultFontSentinel("assets/Roboto.vfFont"));
    CHECK_FALSE(resource::isDefaultFontSentinel("__default_font"));
}

TEST_CASE("the four default-font sentinels are distinct cache keys")
{
    // They index the same std::unordered_map as real font paths; a collision would
    // silently make bold text draw the regular atlas.
    const std::set<std::string> keys = {
        resource::DEFAULT_FONT_SENTINEL,
        resource::DEFAULT_FONT_BOLD_SENTINEL,
        resource::DEFAULT_FONT_ITALIC_SENTINEL,
        resource::DEFAULT_FONT_BOLD_ITALIC_SENTINEL,
    };
    CHECK(keys.size() == 4);
}

TEST_CASE("style bits map to the matching default sentinel and engine path")
{
    CHECK(std::string(resource::defaultFontSentinelForStyle(text::STYLE_BOLD)) ==
          resource::DEFAULT_FONT_BOLD_SENTINEL);
    CHECK(std::string(resource::defaultFontSentinelForStyle(text::STYLE_ITALIC)) ==
          resource::DEFAULT_FONT_ITALIC_SENTINEL);
    CHECK(std::string(resource::defaultFontSentinelForStyle(text::STYLE_BOLD | text::STYLE_ITALIC)) ==
          resource::DEFAULT_FONT_BOLD_ITALIC_SENTINEL);

    SUBCASE("an unstyled or out-of-range request yields the base sentinel, never a path")
    {
        CHECK(std::string(resource::defaultFontSentinelForStyle(0)) ==
              resource::DEFAULT_FONT_SENTINEL);
        CHECK(std::string(resource::defaultFontSentinelForStyle(9)) ==
              resource::DEFAULT_FONT_SENTINEL);
        CHECK(resource::defaultFontEnginePathForStyle(0) == nullptr);
        CHECK(resource::defaultFontEnginePathForStyle(9) == nullptr);
    }

    SUBCASE("engine paths keep the prefix PathResolver byte-compares against")
    {
        // resolveEnginePath rewrites "../../resources/..." to "resources/..." for an
        // exported build, and that rewritten string is the .vfpak archive key. Any
        // other spelling silently skips the rewrite and breaks only the shipped game.
        for (const uint32_t bits : {text::STYLE_BOLD, text::STYLE_ITALIC,
                                    text::STYLE_BOLD | text::STYLE_ITALIC})
        {
            const std::string path = resource::defaultFontEnginePathForStyle(bits);
            CHECK(path.rfind("../../resources/fonts/", 0) == 0);
            CHECK(path.substr(path.size() - 7) == ".vfFont");
        }
    }

    SUBCASE("the styled engine paths are the naming convention applied to the base path")
    {
        // The default family and a user family must resolve by the same rule, or a
        // font that works in the editor stops working once assigned explicitly.
        for (const uint32_t bits : {text::STYLE_BOLD, text::STYLE_ITALIC,
                                    text::STYLE_BOLD | text::STYLE_ITALIC})
        {
            const auto candidates =
                text::styledPathCandidates(resource::DEFAULT_FONT_ENGINE_PATH, bits);
            REQUIRE_FALSE(candidates.empty());
            CHECK(candidates[0] == resource::defaultFontEnginePathForStyle(bits));
        }
    }
}

TEST_CASE("bits outside the style mask are ignored rather than leaking into the result")
{
    const StyleFaceProbe probes[] = {{STYLE_BOLD, true, true}};
    const StyleChoice choice = chooseStyleFace(STYLE_BOLD | 0xF0u, probes);
    CHECK(choice.index == 0);
    // 0xF0 must not survive into styleFlags — the shader would read whatever future
    // meaning those bits acquire.
    CHECK(choice.synthesizedBits == 0u);
}
