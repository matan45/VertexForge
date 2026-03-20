#include "SVTTextureProcessor.hpp"
#include "TextureCompressor.hpp"
#include "../../graphics/render/svt/SVTFileFormat.hpp"
#include "../../graphics/render/svt/SVTTypes.hpp"
#include "config/Config.hpp"
#include <stb_image.h>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace types
{
    bool SVTTextureProcessor::shouldUseSVT(uint32_t width, uint32_t height, uint32_t minSize)
    {
        return width >= minSize || height >= minSize;
    }

    uint32_t SVTTextureProcessor::computeVirtualSizeLog2(uint32_t width, uint32_t height,
                                                           uint32_t tileSizeLog2)
    {
        uint32_t maxDim = std::max(width, height);
        uint32_t tileSize = 1u << tileSizeLog2;

        // Round up to next power of two that covers the image
        uint32_t virtualSize = tileSize;
        uint32_t log2Size = tileSizeLog2;
        while (virtualSize < maxDim)
        {
            virtualSize <<= 1;
            ++log2Size;
        }
        return log2Size;
    }

    bool SVTTextureProcessor::convertToSVT(const std::string& inputPath,
                                            const std::string& outputPath,
                                            const Config& config,
                                            SVTProgressCallback progressCallback)
    {
        if (progressCallback) progressCallback(0.0f);

        // Load source image
        int w, h, channels;
        stbi_set_flip_vertically_on_load(false);
        uint8_t* data = stbi_load(inputPath.c_str(), &w, &h, &channels, 4);
        if (!data)
        {
            return false;
        }

        if (progressCallback) progressCallback(0.1f);

        bool result = convertRGBA8ToSVT(data, static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                                         outputPath, config, progressCallback);

        stbi_image_free(data);
        return result;
    }

    bool SVTTextureProcessor::convertRGBA8ToSVT(const uint8_t* rgbaData,
                                                  uint32_t width, uint32_t height,
                                                  const std::string& outputPath,
                                                  const Config& config,
                                                  SVTProgressCallback progressCallback)
    {
        using namespace render::svt;

        uint32_t tileSize = 1u << config.tileSizeLog2;
        uint32_t border = config.borderSize;
        uint32_t physTileSize = tileSize + 2 * border;

        uint32_t virtualSizeLog2 = computeVirtualSizeLog2(width, height, config.tileSizeLog2);
        uint32_t mipLevels = computeMipLevelCount(virtualSizeLog2, config.tileSizeLog2);

        // Create output file
        SVTFileWriter writer;
        if (!writer.create(outputPath, virtualSizeLog2, config.tileSizeLog2, border))
        {
            return false;
        }

        // Generate mip chain (RGBA8 downsampled images)
        struct MipLevel
        {
            std::vector<uint8_t> data;
            uint32_t width = 0;
            uint32_t height = 0;
        };

        std::vector<MipLevel> mips(mipLevels);

        // Mip 0 = source image (copy)
        mips[0].width = width;
        mips[0].height = height;
        mips[0].data.resize(static_cast<size_t>(width) * height * 4);
        std::memcpy(mips[0].data.data(), rgbaData, mips[0].data.size());

        // Generate downsampled mips
        for (uint32_t m = 1; m < mipLevels; ++m)
        {
            const auto& prev = mips[m - 1];
            auto& cur = mips[m];
            cur.width = std::max(prev.width / 2, 1u);
            cur.height = std::max(prev.height / 2, 1u);
            cur.data.resize(static_cast<size_t>(cur.width) * cur.height * 4);

            for (uint32_t y = 0; y < cur.height; ++y)
            {
                for (uint32_t x = 0; x < cur.width; ++x)
                {
                    uint32_t sx = x * 2;
                    uint32_t sy = y * 2;
                    uint32_t sx1 = std::min(sx + 1, prev.width - 1);
                    uint32_t sy1 = std::min(sy + 1, prev.height - 1);

                    for (int c = 0; c < 4; ++c)
                    {
                        uint32_t sum = prev.data[(sy * prev.width + sx) * 4 + c]
                                     + prev.data[(sy * prev.width + sx1) * 4 + c]
                                     + prev.data[(sy1 * prev.width + sx) * 4 + c]
                                     + prev.data[(sy1 * prev.width + sx1) * 4 + c];
                        cur.data[(y * cur.width + x) * 4 + c] = static_cast<uint8_t>(sum / 4);
                    }
                }
            }
        }

        if (progressCallback) progressCallback(0.3f);

        // Tile and compress each mip level
        uint32_t totalTiles = computeTotalPageTableEntries(virtualSizeLog2, config.tileSizeLog2);
        uint32_t tilesProcessed = 0;

        std::vector<uint8_t> tileRGBA(static_cast<size_t>(physTileSize) * physTileSize * 4);

        for (uint32_t m = 0; m < mipLevels; ++m)
        {
            uint32_t tilesPerSide = computeTilesPerMipSide(m, virtualSizeLog2, config.tileSizeLog2);
            if (tilesPerSide == 0) tilesPerSide = 1;

            const auto& mip = mips[m];
            uint32_t virtualSize = 1u << (virtualSizeLog2 - m);

            for (uint32_t ty = 0; ty < tilesPerSide; ++ty)
            {
                for (uint32_t tx = 0; tx < tilesPerSide; ++tx)
                {
                    // Extract tile with border from mip image
                    int32_t tileStartX = static_cast<int32_t>(tx * tileSize) - static_cast<int32_t>(border);
                    int32_t tileStartY = static_cast<int32_t>(ty * tileSize) - static_cast<int32_t>(border);

                    for (uint32_t py = 0; py < physTileSize; ++py)
                    {
                        for (uint32_t px = 0; px < physTileSize; ++px)
                        {
                            int32_t srcX = std::clamp(tileStartX + static_cast<int32_t>(px),
                                                       0, static_cast<int32_t>(mip.width) - 1);
                            int32_t srcY = std::clamp(tileStartY + static_cast<int32_t>(py),
                                                       0, static_cast<int32_t>(mip.height) - 1);

                            size_t dstIdx = (static_cast<size_t>(py) * physTileSize + px) * 4;
                            size_t srcIdx = (static_cast<size_t>(srcY) * mip.width + srcX) * 4;

                            tileRGBA[dstIdx + 0] = mip.data[srcIdx + 0];
                            tileRGBA[dstIdx + 1] = mip.data[srcIdx + 1];
                            tileRGBA[dstIdx + 2] = mip.data[srcIdx + 2];
                            tileRGBA[dstIdx + 3] = mip.data[srcIdx + 3];
                        }
                    }

                    // BC7 compress
                    auto compressed = TextureCompressor::compressBC7(
                        tileRGBA.data(), physTileSize, physTileSize,
                        importConfig::TextureCompressionQuality::Fast);

                    if (!compressed.empty())
                    {
                        VirtualTileCoord coord{tx, ty, m};
                        writer.writeTile(coord, compressed.data(),
                                         static_cast<uint32_t>(compressed.size()));
                    }

                    ++tilesProcessed;
                    if (progressCallback)
                    {
                        progressCallback(0.3f + 0.7f * static_cast<float>(tilesProcessed) / totalTiles);
                    }
                }
            }
        }

        return writer.finalize();
    }
}
