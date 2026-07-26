#include <doctest.h>

#include <config/Config.hpp>
#include <controllers/Import.hpp>
#include <registry/builtin/BuiltinImporters.hpp>
#include <resource/FontResource.hpp>
#include <resource/Types.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

// VK-1633 end-to-end authoring tests deliberately reach the importer only
// through its public controller facade. Tests has no FreeType or msdfgen include
// directory, which also proves their types do not leak across Import.dll.

namespace
{
    namespace fs = std::filesystem;

    struct TempDir
    {
        fs::path path;

        explicit TempDir(const char* tag)
            : path(fs::temp_directory_path() / (std::string("vf_vk1633_") + tag))
        {
            std::error_code ec;
            fs::remove_all(path, ec);
            fs::create_directories(path, ec);
        }

        ~TempDir()
        {
            std::error_code ec;
            fs::remove_all(path, ec);
        }
    };

    fs::path repoRoot()
    {
        return fs::path(__FILE__).parent_path().parent_path().parent_path();
    }

    fs::path robotoSource()
    {
        return repoRoot() / "resources" / "editor" / "Roboto-Regular.ttf";
    }

    fs::path importRoboto(
        const fs::path& dir,
        const std::string& stem,
        const std::map<std::string, importConfig::ImportOptionValue>& options)
    {
        std::error_code ec;
        const fs::path staged = dir / (stem + ".ttf");
        fs::copy_file(robotoSource(), staged, fs::copy_options::overwrite_existing, ec);
        if (ec)
            return {};

        import::builtin::ensureRegistered();
        controllers::Import::setLocation(dir.string());

        importConfig::ImportConfig config;
        config.customOptions = options;
        const auto result = controllers::Import::importFiles(
            {importConfig::ImportFiles(staged.string(), config)});
        if (result.failureCount != 0)
            return {};

        const fs::path output = dir / (stem + "." + FileExtension::font);
        return fs::exists(output) ? output : fs::path{};
    }

    std::vector<unsigned char> readBytes(const fs::path& path)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
            return {};
        const auto end = file.tellg();
        if (end <= 0)
            return {};
        std::vector<unsigned char> bytes(static_cast<size_t>(end));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        return file ? bytes : std::vector<unsigned char>{};
    }

    uint8_t medianByte(uint8_t r, uint8_t g, uint8_t b)
    {
        return std::max(std::min(r, g), std::min(std::max(r, g), b));
    }

    uint8_t mtsdfMedianAt(const resource::FontData& font, uint32_t x, uint32_t y)
    {
        const size_t index =
            (static_cast<size_t>(y) * font.atlas.width + static_cast<size_t>(x)) * 4;
        return medianByte(font.atlas.pixels[index], font.atlas.pixels[index + 1],
                          font.atlas.pixels[index + 2]);
    }
}

