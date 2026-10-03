#include "ImageEncode.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <climits>
#include <cmath>

// File-local copy: Import.dll has its own (non-static) stb_image_write, which is
// not exported, so Mcp compiles a private one.
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace mcp::util
{
    namespace
    {
        uint8_t toUnorm8(float value)
        {
            // Written so NaN (and negatives) map to 0.
            if (!(value > 0.0f))
            {
                return 0;
            }
            if (value >= 1.0f)
            {
                return 255;
            }
            return static_cast<uint8_t>(std::lround(value * 255.0f));
        }

        // IEC 61966-2-1 linear -> sRGB transfer, the encode the SRGB swapchain applies.
        float encodeSrgb(float value)
        {
            if (!(value > 0.0f))
            {
                return 0.0f;
            }
            if (value >= 1.0f)
            {
                return 1.0f;
            }
            return value <= 0.0031308f ? value * 12.92f
                                       : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
        }

        void appendToVector(void* context, void* data, int size)
        {
            auto* out = static_cast<std::vector<uint8_t>*>(context);
            const auto* bytes = static_cast<const uint8_t*>(data);
            out->insert(out->end(), bytes, bytes + size);
        }
    }

    std::vector<uint8_t> halfRgbaToRgba8(const std::vector<uint16_t>& rgba16f, uint32_t width, uint32_t height)
    {
        const std::size_t pixelCount = static_cast<std::size_t>(width) * height;
        if (pixelCount == 0 || rgba16f.size() < pixelCount * 4)
        {
            return {};
        }

        std::vector<uint8_t> out(pixelCount * 4);
        for (std::size_t i = 0; i < pixelCount; ++i)
        {
            const std::size_t base = i * 4;
            for (std::size_t c = 0; c < 3; ++c)
            {
                out[base + c] = toUnorm8(encodeSrgb(glm::unpackHalf1x16(rgba16f[base + c])));
            }
            out[base + 3] = 255;
        }
        return out;
    }

    std::vector<uint8_t> downscaleRgba8(const std::vector<uint8_t>& src, uint32_t width, uint32_t height,
                                        uint32_t maxWidth, uint32_t& outWidth, uint32_t& outHeight)
    {
        if (src.size() < static_cast<std::size_t>(width) * height * 4)
        {
            outWidth = 0;
            outHeight = 0;
            return {};
        }
        if (maxWidth == 0 || width <= maxWidth || height == 0)
        {
            outWidth = width;
            outHeight = height;
            return src;
        }

        outWidth = maxWidth;
        const uint64_t scaledHeight =
            (static_cast<uint64_t>(height) * maxWidth + width / 2) / width;  // rounded
        outHeight = static_cast<uint32_t>(std::max<uint64_t>(1, scaledHeight));

        std::vector<uint8_t> out(static_cast<std::size_t>(outWidth) * outHeight * 4);
        for (uint32_t oy = 0; oy < outHeight; ++oy)
        {
            const uint32_t y0 = static_cast<uint32_t>(static_cast<uint64_t>(oy) * height / outHeight);
            const uint32_t y1 = std::max(y0 + 1,
                static_cast<uint32_t>(static_cast<uint64_t>(oy + 1) * height / outHeight));
            for (uint32_t ox = 0; ox < outWidth; ++ox)
            {
                const uint32_t x0 = static_cast<uint32_t>(static_cast<uint64_t>(ox) * width / outWidth);
                const uint32_t x1 = std::max(x0 + 1,
                    static_cast<uint32_t>(static_cast<uint64_t>(ox + 1) * width / outWidth));

                uint64_t sum[4] = {0, 0, 0, 0};
                for (uint32_t y = y0; y < y1; ++y)
                {
                    const uint8_t* row = src.data() + (static_cast<std::size_t>(y) * width + x0) * 4;
                    for (uint32_t x = x0; x < x1; ++x, row += 4)
                    {
                        sum[0] += row[0];
                        sum[1] += row[1];
                        sum[2] += row[2];
                        sum[3] += row[3];
                    }
                }

                const uint64_t count = static_cast<uint64_t>(x1 - x0) * (y1 - y0);
                uint8_t* dst = out.data() + (static_cast<std::size_t>(oy) * outWidth + ox) * 4;
                for (int c = 0; c < 4; ++c)
                {
                    dst[c] = static_cast<uint8_t>((sum[c] + count / 2) / count);
                }
            }
        }
        return out;
    }

    std::vector<uint8_t> encodePng(const std::vector<uint8_t>& rgba8, uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0 || width > INT_MAX / 4 || height > INT_MAX ||
            rgba8.size() < static_cast<std::size_t>(width) * height * 4)
        {
            return {};
        }

        std::vector<uint8_t> png;
        const int ok = stbi_write_png_to_func(appendToVector, &png, static_cast<int>(width),
                                              static_cast<int>(height), 4, rgba8.data(),
                                              static_cast<int>(width * 4));
        if (ok == 0)
        {
            return {};
        }
        return png;
    }

    std::string base64Encode(const uint8_t* data, std::size_t size)
    {
        static constexpr char alphabet[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        std::string out;
        out.reserve((size + 2) / 3 * 4);

        std::size_t i = 0;
        for (; i + 3 <= size; i += 3)
        {
            const uint32_t triple = (static_cast<uint32_t>(data[i]) << 16) |
                                    (static_cast<uint32_t>(data[i + 1]) << 8) |
                                    static_cast<uint32_t>(data[i + 2]);
            out.push_back(alphabet[(triple >> 18) & 0x3F]);
            out.push_back(alphabet[(triple >> 12) & 0x3F]);
            out.push_back(alphabet[(triple >> 6) & 0x3F]);
            out.push_back(alphabet[triple & 0x3F]);
        }

        const std::size_t remaining = size - i;
        if (remaining == 1)
        {
            const uint32_t triple = static_cast<uint32_t>(data[i]) << 16;
            out.push_back(alphabet[(triple >> 18) & 0x3F]);
            out.push_back(alphabet[(triple >> 12) & 0x3F]);
            out.push_back('=');
            out.push_back('=');
        }
        else if (remaining == 2)
        {
            const uint32_t triple = (static_cast<uint32_t>(data[i]) << 16) |
                                    (static_cast<uint32_t>(data[i + 1]) << 8);
            out.push_back(alphabet[(triple >> 18) & 0x3F]);
            out.push_back(alphabet[(triple >> 12) & 0x3F]);
            out.push_back(alphabet[(triple >> 6) & 0x3F]);
            out.push_back('=');
        }
        return out;
    }
}
