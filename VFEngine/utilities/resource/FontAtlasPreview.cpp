#include "FontAtlasPreview.hpp"

#include "math/MathHelper.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>

namespace resource
{
    TextureData fontAtlasToPreviewRGBA(const FontData& fontData)
    {
        const FontAtlasData& atlas = fontData.atlas;

        TextureData textureData;
        textureData.width = atlas.width;
        textureData.height = atlas.height;
        textureData.numbersOfChannels = 4;
        textureData.mipLevels = 1;

        const size_t bytesPerPixel = fontAtlasBytesPerPixel(atlas.format);
        const size_t width = static_cast<size_t>(atlas.width);
        const size_t height = static_cast<size_t>(atlas.height);
        if (bytesPerPixel == 0 || width == 0 || height == 0 ||
            width > std::numeric_limits<size_t>::max() / height)
        {
            throw std::runtime_error("Invalid font atlas dimensions or format");
        }

        const size_t pixelCount = width * height;
        if (pixelCount > std::numeric_limits<size_t>::max() / bytesPerPixel ||
            atlas.pixels.size() != pixelCount * bytesPerPixel ||
            pixelCount > std::numeric_limits<size_t>::max() / 4)
        {
            throw std::runtime_error("Invalid font atlas pixel payload");
        }

        std::vector<unsigned char> rgbaData(pixelCount * 4);

        if (atlas.format == FontAtlasFormat::SDF_8)
        {
            // VK-1631: derive the bake from the font's own SDF parameters instead of the
            // sdf:: defaults, which silently assumed spread 4 / onEdge 128. The 0.5f mirrors
            // the shader's `w = 0.5 * fwidth(sdf)`, so at the atlas's native 1:1 scale this
            // preview matches what the text pipelines render (and, at the default spread,
            // reproduces the previous 128 / 16 exactly).
            const float edgeByte = fontData.sdfParams.edgeValue * 255.0f;
            const float smoothByte = 0.5f * sdfSmoothWidth(fontData) * 255.0f;

            for (size_t i = 0; i < pixelCount; ++i)
            {
                rgbaData[i * 4 + 0] = 255;
                rgbaData[i * 4 + 1] = 255;
                rgbaData[i * 4 + 2] = 255;
                rgbaData[i * 4 + 3] = sdf::sdfToAlphaByte(atlas.pixels[i], edgeByte, smoothByte);
            }
        }
        else if (atlas.format == FontAtlasFormat::GRAYSCALE_8)
        {
            for (size_t i = 0; i < pixelCount; ++i)
            {
                unsigned char value = atlas.pixels[i];
                rgbaData[i * 4 + 0] = 255;
                rgbaData[i * 4 + 1] = 255;
                rgbaData[i * 4 + 2] = 255;
                rgbaData[i * 4 + 3] = value;
            }
        }
        else if (atlas.format == FontAtlasFormat::RGBA_32)
        {
            rgbaData.assign(atlas.pixels.begin(), atlas.pixels.end());
        }
        else if (atlas.format == FontAtlasFormat::MTSDF_RGBA_32)
        {
            // Preview the same reconstructed field used by the runtime shaders.
            // MTSDF's alpha channel remains persisted in the atlas for future
            // outline/shadow/glow effects; the base glyph uses median RGB.
            //
            // VK-1634: the band comes from the shared helpers the fragment shader mirrors.
            // One atlas texel is one preview pixel here, so the magnification is 1 and
            // screenPxRange collapses to pxRange - which for any font that passes the
            // .vfFont validators (pxRange in [1, 16]) is numerically what this branch
            // computed before.
            const float edgeByte = fontData.sdfParams.edgeValue * 255.0f;
            const float screenRange = sdf::mtsdfScreenPxRange(fontData.sdfParams.pxRange, 1.0f);
            const float smoothByte = sdf::mtsdfHalfBand(screenRange) * 255.0f;

            for (size_t i = 0; i < pixelCount; ++i)
            {
                const size_t source = i * 4;
                const unsigned char r = atlas.pixels[source + 0];
                const unsigned char g = atlas.pixels[source + 1];
                const unsigned char b = atlas.pixels[source + 2];
                const unsigned char median =
                    (std::max)((std::min)(r, g), (std::min)((std::max)(r, g), b));

                rgbaData[source + 0] = 255;
                rgbaData[source + 1] = 255;
                rgbaData[source + 2] = 255;
                rgbaData[source + 3] =
                    sdf::sdfToAlphaByte(median, edgeByte, smoothByte);
            }
        }

        MipLevelData mip;
        mip.width = atlas.width;
        mip.height = atlas.height;
        mip.data = std::move(rgbaData);
        textureData.mipData.push_back(std::move(mip));

        return textureData;
    }
}
