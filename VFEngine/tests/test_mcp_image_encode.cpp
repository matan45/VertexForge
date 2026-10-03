#include <doctest.h>

#include "util/ImageEncode.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>

// Tests has no stb_image implementation of its own (Import.dll's copy is not
// exported), so decode through a file-local static one.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    std::vector<uint16_t> halfPixel(float r, float g, float b, float a)
    {
        return {glm::packHalf1x16(r), glm::packHalf1x16(g), glm::packHalf1x16(b), glm::packHalf1x16(a)};
    }

    std::string base64(const std::string& text)
    {
        return mcp::util::base64Encode(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    }
}

TEST_CASE("mcp image encode: half to rgba8 sRGB-encodes, clamps and forces opaque alpha")
{
    std::vector<uint16_t> halves;
    for (const auto& pixel : {halfPixel(0.0f, 0.5f, 1.0f, 0.0f),
                              halfPixel(2.0f, -1.0f, 0.25f, 0.5f)})
    {
        halves.insert(halves.end(), pixel.begin(), pixel.end());
    }

    const std::vector<uint8_t> rgba = mcp::util::halfRgbaToRgba8(halves, 2, 1);
    REQUIRE(rgba.size() == 8);
    CHECK(rgba[0] == 0);
    CHECK(rgba[1] == 188);  // sRGB(0.5) = 0.7354 -> 187.52
    CHECK(rgba[2] == 255);
    CHECK(rgba[3] == 255);  // alpha forced
    CHECK(rgba[4] == 255);  // > 1 clamps
    CHECK(rgba[5] == 0);    // negative clamps
    CHECK(rgba[6] == 137);  // sRGB(0.25) = 0.5371 -> 136.96
    CHECK(rgba[7] == 255);

    SUBCASE("too few halves yields empty")
    {
        CHECK(mcp::util::halfRgbaToRgba8(halves, 3, 1).empty());
        CHECK(mcp::util::halfRgbaToRgba8({}, 0, 0).empty());
    }
}

TEST_CASE("mcp image encode: downscale dimensions and box filter")
{
    uint32_t w = 0;
    uint32_t h = 0;

    SUBCASE("16:9 to maxWidth keeps the aspect ratio")
    {
        std::vector<uint8_t> src(static_cast<std::size_t>(1920) * 1080 * 4, 7);
        const auto out = mcp::util::downscaleRgba8(src, 1920, 1080, 1280, w, h);
        CHECK(w == 1280);
        CHECK(h == 720);
        REQUIRE(out.size() == static_cast<std::size_t>(1280) * 720 * 4);
        CHECK(out.front() == 7);
        CHECK(out.back() == 7);
    }

    SUBCASE("very wide image keeps at least one row")
    {
        std::vector<uint8_t> src(static_cast<std::size_t>(1000) * 1 * 4, 0);
        const auto out = mcp::util::downscaleRgba8(src, 1000, 1, 64, w, h);
        CHECK(w == 64);
        CHECK(h == 1);
        CHECK(out.size() == static_cast<std::size_t>(64) * 4);
    }

    SUBCASE("narrower than maxWidth is a pass-through copy")
    {
        std::vector<uint8_t> src{1, 2, 3, 4, 5, 6, 7, 8};
        const auto out = mcp::util::downscaleRgba8(src, 2, 1, 64, w, h);
        CHECK(w == 2);
        CHECK(h == 1);
        CHECK(out == src);
    }

    SUBCASE("box filter averages source pixels")
    {
        // 4x2 -> 2x1: each output pixel averages a 2x2 block.
        std::vector<uint8_t> src{
            0, 0, 0, 255,     255, 255, 255, 255,   10, 20, 30, 255,  10, 20, 30, 255,
            255, 255, 255, 255, 0, 0, 0, 255,       30, 40, 50, 255,  30, 40, 50, 255
        };
        const auto out = mcp::util::downscaleRgba8(src, 4, 2, 2, w, h);
        CHECK(w == 2);
        CHECK(h == 1);
        REQUIRE(out.size() == 8);
        CHECK(out[0] == 128);  // (0 + 255 + 255 + 0) / 4 = 127.5 rounds up
        CHECK(out[3] == 255);
        CHECK(out[4] == 20);
        CHECK(out[5] == 30);
        CHECK(out[6] == 40);
        CHECK(out[7] == 255);
    }

    SUBCASE("undersized source yields empty")
    {
        std::vector<uint8_t> src(4, 0);
        const auto out = mcp::util::downscaleRgba8(src, 2, 2, 1, w, h);
        CHECK(out.empty());
        CHECK(w == 0);
        CHECK(h == 0);
    }
}

TEST_CASE("mcp image encode: PNG round-trips through stb_image")
{
    const uint32_t width = 3;
    const uint32_t height = 2;
    std::vector<uint8_t> rgba(static_cast<std::size_t>(width) * height * 4);
    for (std::size_t i = 0; i < rgba.size(); ++i)
    {
        rgba[i] = static_cast<uint8_t>(i * 11);
    }

    const std::vector<uint8_t> png = mcp::util::encodePng(rgba, width, height);
    REQUIRE(png.size() > 8);
    const uint8_t signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    CHECK(std::memcmp(png.data(), signature, 8) == 0);

    int decodedWidth = 0;
    int decodedHeight = 0;
    int channels = 0;
    stbi_uc* decoded = stbi_load_from_memory(png.data(), static_cast<int>(png.size()),
                                             &decodedWidth, &decodedHeight, &channels, 4);
    REQUIRE(decoded != nullptr);
    CHECK(decodedWidth == static_cast<int>(width));
    CHECK(decodedHeight == static_cast<int>(height));
    CHECK(std::memcmp(decoded, rgba.data(), rgba.size()) == 0);  // row 0 stays the top row
    stbi_image_free(decoded);

    CHECK(mcp::util::encodePng(rgba, width, height + 1).empty());
    CHECK(mcp::util::encodePng({}, 0, 0).empty());
}

TEST_CASE("mcp image encode: base64 matches RFC 4648 test vectors")
{
    CHECK(base64("") == "");
    CHECK(base64("f") == "Zg==");
    CHECK(base64("fo") == "Zm8=");
    CHECK(base64("foo") == "Zm9v");
    CHECK(base64("foob") == "Zm9vYg==");
    CHECK(base64("fooba") == "Zm9vYmE=");
    CHECK(base64("foobar") == "Zm9vYmFy");

    const uint8_t high[3] = {0xFB, 0xFF, 0xFE};
    CHECK(mcp::util::base64Encode(high, 3) == "+//+");
}
