#include <doctest.h>

#include <impl/render/FontFallbackChainResolver.hpp>
#include <config/ProjectConfig.hpp>
#include <components/TextEffects.hpp>
#include <resource/VfFontHeader.hpp>
#include <text/FontStyleFace.hpp>
#include <text/RichTextParser.hpp>

#include <string>

// ============================================================
// VK-1638 — regressions found by code review of the font/text branch.
//
// Each case below pins the behaviour a defect got wrong, so a future refactor that
// reintroduces it fails here instead of in the editor. CPU-only: no Vulkan device, no
// mounted archive, no real font file.
// ============================================================

TEST_SUITE("VK1638ReviewFixes")
{
    // ---- A1: exported games resolve their font fallbacks -------------------------
    //
    // Fallbacks are stored relative to the project's Assets root. GameExporter ships
    // workingDirectory = "Assets" and archives assets as "Assets/" + relativePath, so
    // archive mode needs the SAME join as the editor - skipping it produced a key that
    // matched neither the archive nor the filesystem, and every fallback was dropped.

    TEST_CASE("font fallbacks resolve under the archive's Assets prefix")
    {
        config::ProjectConfig project;
        project.workingDirectory = "Assets";
        project.fontFallbackChain = {"Fonts/NotoCJK.vfFont", "Fonts/Emoji.vfFont"};

        const auto resolved = services::font_fallback::resolve(project, /*archiveMode=*/true);

        REQUIRE(resolved.size() == 2);
        CHECK(resolved[0] == "Assets/Fonts/NotoCJK.vfFont");
        CHECK(resolved[1] == "Assets/Fonts/Emoji.vfFont");
    }

    TEST_CASE("archive keys are forward-slashed regardless of the authored separator")
    {
        config::ProjectConfig project;
        project.workingDirectory = "Assets";
        project.fontFallbackChain = {"Fonts/Sub/Face.vfFont"};

        const auto resolved = services::font_fallback::resolve(project, /*archiveMode=*/true);

        REQUIRE(resolved.size() == 1);
        CHECK(resolved[0].find('\\') == std::string::npos);
        CHECK(resolved[0] == "Assets/Fonts/Sub/Face.vfFont");
    }

    TEST_CASE("an absolute fallback is left alone in both modes")
    {
        config::ProjectConfig project;
        project.workingDirectory = "Assets";
#ifdef _WIN32
        project.fontFallbackChain = {"C:/Fonts/Face.vfFont"};
#else
        project.fontFallbackChain = {"/fonts/Face.vfFont"};
#endif

        const auto archive = services::font_fallback::resolve(project, /*archiveMode=*/true);
        REQUIRE(archive.size() == 1);
        CHECK(archive[0].find("Assets") == std::string::npos);
    }

    TEST_CASE("the authored fallback limit is enforced by the resolver too")
    {
        config::ProjectConfig project;
        project.workingDirectory = "Assets";
        project.fontFallbackChain = {"a.vfFont", "b.vfFont", "c.vfFont", "d.vfFont"};

        const auto resolved = services::font_fallback::resolve(project, /*archiveMode=*/true);
        CHECK(resolved.size() == config::ProjectConfig::maxFontFallbacks);
    }

    TEST_CASE("only .vfFont import results trigger a font-cache invalidation")
    {
        CHECK(services::font_asset::isFontOutput("Assets/Fonts/Roboto.vfFont"));
        // The importer reports the path as it built it; the extension spelling is not
        // normalised anywhere in between.
        CHECK(services::font_asset::isFontOutput("Assets/Fonts/Roboto.vffont"));
        CHECK(services::font_asset::isFontOutput("Assets/Fonts/Roboto.VFFONT"));
        CHECK_FALSE(services::font_asset::isFontOutput("Assets/Textures/Roboto.vfImage"));
        CHECK_FALSE(services::font_asset::isFontOutput("Assets/Fonts/Roboto.ttf"));
        CHECK_FALSE(services::font_asset::isFontOutput(""));
    }

    // ---- B1: a demoted glyph keeps the style its span asked for -------------------
    //
    // layoutTextStyled borrows a glyph from the base face when the styled sibling lacks
    // that codepoint. The landed face then provides only what IT was requested for, so
    // the rest of the request still has to be synthesized - otherwise one glyph in the
    // middle of a bold word renders at regular weight.

    TEST_CASE("residualStyleBits reduces to the slot's own synthesis when not demoted")
    {
        using namespace text;

        // A Normal label on a family with no styled faces: nothing requested, nothing faked.
        CHECK(residualStyleBits(0, 0, 0) == 0u);

        // Bold requested, real Bold face found -> nothing left to synthesize.
        CHECK(residualStyleBits(STYLE_BOLD, STYLE_BOLD, 0) == 0u);

        // Bold requested, no Bold face -> the slot synthesizes it, and so does this.
        CHECK(residualStyleBits(STYLE_BOLD, STYLE_BOLD, STYLE_BOLD) == STYLE_BOLD);

        // BoldItalic requested, only a real Bold exists -> italic is the residual.
        CHECK(residualStyleBits(STYLE_BOLD | STYLE_ITALIC,
                                STYLE_BOLD | STYLE_ITALIC,
                                STYLE_ITALIC) == STYLE_ITALIC);
    }

    TEST_CASE("residualStyleBits re-synthesizes what a demotion took away")
    {
        using namespace text;

        // The [b] span resolved to a real Bold face (slot 1), but this glyph was demoted
        // to the Normal base slot (requested 0, synthesizes nothing). The base face is
        // not bold, so bold must be faked rather than silently dropped.
        CHECK(residualStyleBits(STYLE_BOLD, /*slotRequest=*/0, /*slotSynthesized=*/0)
              == STYLE_BOLD);

        // Italic label, [b] span, demoted to the label's own slot: that slot provides a
        // real italic, so only bold is left over.
        CHECK(residualStyleBits(STYLE_BOLD | STYLE_ITALIC,
                                /*slotRequest=*/STYLE_ITALIC,
                                /*slotSynthesized=*/0) == STYLE_BOLD);

        // Same, but the italic was being faked anyway - both axes stay faked.
        CHECK(residualStyleBits(STYLE_BOLD | STYLE_ITALIC,
                                /*slotRequest=*/STYLE_ITALIC,
                                /*slotSynthesized=*/STYLE_ITALIC)
              == (STYLE_BOLD | STYLE_ITALIC));
    }

    TEST_CASE("residualStyleBits never leaks a non-style bit")
    {
        using namespace text;
        // STYLE_TOFU is a renderer output flag, never a requested style.
        CHECK((residualStyleBits(STYLE_TOFU | STYLE_BOLD, 0, 0) & STYLE_TOFU) == 0u);
    }

    // ---- E2: a bare [shadow] inherits the label's colour --------------------------
    // (C1, the text-effect save gating, is covered through the public saveScene /
    //  loadSceneInto seam in test_text_serialization.cpp.)

    TEST_CASE("bare [shadow] keeps the label's own shadow colour")
    {
        const auto parsed = text::parseRichText("[shadow]x[/shadow]");
        REQUIRE(parsed.perCodepoint.size() == 1);
        REQUIRE(parsed.perCodepoint[0].hasShadow);
        CHECK_FALSE(parsed.perCodepoint[0].hasShadowColor);

        components::TextEffectSettings label;
        label.shadowColor = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f); // red
        label.shadowOffset = glm::vec2(2.0f, 2.0f);

        const auto resolved = parsed.perCodepoint[0].applyTo(label);
        CHECK(resolved.shadowColor.r == doctest::Approx(1.0f));
        CHECK(resolved.shadowColor.g == doctest::Approx(0.0f));
        CHECK(resolved.shadowColor.a == doctest::Approx(1.0f));
        // The offset was already inherited before this fix; keep it pinned.
        CHECK(resolved.shadowOffset.x == doctest::Approx(2.0f));
    }

    TEST_CASE("[shadow=...] still overrides the label's shadow colour")
    {
        const auto parsed = text::parseRichText("[shadow=#0000FFFF,1,1]x[/shadow]");
        REQUIRE(parsed.perCodepoint.size() == 1);
        REQUIRE(parsed.perCodepoint[0].hasShadow);
        CHECK(parsed.perCodepoint[0].hasShadowColor);

        components::TextEffectSettings label;
        label.shadowColor = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f); // red

        const auto resolved = parsed.perCodepoint[0].applyTo(label);
        CHECK(resolved.shadowColor.r == doctest::Approx(0.0f));
        CHECK(resolved.shadowColor.b == doctest::Approx(1.0f));
    }

    TEST_CASE("bare [shadow] on a label with no shadow still gets a usable default")
    {
        const auto parsed = text::parseRichText("[shadow]x[/shadow]");
        REQUIRE(parsed.perCodepoint.size() == 1);

        // A label that never touched its shadow: the struct default colour is the same
        // (0,0,0,0.5) the bare tag used to hard-code, so the tag is still not a no-op.
        const components::TextEffectSettings label;
        const auto resolved = parsed.perCodepoint[0].applyTo(label);
        CHECK(resolved.shadowColor.a == doctest::Approx(0.5f));
        CHECK(resolved.shadowOffset.x == doctest::Approx(text::RICH_TEXT_DEFAULT_SHADOW_OFFSET));
        CHECK(resolved.shadowOffset.y == doctest::Approx(text::RICH_TEXT_DEFAULT_SHADOW_OFFSET));
    }

    // ---- A3: a stale .vfFont is identified, not silently substituted --------------

    TEST_CASE("classifyVfFontHeader accepts exactly the current format")
    {
        const resource::VfFontHeader current; // defaults describe what the engine writes
        CHECK(resource::classifyVfFontHeader(current, resource::FONT_HEADER_SIZE) ==
              resource::FontHeaderStatus::Ok);
    }

    TEST_CASE("a pre-2.0.0 .vfFont is reported as needing a reimport, not as valid")
    {
        // Format 1.0.0 had no magic word at all - the first four bytes were the fileType
        // byte and the start of the engine's major version.
        resource::VfFontHeader legacy;
        legacy.magic = {'\0', '\0', '\0', '\0'};
        CHECK(resource::classifyVfFontHeader(legacy, resource::FONT_HEADER_SIZE) ==
              resource::FontHeaderStatus::NotAFont);
    }

    TEST_CASE("a future format version is distinguished from a corrupt file")
    {
        resource::VfFontHeader future;
        future.versionMajor = resource::FONT_FORMAT_VERSION_MAJOR + 1;
        CHECK(resource::classifyVfFontHeader(future, resource::FONT_HEADER_SIZE) ==
              resource::FontHeaderStatus::StaleVersion);

        resource::VfFontHeader patched;
        patched.versionPatch = resource::FONT_FORMAT_VERSION_PATCH + 1;
        CHECK(resource::classifyVfFontHeader(patched, resource::FONT_HEADER_SIZE) ==
              resource::FontHeaderStatus::StaleVersion);
    }

    TEST_CASE("a truncated file is Unreadable rather than a garbage version")
    {
        const resource::VfFontHeader current;
        CHECK(resource::classifyVfFontHeader(current, resource::FONT_HEADER_SIZE - 1) ==
              resource::FontHeaderStatus::Unreadable);
        CHECK(resource::classifyVfFontHeader(current, 0) ==
              resource::FontHeaderStatus::Unreadable);
    }

    TEST_CASE("a wrong fileType is rejected even with the right magic")
    {
        resource::VfFontHeader wrongType;
        wrongType.fileType = static_cast<uint8_t>(resource::FileType::MESH);
        CHECK(resource::classifyVfFontHeader(wrongType, resource::FONT_HEADER_SIZE) ==
              resource::FontHeaderStatus::NotAFont);
    }
}
