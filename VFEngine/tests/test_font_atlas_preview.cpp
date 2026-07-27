#include <doctest.h>

#include "test_repo_scan_helpers.hpp"

#include <math/MathHelper.hpp>
#include <resource/FontAtlasPreview.hpp>
#include <resource/FontResource.hpp>
#include <resource/Types.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

// ============================================================
// VK-1634: resource::fontAtlasToPreviewRGBA is the CPU bake behind the editor's font atlas
// preview. It used to be a private static on FontPreviewWindow, which Tests cannot reach -
// Editor is a ConsoleApp and is never linked here - so it lived untested while carrying the
// only CPU-side copy of the shaders' field reconstruction.
//
// The cases below are deliberately built on synthesised FontData rather than on an import,
// so the exact input bytes are known and the reconstruction can be asserted on values rather
// than on "something changed". One case runs the committed DefaultFont.vfFont through it as
// a real-data smoke test.
// ============================================================

namespace
{
    namespace fs = std::filesystem;

    resource::FontData makeAtlas(resource::FontAtlasFormat format,
                                 uint32_t width, uint32_t height,
                                 std::vector<unsigned char> pixels)
    {
        resource::FontData font;
        font.atlas.format = format;
        font.atlas.width = width;
        font.atlas.height = height;
        font.atlas.pixels = std::move(pixels);
        return font;
    }

    // Mirrors medianRGB() in resources/shaders/common/text_sdf.glsl.
    unsigned char median3(unsigned char r, unsigned char g, unsigned char b)
    {
        return (std::max)((std::min)(r, g), (std::min)((std::max)(r, g), b));
    }
}

