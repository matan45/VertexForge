#include <doctest.h>

#include <config/Config.hpp>
#include <resource/DefaultFont.hpp>
#include <resource/FontResource.hpp>
#include <resource/Types.hpp>
#include <resource/VfFontHeader.hpp>
#include <controllers/Import.hpp>
#include <registry/builtin/BuiltinImporters.hpp>

#include <filesystem>
#include <string>

// ============================================================
// VK-1628: the engine ships one default font so text with no fontRef still
// renders (see resource/DefaultFont.hpp).
//
// resources/fonts/DefaultFont.vfFont is a COMMITTED binary. The generator below
// is how it is produced, and it is skipped by default because it writes into the
// source tree:
//
//     Tests.exe --test-case="regenerate*" --no-skip
//
// Everything else here runs normally and guards the committed artifact: if a
// future format bump (e.g. MTSDF) invalidates it without a regeneration, the
// load-back case fails loudly instead of the engine silently rendering no text.
// ============================================================

namespace
{
    namespace fs = std::filesystem;

    // Tests.exe runs from bin/Tests/<Config>/x64/ and resources/ is NOT copied
    // there, so the repo has to be reached from the source location.
    fs::path repoRoot()
    {
        return fs::path(__FILE__).parent_path() // VFEngine/tests
            .parent_path()                      // VFEngine
            .parent_path();                     // repo root
    }

    fs::path defaultFontPath()
    {
        return repoRoot() / "resources" / "fonts" / "DefaultFont.vfFont";
    }
}

TEST_SUITE("DefaultFont")
{
    TEST_CASE("regenerate resources/fonts/DefaultFont.vfFont from Roboto" * doctest::skip())
    {
        const fs::path source = repoRoot() / "resources" / "editor" / "Roboto-Regular.ttf";
        REQUIRE_MESSAGE(fs::exists(source), "missing source face: " << source.string());

        const fs::path outDir = repoRoot() / "resources" / "fonts";
        std::error_code ec;
        fs::create_directories(outDir, ec);

        // The importer names its output from the source stem, so stage the face
        // under the name we want to ship.
        const fs::path stagedTtf = outDir / "DefaultFont.ttf";
        fs::copy_file(source, stagedTtf, fs::copy_options::overwrite_existing, ec);
        REQUIRE_FALSE(ec);

        import::builtin::ensureRegistered();
        controllers::Import::setLocation(outDir.string());

        importConfig::ImportConfig config;
        const auto result = controllers::Import::importFiles(
            {importConfig::ImportFiles(stagedTtf.string(), config)});
        CHECK(result.failureCount == 0);

        fs::remove(stagedTtf, ec);

        // FontSerializer::saveToFile returns void and swallows I/O errors, and
        // Font::loadFromFile ignores it — only the file on disk proves success.
        const fs::path out = outDir / ("DefaultFont." + FileExtension::font);
        REQUIRE(fs::exists(out));

        fs::path meta = out;
        meta += "." + FileExtension::assetMeta;
        CHECK(fs::exists(meta)); // commit this too: it pins the GUID across clones

        const resource::FontData font = resource::FontResource::loadFont(out.string());
        CHECK_FALSE(font.glyphs.empty());

        MESSAGE("Wrote " << out.string() << " (" << fs::file_size(out) << " bytes) — commit it.");
    }

    TEST_CASE("the committed default font parses at the current format version")
    {
        const fs::path path = defaultFontPath();
        REQUIRE_MESSAGE(fs::exists(path),
                        "missing " << path.string()
                        << " — run: Tests.exe --test-case=\"regenerate*\" --no-skip");

        const resource::FontData font = resource::FontResource::loadFont(path.string());

        // loadFont returns a default-constructed FontData on every failure, so an
        // empty glyph table is how a version/magic mismatch surfaces here.
        REQUIRE_FALSE(font.glyphs.empty());
        CHECK(font.version.major == resource::FONT_FORMAT_VERSION_MAJOR);
        CHECK(font.version.minor == resource::FONT_FORMAT_VERSION_MINOR);
        CHECK(font.version.patch == resource::FONT_FORMAT_VERSION_PATCH);

        CHECK(font.atlas.width > 0u);
        CHECK(font.atlas.height > 0u);
        CHECK_FALSE(font.atlas.pixels.empty());

        // Basic Latin must be covered or the fallback cannot render ASCII.
        CHECK(font.findGlyph('A') != nullptr);
        CHECK(font.findGlyph('z') != nullptr);
        CHECK(font.findGlyph(' ') != nullptr);
    }

    TEST_CASE("the default-font sentinel is not a usable filesystem path")
    {
        // The sentinel is a TextFontCache key. If it ever reached a loader it would
        // resolve to nothing — this pins that it stays distinguishable from a path.
        const std::string sentinel = resource::DEFAULT_FONT_SENTINEL;
        CHECK(sentinel == "__default_font__");
        CHECK_FALSE(fs::exists(sentinel));

        // PathResolver rewrites this prefix byte-exactly; a change here silently
        // breaks only the exported game.
        const std::string enginePath = resource::DEFAULT_FONT_ENGINE_PATH;
        CHECK(enginePath.rfind("../../resources/", 0) == 0);
    }
}
