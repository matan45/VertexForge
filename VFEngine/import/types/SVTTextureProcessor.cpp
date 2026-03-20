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
    namespace
    {
        struct MipLevel
        {
            std::vector<uint8_t> data;
            uint32_t width = 0;
            uint32_t height = 0;
        };

        std::vector<MipLevel> generateMipChain(const ImageData& image, uint32_t mipLevels)
        {
            std::vector<MipLevel> mips(mipLevels);

            mips[0].width = image.width;
            mips[0].height = image.height;
            mips[0].data.resize(static_cast<size_t>(image.width) * image.height * 4);
            std::memcpy(mips[0].data.data(), image.data, mips[0].data.size());

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

            return mips;
        }

        void extractTileWithBorder(const MipLevel& mip, uint32_t tx, uint32_t ty,
                                    uint32_t tileSize, uint32_t border, uint32_t physTileSize,
                                    std::vector<uint8_t>& tileRGBA)
        {
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
        }
    }

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

        ImageData image{data, static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
        bool result = convertRGBA8ToSVT(image, outputPath, config, progressCallback);

        stbi_image_free(data);
        return result;
    }

    bool SVTTextureProcessor::convertRGBA8ToSVT(const ImageData& image,
                                                  const std::string& outputPath,
                                                  const Config& config,
                                                  SVTProgressCallback progressCallback)
    {
        using namespace render::svt;

        uint32_t tileSize = 1u << config.tileSizeLog2;
        uint32_t border = config.borderSize;
        uint32_t physTileSize = tileSize + 2 * border;

        uint32_t virtualSizeLog2 = computeVirtualSizeLog2(image.width, image.height, config.tileSizeLog2);
        uint32_t mipLevels = computeMipLevelCount(virtualSizeLog2, config.tileSizeLog2);

        SVTFileWriter writer;
        if (!writer.create(outputPath, virtualSizeLog2, config.tileSizeLog2, border))
            return false;

        // Generate mip chain
        auto mips = generateMipChain(image, mipLevels);

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

            for (uint32_t ty = 0; ty < tilesPerSide; ++ty)
            {
                for (uint32_t tx = 0; tx < tilesPerSide; ++tx)
                {
                    extractTileWithBorder(mip, tx, ty, tileSize, border, physTileSize, tileRGBA);

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
                        progressCallback(0.3f + 0.7f * static_cast<float>(tilesProcessed) / totalTiles);
                }
            }
        }

        return writer.finalize();
    }

    bool SVTTextureProcessor::packORMToSVT(const ORMPackInput& input,
                                            const std::string& outputPath,
                                            const Config& config,
                                            SVTProgressCallback progressCallback)
    {
        if (progressCallback) progressCallback(0.0f);

        uint32_t width = 0, height = 0;
        auto loadORMChannels = [&]() -> std::vector<uint8_t>
        {
            int aoW = 0, aoH = 0, aoC = 0;
            int roughW = 0, roughH = 0, roughC = 0;
            int metalW = 0, metalH = 0, metalC = 0;

            stbi_set_flip_vertically_on_load(false);
            uint8_t* aoData = !input.aoPath.empty()
                ? stbi_load(input.aoPath.c_str(), &aoW, &aoH, &aoC, 1) : nullptr;
            uint8_t* roughData = !input.roughnessPath.empty()
                ? stbi_load(input.roughnessPath.c_str(), &roughW, &roughH, &roughC, 1) : nullptr;
            uint8_t* metalData = !input.metallicPath.empty()
                ? stbi_load(input.metallicPath.c_str(), &metalW, &metalH, &metalC, 1) : nullptr;

            if (aoData) { width = static_cast<uint32_t>(aoW); height = static_cast<uint32_t>(aoH); }
            else if (roughData) { width = static_cast<uint32_t>(roughW); height = static_cast<uint32_t>(roughH); }
            else if (metalData) { width = static_cast<uint32_t>(metalW); height = static_cast<uint32_t>(metalH); }

            if (width == 0 || height == 0)
            {
                if (aoData) stbi_image_free(aoData);
                if (roughData) stbi_image_free(roughData);
                if (metalData) stbi_image_free(metalData);
                return {};
            }

            uint8_t defAO = static_cast<uint8_t>(std::clamp(input.defaultAO * 255.0f, 0.0f, 255.0f));
            uint8_t defRough = static_cast<uint8_t>(std::clamp(input.defaultRoughness * 255.0f, 0.0f, 255.0f));
            uint8_t defMetal = static_cast<uint8_t>(std::clamp(input.defaultMetallic * 255.0f, 0.0f, 255.0f));

            std::vector<uint8_t> ormRGBA(static_cast<size_t>(width) * height * 4);
            for (uint32_t y = 0; y < height; ++y)
            {
                for (uint32_t x = 0; x < width; ++x)
                {
                    size_t srcIdx = static_cast<size_t>(y) * width + x;
                    size_t dstIdx = srcIdx * 4;
                    ormRGBA[dstIdx + 0] = (aoData && srcIdx < static_cast<size_t>(aoW) * aoH)
                                           ? aoData[srcIdx] : defAO;
                    ormRGBA[dstIdx + 1] = (roughData && srcIdx < static_cast<size_t>(roughW) * roughH)
                                           ? roughData[srcIdx] : defRough;
                    ormRGBA[dstIdx + 2] = (metalData && srcIdx < static_cast<size_t>(metalW) * metalH)
                                           ? metalData[srcIdx] : defMetal;
                    ormRGBA[dstIdx + 3] = 255;
                }
            }

            if (aoData) stbi_image_free(aoData);
            if (roughData) stbi_image_free(roughData);
            if (metalData) stbi_image_free(metalData);

            return ormRGBA;
        };

        auto ormRGBA = loadORMChannels();
        if (ormRGBA.empty()) return false;

        if (progressCallback) progressCallback(0.2f);

        Config ormConfig = config;
        ormConfig.srgb = false;
        ImageData image{ormRGBA.data(), width, height};
        return convertRGBA8ToSVT(image, outputPath, ormConfig,
            [&](float p) { if (progressCallback) progressCallback(0.2f + p * 0.8f); });
    }
}
