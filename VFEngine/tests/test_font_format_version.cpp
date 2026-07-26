#include <doctest.h>

#include <resource/FontResource.hpp>
#include <resource/Types.hpp>
#include <resource/EndianUtils.hpp>
#include <resource/VfFontHeader.hpp>
#include <types/FontSerializer.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// ============================================================
// VK-1627: the .vfFont format version is owned by VfFontHeader.hpp and is
// decoupled from the global engine version in config/Config.hpp. Before this
// change every .vfFont was stamped with Version::major/minor/patch and the
// loader strict-equality checked it, so any engine bump silently invalidated
// every font and blanked all text.
//
// Files are synthesized byte-for-byte here rather than round-tripped for most
// cases, because only a hand-written header can carry a *wrong* magic/version.
// The one round-trip case exercises the real writer so the two sides cannot
// drift apart in a future format bump (VK-1633/1634 MSDF).
// ============================================================

namespace
{
    namespace fs = std::filesystem;

    // Fixture payload. Shared by the byte writer and the in-memory builder so
    // checkLoaded() accepts a file produced by either.
    constexpr uint32_t kFixtureFlags =
        static_cast<uint32_t>(resource::FontFormatFlags::SDF_ENABLED) |
        static_cast<uint32_t>(resource::FontFormatFlags::KERNING_ENABLED);

    constexpr uint32_t kGlyphCodepoint = 0x41; // 'A'
    constexpr uint32_t kAtlasDimension = 4;
    constexpr uint32_t kAtlasBytes = kAtlasDimension * kAtlasDimension; // SDF_8 => 1 bpp
    constexpr unsigned char kAtlasFill = 0xFF;

    const std::string kFontName = "VK1627";
    const std::string kFontStyle = "Regular";

    struct TempDir
    {
        fs::path path;

        explicit TempDir(const char* name)
            : path(fs::temp_directory_path() / name)
        {
            fs::remove_all(path);
            fs::create_directories(path);
        }

        ~TempDir()
        {
            std::error_code ec;
            fs::remove_all(path, ec);
        }
    };

    // A header describing a valid, current-format font. Negative cases copy it
    // and perturb exactly one field.
    resource::VfFontHeader fixtureHeader()
    {
        resource::VfFontHeader header;
        header.formatFlags = kFixtureFlags;
        return header;
    }

    // Writes a minimal but fully valid .vfFont: 1 character range, 1 glyph, no
    // kerning pairs, and a 4x4 SDF_8 atlas whose declared size matches
    // width*height*bpp exactly (so the reader logs no size-mismatch warning).
    // When writeBody is false the file ends after the 21-byte header.
    void writeFontFile(const fs::path& path, const resource::VfFontHeader& header, bool writeBody = true)
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        REQUIRE(f.is_open());

        using namespace resource::endian;

        resource::writeVfFontHeader(f, header);
        if (!writeBody)
        {
            return;
        }

        // --- Metadata (length-prefixed strings, not null-terminated) ---
        writeLE<uint32_t>(f, static_cast<uint32_t>(kFontName.size()));
        f.write(kFontName.data(), static_cast<std::streamsize>(kFontName.size()));
        writeLE<uint32_t>(f, static_cast<uint32_t>(kFontStyle.size()));
        f.write(kFontStyle.data(), static_cast<std::streamsize>(kFontStyle.size()));

        writeLE<uint32_t>(f, 32u);     // baseFontSize
        writeLE<float>(f, 40.0f);      // lineHeight
        writeLE<float>(f, 32.0f);      // ascender
        writeLE<float>(f, -8.0f);      // descender
        writeLE<float>(f, -2.0f);      // underlinePosition
        writeLE<float>(f, 1.0f);       // underlineThickness

        writeLE<float>(f, 4.0f);       // sdf spread
        writeLE<uint32_t>(f, 4u);      // sdf padding
        writeLE<float>(f, 0.5f);       // sdf edgeValue
        writeLE<float>(f, 0.0f);       // MTSDF pxRange (legacy reserved bits were zero)

        // --- Character ranges ---
        writeLE<uint32_t>(f, 1u);
        writeLE<uint32_t>(f, kGlyphCodepoint); // rangeStart
        writeLE<uint32_t>(f, kGlyphCodepoint); // rangeEnd