TEST_SUITE("FontMTSDF")
{
    TEST_CASE("MTSDF import is colored, correctly oriented, stable, and metric-compatible")
    {
        REQUIRE_MESSAGE(fs::exists(robotoSource()),
                        "missing fixture: " << robotoSource().string());

        TempDir sdfDir("sdf");
        TempDir grayscaleDir("grayscale");
        TempDir mtsdfDir("mtsdf");
        TempDir repeatDir("repeat");

        const fs::path sdfPath = importRoboto(sdfDir.path, "DefaultFont", {});

        std::map<std::string, importConfig::ImportOptionValue> grayscaleOptions;
        grayscaleOptions["fieldMode"] = int32_t{0};
        grayscaleOptions["includeLatin1Supplement"] = false;
        const fs::path grayscalePath =
            importRoboto(grayscaleDir.path, "Grayscale", grayscaleOptions);

        std::map<std::string, importConfig::ImportOptionValue> mtsdfOptions;
        mtsdfOptions["fieldMode"] = int32_t{2};
        mtsdfOptions["mtsdfPxRange"] = 4.0f;
        mtsdfOptions["includeLatin1Supplement"] = false;
        const fs::path mtsdfPath = importRoboto(mtsdfDir.path, "MTSDF", mtsdfOptions);
        const fs::path repeatPath = importRoboto(repeatDir.path, "MTSDFRepeat", mtsdfOptions);

        REQUIRE_FALSE(sdfPath.empty());
        REQUIRE_FALSE(grayscalePath.empty());
        REQUIRE_FALSE(mtsdfPath.empty());
        REQUIRE_FALSE(repeatPath.empty());

        const resource::FontData sdf = resource::FontResource::loadFont(sdfPath.string());
        const resource::FontData grayscale =
            resource::FontResource::loadFont(grayscalePath.string());
        const resource::FontData mtsdf =
            resource::FontResource::loadFont(mtsdfPath.string());

        REQUIRE_FALSE(sdf.glyphs.empty());
        REQUIRE_FALSE(grayscale.glyphs.empty());
        REQUIRE_FALSE(mtsdf.glyphs.empty());

        CHECK(sdf.atlas.format == resource::FontAtlasFormat::SDF_8);
        CHECK(sdf.atlas.pixels.size() ==
              static_cast<size_t>(sdf.atlas.width) * sdf.atlas.height);
        CHECK(sdf.isSDF());
        CHECK_FALSE(sdf.isMSDF());
        CHECK(sdf.sdfParams.pxRange == doctest::Approx(0.0f));

        CHECK(grayscale.atlas.format == resource::FontAtlasFormat::GRAYSCALE_8);
        CHECK(grayscale.atlas.pixels.size() ==
              static_cast<size_t>(grayscale.atlas.width) * grayscale.atlas.height);
        CHECK_FALSE(grayscale.isSDF());
        CHECK_FALSE(grayscale.isMSDF());

        CHECK(mtsdf.atlas.format == resource::FontAtlasFormat::MTSDF_RGBA_32);
        CHECK(mtsdf.atlas.pixels.size() ==
              static_cast<size_t>(mtsdf.atlas.width) * mtsdf.atlas.height * 4);
        CHECK(mtsdf.isSDF());
        CHECK(mtsdf.isMSDF());
        CHECK(mtsdf.sdfParams.pxRange == doctest::Approx(4.0f));
        CHECK(mtsdf.sdfParams.edgeValue == doctest::Approx(0.5f));
        CHECK(mtsdf.sdfParams.padding == 0u);

        const auto* a = mtsdf.findGlyph('A');
        REQUIRE(a != nullptr);
        REQUIRE(a->atlasWidth > 0u);
        REQUIRE(a->atlasHeight > 0u);

        bool channelsDiffer = false;
        bool foundInside = false;
        bool foundOutside = false;
        for (uint32_t y = 0; y < a->atlasHeight; ++y)
        {
            for (uint32_t x = 0; x < a->atlasWidth; ++x)
            {
                const size_t index =
                    (static_cast<size_t>(a->atlasY + y) * mtsdf.atlas.width +
                     static_cast<size_t>(a->atlasX + x)) * 4;
                const uint8_t r = mtsdf.atlas.pixels[index];
                const uint8_t g = mtsdf.atlas.pixels[index + 1];
                const uint8_t b = mtsdf.atlas.pixels[index + 2];
                const uint8_t alpha = mtsdf.atlas.pixels[index + 3];
                channelsDiffer = channelsDiffer || r != g || g != b;
                const uint8_t median = medianByte(r, g, b);
                foundInside = foundInside || (median > 127 && alpha > 127);
                foundOutside = foundOutside || (median < 127 && alpha < 127);
            }
        }
        CHECK(channelsDiffer);
        CHECK(foundInside);
        CHECK(foundOutside);

        // Roboto's F has substantially more filled pixels in its upper third
        // than its lower third. This relationship reverses if atlas rows flip.
        const auto* f = mtsdf.findGlyph('F');
        REQUIRE(f != nullptr);
        REQUIRE(f->atlasHeight >= 3u);
        uint32_t upperInside = 0;
        uint32_t lowerInside = 0;
        const uint32_t third = f->atlasHeight / 3u;
        for (uint32_t y = 0; y < third; ++y)
        {
            for (uint32_t x = 0; x < f->atlasWidth; ++x)
            {
                upperInside += mtsdfMedianAt(mtsdf, f->atlasX + x, f->atlasY + y) > 127;
                lowerInside +=
                    mtsdfMedianAt(mtsdf, f->atlasX + x,
                                  f->atlasY + f->atlasHeight - 1u - y) > 127;
            }
        }
        CHECK(upperInside > lowerInside);

        const auto* sdfH = sdf.findGlyph('H');
        const auto* mtsdfH = mtsdf.findGlyph('H');
        REQUIRE(sdfH != nullptr);
        REQUIRE(mtsdfH != nullptr);
        CHECK(mtsdfH->advanceX == doctest::Approx(sdfH->advanceX).epsilon(0.001));
        CHECK(std::abs(mtsdfH->bearingX - sdfH->bearingX) <= 2.0f);
        CHECK(std::abs(mtsdfH->bearingY - sdfH->bearingY) <= 2.0f);
        CHECK(std::abs(static_cast<int>(mtsdfH->atlasWidth) -
                       static_cast<int>(sdfH->atlasWidth)) <= 4);
        CHECK(std::abs(static_cast<int>(mtsdfH->atlasHeight) -
                       static_cast<int>(sdfH->atlasHeight)) <= 4);

        const auto* grayA = grayscale.findGlyph('A');
        const auto* sdfA = sdf.findGlyph('A');
        REQUIRE(grayA != nullptr);
        REQUIRE(sdfA != nullptr);
        CHECK(grayA->atlasWidth < sdfA->atlasWidth);
        CHECK(grayA->atlasHeight < sdfA->atlasHeight);

        const auto committedDefault =
            readBytes(repoRoot() / "resources" / "fonts" / "DefaultFont.vfFont");
        REQUIRE_FALSE(committedDefault.empty());
        CHECK(readBytes(sdfPath) == committedDefault);

        CHECK(readBytes(mtsdfPath) == readBytes(repeatPath));
    }
}