TEST_SUITE("FontAtlasPreview")
{
    TEST_CASE("every atlas format bakes to a full RGBA image")
    {
        SUBCASE("GRAYSCALE_8 puts raw coverage in alpha and leaves the glyph white")
        {
            auto font = makeAtlas(resource::FontAtlasFormat::GRAYSCALE_8, 2, 2,
                                  {0, 64, 200, 255});

            const auto preview = resource::fontAtlasToPreviewRGBA(font);

            REQUIRE(preview.mipData.size() == 1);
            REQUIRE(preview.mipData[0].data.size() == 2u * 2u * 4u);
            CHECK(preview.width == 2u);
            CHECK(preview.height == 2u);
            CHECK(preview.numbersOfChannels == 4u);

            const auto& out = preview.mipData[0].data;
            for (size_t i = 0; i < 4; ++i)
            {
                CAPTURE(i);
                CHECK(out[i * 4 + 0] == 255);
                CHECK(out[i * 4 + 1] == 255);
                CHECK(out[i * 4 + 2] == 255);
            }
            CHECK(out[0 * 4 + 3] == 0);
            CHECK(out[1 * 4 + 3] == 64);
            CHECK(out[2 * 4 + 3] == 200);
            CHECK(out[3 * 4 + 3] == 255);
        }

        SUBCASE("RGBA_32 colour emoji passes through byte for byte")
        {
            const std::vector<unsigned char> pixels{
                1, 2, 3, 4,   250, 240, 230, 220,
                0, 0, 0, 0,   17, 34, 51, 68};
            auto font = makeAtlas(resource::FontAtlasFormat::RGBA_32, 2, 2, pixels);

            const auto preview = resource::fontAtlasToPreviewRGBA(font);

            REQUIRE(preview.mipData.size() == 1);
            CHECK(preview.mipData[0].data == pixels);
        }

        SUBCASE("SDF_8 reconstructs through the font's own edge and spread")
        {
            auto font = makeAtlas(resource::FontAtlasFormat::SDF_8, 2, 2,
                                  {0, 128, 255, 128});
            font.formatFlags = resource::FontFormatFlags::SDF_ENABLED;
            font.sdfParams.spread = 4.0f;
            font.sdfParams.edgeValue = 0.5f;
            font.sdfParams.pxRange = 0.0f;

            const auto preview = resource::fontAtlasToPreviewRGBA(font);
            const auto& out = preview.mipData[0].data;

            // Fully outside / fully inside are unambiguous; the on-edge texel must land in
            // the ramp rather than snapping to either extreme.
            CHECK(out[0 * 4 + 3] == 0);
            CHECK(out[2 * 4 + 3] == 255);
            CHECK(out[1 * 4 + 3] > 0);
            CHECK(out[1 * 4 + 3] < 255);
            CHECK(out[1 * 4 + 3] == out[3 * 4 + 3]);
        }
    }

    // The whole point of MTSDF is that the edge comes from the MEDIAN of RGB. Reading the red
    // channel (the pre-VK-1633 behaviour) or the max would give a visibly different answer on
    // these texels, so this is a behavioural check and not a formula restatement.
    TEST_CASE("MTSDF preview reconstructs the median, not a single channel")
    {
        // px0 outside everywhere; px1 inside everywhere; px2 median 128 with a misleading
        // red and a zero source alpha; px3 median 0 with a misleading green.
        auto font = makeAtlas(resource::FontAtlasFormat::MTSDF_RGBA_32, 2, 2,
                              {  0,   0,   0,   0,
                               255, 255, 255, 255,
                               255,   0, 128,   0,
                                 0, 255,   0, 255});
        font.formatFlags = resource::FontFormatFlags::SDF_ENABLED |
                           resource::FontFormatFlags::MSDF_ENABLED;
        font.sdfParams.spread = 4.0f;
        font.sdfParams.edgeValue = 0.5f;
        font.sdfParams.pxRange = 4.0f;

        REQUIRE(median3(255, 0, 128) == 128);
        REQUIRE(median3(0, 255, 0) == 0);

        const auto preview = resource::fontAtlasToPreviewRGBA(font);
        REQUIRE(preview.mipData.size() == 1);
        const auto& out = preview.mipData[0].data;
        REQUIRE(out.size() == 2u * 2u * 4u);

        for (size_t i = 0; i < 4; ++i)
        {
            CAPTURE(i);
            CHECK(out[i * 4 + 0] == 255);
            CHECK(out[i * 4 + 1] == 255);
            CHECK(out[i * 4 + 2] == 255);
        }

        CHECK(out[0 * 4 + 3] == 0);
        CHECK(out[1 * 4 + 3] == 255);

        // Median 128 sits on the 0.5 iso-value, so it must land mid-ramp. Reading .r (255)
        // would give 255 and reading max(rgb) on px3 would give 255 as well.
        CHECK(out[2 * 4 + 3] > 0);
        CHECK(out[2 * 4 + 3] < 255);
        CHECK(out[3 * 4 + 3] == 0);

        // The band is the shared helper the fragment shader mirrors, evaluated at the
        // preview's native 1:1 scale.
        const float edgeByte = font.sdfParams.edgeValue * 255.0f;
        const float screenRange = sdf::mtsdfScreenPxRange(font.sdfParams.pxRange, 1.0f);
        const float smoothByte = sdf::mtsdfHalfBand(screenRange) * 255.0f;
        CHECK(out[2 * 4 + 3] == sdf::sdfToAlphaByte(128, edgeByte, smoothByte));

        // For any pxRange the validators accept ([1, 16]) the new helpers reproduce the
        // literal (0.5 / pxRange) * 255 this branch used before VK-1634.
        CHECK(smoothByte == doctest::Approx((0.5f / font.sdfParams.pxRange) * 255.0f));
    }

    TEST_CASE("an inconsistent atlas is rejected instead of read out of bounds")
    {
        SUBCASE("zero dimensions")
        {
            auto font = makeAtlas(resource::FontAtlasFormat::GRAYSCALE_8, 0, 4, {});
            CHECK_THROWS_AS(resource::fontAtlasToPreviewRGBA(font), std::runtime_error);
        }

        SUBCASE("unknown format has no bytes per pixel")
        {
            auto font = makeAtlas(static_cast<resource::FontAtlasFormat>(99), 2, 2,
                                  {0, 0, 0, 0});
            CHECK_THROWS_AS(resource::fontAtlasToPreviewRGBA(font), std::runtime_error);
        }

        SUBCASE("pixel buffer disagrees with width * height * bytesPerPixel")
        {
            // MTSDF is four bytes per pixel, so 2x2 needs 16 - not 4.
            auto font = makeAtlas(resource::FontAtlasFormat::MTSDF_RGBA_32, 2, 2,
                                  {0, 0, 0, 0});
            CHECK_THROWS_AS(resource::fontAtlasToPreviewRGBA(font), std::runtime_error);
        }
    }

    // Real-data smoke test: the committed default font is a legacy SDF_8 bake, so this also
    // guards that VK-1634 left the legacy preview path alone.
    TEST_CASE("the committed default font bakes to a plausible preview")
    {
        const auto root = repo_scan::findRepoRoot();
        REQUIRE_MESSAGE(root.has_value(), "could not locate the repo root from Tests.exe");
        const fs::path fontPath = *root / "resources" / "fonts" / "DefaultFont.vfFont";
        REQUIRE_MESSAGE(fs::exists(fontPath), "missing " << fontPath.string());

        const resource::FontData font = resource::FontResource::loadFont(fontPath.string());
        REQUIRE_FALSE(font.glyphs.empty());
        REQUIRE(font.atlas.format == resource::FontAtlasFormat::SDF_8);

        const auto preview = resource::fontAtlasToPreviewRGBA(font);
        REQUIRE(preview.mipData.size() == 1);
        CHECK(preview.width == font.atlas.width);
        CHECK(preview.height == font.atlas.height);
        CHECK(preview.mipData[0].data.size() ==
              static_cast<size_t>(font.atlas.width) * font.atlas.height * 4);

        // A real glyph atlas has both fully transparent gutters and fully opaque stems.
        bool sawTransparent = false;
        bool sawOpaque = false;
        for (size_t i = 3; i < preview.mipData[0].data.size(); i += 4)
        {
            sawTransparent = sawTransparent || preview.mipData[0].data[i] == 0;
            sawOpaque = sawOpaque || preview.mipData[0].data[i] == 255;
        }
        CHECK(sawTransparent);
        CHECK(sawOpaque);
    }
}