        // --- Glyphs (must stay sorted by codepoint: FontData::findGlyph binary-searches) ---
        writeLE<uint32_t>(f, 1u);
        writeLE<uint32_t>(f, kGlyphCodepoint);
        writeLE<float>(f, 8.0f);       // advanceX
        writeLE<float>(f, 0.0f);       // advanceY
        writeLE<float>(f, 0.0f);       // bearingX
        writeLE<float>(f, 8.0f);       // bearingY
        writeLE<float>(f, 8.0f);       // glyphWidth
        writeLE<float>(f, 8.0f);       // glyphHeight
        writeLE<uint32_t>(f, 0u);      // atlasX
        writeLE<uint32_t>(f, 0u);      // atlasY
        writeLE<uint32_t>(f, kAtlasDimension); // atlasWidth
        writeLE<uint32_t>(f, kAtlasDimension); // atlasHeight
        writeLE<uint32_t>(f, 0u);      // glyphFlags

        // --- Kerning pairs ---
        writeLE<uint32_t>(f, 0u);

        // --- Atlas ---
        writeLE<uint32_t>(f, kAtlasDimension);
        writeLE<uint32_t>(f, kAtlasDimension);
        writeLE<uint32_t>(f, static_cast<uint32_t>(resource::FontAtlasFormat::SDF_8));
        writeLE<uint32_t>(f, kAtlasBytes);

        const std::vector<unsigned char> pixels(kAtlasBytes, kAtlasFill);
        f.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    }

    // The same font as writeFontFile(), in memory, for the FontSerializer round-trip.
    resource::FontData makeMinimalFont()
    {
        resource::FontData font;
        font.formatFlags = static_cast<resource::FontFormatFlags>(kFixtureFlags);

        font.metadata.fontName = kFontName;
        font.metadata.fontStyle = kFontStyle;
        font.metadata.baseFontSize = 32;
        font.metadata.lineHeight = 40.0f;
        font.metadata.ascender = 32.0f;
        font.metadata.descender = -8.0f;
        font.metadata.underlinePosition = -2.0f;
        font.metadata.underlineThickness = 1.0f;

        font.sdfParams.spread = 4.0f;
        font.sdfParams.padding = 4;
        font.sdfParams.edgeValue = 0.5f;
        font.sdfParams.pxRange = 0.0f;

        resource::CharacterRange range;
        range.rangeStart = kGlyphCodepoint;
        range.rangeEnd = kGlyphCodepoint;
        font.characterRanges.push_back(range);

        resource::GlyphData glyph;
        glyph.codepoint = kGlyphCodepoint;
        glyph.advanceX = 8.0f;
        glyph.advanceY = 0.0f;
        glyph.bearingX = 0.0f;
        glyph.bearingY = 8.0f;
        glyph.glyphWidth = 8.0f;
        glyph.glyphHeight = 8.0f;
        glyph.atlasX = 0;
        glyph.atlasY = 0;
        glyph.atlasWidth = kAtlasDimension;
        glyph.atlasHeight = kAtlasDimension;
        glyph.glyphFlags = 0;
        font.glyphs.push_back(glyph);

        font.atlas.width = kAtlasDimension;
        font.atlas.height = kAtlasDimension;
        font.atlas.format = resource::FontAtlasFormat::SDF_8;
        font.atlas.pixels.assign(kAtlasBytes, kAtlasFill);

        return font;
    }

    void checkLoaded(const resource::FontData& font)
    {
        CHECK(font.metadata.fontName == kFontName);
        CHECK(font.metadata.fontStyle == kFontStyle);
        CHECK(font.metadata.baseFontSize == 32u);
        CHECK(font.metadata.lineHeight == doctest::Approx(40.0f));
        CHECK(font.sdfParams.pxRange == doctest::Approx(0.0f));

        REQUIRE(font.characterRanges.size() == 1);
        CHECK(font.characterRanges[0].rangeStart == kGlyphCodepoint);

        REQUIRE(font.glyphs.size() == 1);
        CHECK(font.glyphs[0].codepoint == kGlyphCodepoint);
        CHECK(font.glyphs[0].advanceX == doctest::Approx(8.0f));

        CHECK(font.atlas.width == kAtlasDimension);
        CHECK(font.atlas.height == kAtlasDimension);
        CHECK(font.atlas.pixels.size() == kAtlasBytes);

        CHECK(resource::hasFlag(font.formatFlags, resource::FontFormatFlags::KERNING_ENABLED));
    }

    // loadFont signals EVERY error by returning a default-constructed FontData.
    // Three of its members are traps and must NOT be used as discriminators:
    //   headerFileType defaults to FileType::FONT, version defaults to the GLOBAL
    //   engine version, and formatFlags defaults to SDF_ENABLED. Only assert on
    //   members the fixture makes non-empty.
    void checkRejected(const resource::FontData& font)
    {
        CHECK(font.glyphs.empty());
        CHECK(font.characterRanges.empty());
        CHECK(font.metadata.fontName.empty());
        CHECK(font.atlas.width == 0u);
        CHECK(font.atlas.height == 0u);
        CHECK(font.atlas.pixels.empty());
        // formatFlags is assigned only after the magic and version gates pass, and
        // FontData{} defaults to SDF_ENABLED alone - so a missing KERNING_ENABLED
        // proves the loader bailed out in the header.
        CHECK_FALSE(resource::hasFlag(font.formatFlags, resource::FontFormatFlags::KERNING_ENABLED));
    }
}

TEST_SUITE("FontFormatVersion")
{
    TEST_CASE("a current-format .vfFont loads and its payload survives")
    {
        TempDir dir("vf_vk1627_current");
        const fs::path file = dir.path / "current.vfFont";
        writeFontFile(file, fixtureHeader());

        checkLoaded(resource::FontResource::loadFont(file.string()));
    }

    TEST_CASE("a loaded font reports the FONT format version, not the engine version")
    {
        TempDir dir("vf_vk1627_reported_version");
        const fs::path file = dir.path / "version.vfFont";
        writeFontFile(file, fixtureHeader());

        const resource::FontData font = resource::FontResource::loadFont(file.string());
        REQUIRE_FALSE(font.glyphs.empty()); // guard: the load actually succeeded

        CHECK(font.version.major == resource::FONT_FORMAT_VERSION_MAJOR);
        CHECK(font.version.minor == resource::FONT_FORMAT_VERSION_MINOR);
        CHECK(font.version.patch == resource::FONT_FORMAT_VERSION_PATCH);
    }

    TEST_CASE("a pre-VK-1627 .vfFont stamped 1.0.0 is rejected")
    {
        TempDir dir("vf_vk1627_legacy");
        const fs::path file = dir.path / "legacy.vfFont";

        // 1.0.0 are historical bytes already written to disk by the old writer;
        // they are deliberately hardcoded and must NOT track the engine version.
        // The magic stays valid so this isolates the version gate.
        resource::VfFontHeader header = fixtureHeader();
        header.versionMajor = 1;
        header.versionMinor = 0;
        header.versionPatch = 0;
        writeFontFile(file, header);

        checkRejected(resource::FontResource::loadFont(file.string()));
    }

    TEST_CASE("a wrong .vfFont format version is rejected on every component")
    {
        TempDir dir("vf_vk1627_wrong_version");
        const fs::path file = dir.path / "wrong.vfFont";
        resource::VfFontHeader header = fixtureHeader();

        SUBCASE("major")
        {
            header.versionMajor = resource::FONT_FORMAT_VERSION_MAJOR + 1;
        }
        SUBCASE("minor")
        {
            header.versionMinor = resource::FONT_FORMAT_VERSION_MINOR + 1;
        }
        SUBCASE("patch")
        {
            header.versionPatch = resource::FONT_FORMAT_VERSION_PATCH + 1;
        }

        writeFontFile(file, header);
        checkRejected(resource::FontResource::loadFont(file.string()));
    }

    TEST_CASE("a wrong FileType byte is rejected")
    {
        TempDir dir("vf_vk1627_wrong_type");
        const fs::path file = dir.path / "type.vfFont";

        resource::VfFontHeader header = fixtureHeader();
        header.fileType = static_cast<uint8_t>(resource::FileType::MESH);
        writeFontFile(file, header);

        checkRejected(resource::FontResource::loadFont(file.string()));
    }

    TEST_CASE("a wrong magic is rejected")
    {
        TempDir dir("vf_vk1627_wrong_magic");
        const fs::path file = dir.path / "magic.vfFont";

        // 'VFTR' is a terrain file - the most realistic confusion.
        resource::VfFontHeader header = fixtureHeader();
        header.magic = {'V', 'F', 'T', 'R'};
        writeFontFile(file, header);

        checkRejected(resource::FontResource::loadFont(file.string()));
    }

    TEST_CASE("a header-only truncated .vfFont is rejected")
    {
        TempDir dir("vf_vk1627_truncated");
        const fs::path file = dir.path / "truncated.vfFont";
        writeFontFile(file, fixtureHeader(), /*writeBody=*/false);

        REQUIRE(fs::file_size(file) == resource::FONT_HEADER_SIZE);
        // Reads past EOF yield zero-initialised values, so every count is 0 and the
        // load dies deterministically at the atlas-dimension gate (0x0).
        checkRejected(resource::FontResource::loadFont(file.string()));
    }

    TEST_CASE("a missing .vfFont is rejected")
    {
        TempDir dir("vf_vk1627_missing");
        const fs::path file = dir.path / "does_not_exist.vfFont";

        checkRejected(resource::FontResource::loadFont(file.string()));
    }

    TEST_CASE("FontSerializer round-trips through FontResource")
    {
        TempDir dir("vf_vk1627_roundtrip");

        REQUIRE(types::FontSerializer{}.saveToFile(
            dir.path.string(), "vk1627", makeMinimalFont()));

        const fs::path file = dir.path / "vk1627.vfFont";
        REQUIRE(fs::exists(file));
        CHECK(fs::file_size(file) > resource::FONT_HEADER_SIZE);

        // Pin the actual bytes the writer emits, not just what the reader accepts.
        std::ifstream raw(file, std::ios::binary);
        REQUIRE(raw.is_open());
        std::array<char, 4> magic{};
        raw.read(magic.data(), 4);
        CHECK(std::string(magic.data(), magic.size()) == "VFFT");

        checkLoaded(resource::FontResource::loadFont(file.string()));
    }

    TEST_CASE("MTSDF reuses the v2 wire layout and round-trips four-channel pixels")
    {
        TempDir dir("vf_vk1633_mtsdf_roundtrip");

        resource::FontData font = makeMinimalFont();
        font.formatFlags = font.formatFlags |
            resource::FontFormatFlags::SDF_ENABLED |
            resource::FontFormatFlags::MSDF_ENABLED;
        font.sdfParams.spread = 4.0f;
        font.sdfParams.padding = 0;
        font.sdfParams.edgeValue = 0.5f;
        font.sdfParams.pxRange = 4.0f;
        font.atlas.format = resource::FontAtlasFormat::MTSDF_RGBA_32;
        font.atlas.pixels.assign(
            static_cast<size_t>(kAtlasDimension) * kAtlasDimension * 4, 0x80);

        REQUIRE(types::FontSerializer{}.saveToFile(
            dir.path.string(), "mtsdf", font));

        const resource::FontData loaded =
            resource::FontResource::loadFont((dir.path / "mtsdf.vfFont").string());
        REQUIRE_FALSE(loaded.glyphs.empty());
        CHECK(loaded.atlas.format == resource::FontAtlasFormat::MTSDF_RGBA_32);
        CHECK(loaded.atlas.pixels == font.atlas.pixels);
        CHECK(loaded.isSDF());
        CHECK(loaded.isMSDF());
        CHECK(loaded.sdfParams.pxRange == doctest::Approx(4.0f));
        CHECK(loaded.version.major == 2u);
    }

    TEST_CASE("the .vfFont format constants have their expected values")
    {
        // Tripwire: VK-1633/1634 (MSDF) must update this consciously when they bump
        // the format, rather than silently invalidating every font.
        CHECK(std::string(resource::FONT_MAGIC.data(), resource::FONT_MAGIC.size()) == "VFFT");
        CHECK(resource::FONT_FORMAT_VERSION_MAJOR == 2u);
        CHECK(resource::FONT_FORMAT_VERSION_MINOR == 0u);
        CHECK(resource::FONT_FORMAT_VERSION_PATCH == 0u);
        CHECK(resource::FONT_HEADER_SIZE == std::size_t{21});
    }
}
