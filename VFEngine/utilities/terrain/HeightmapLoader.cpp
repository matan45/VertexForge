#include "HeightmapLoader.hpp"
#include "../print/Log.hpp"
#include "../resource/EndianUtils.hpp"
#include "../resource/BC7Decoder.hpp"
#include "../../graphics/render/svt/SVTFileFormat.hpp"

#include <fstream>
#include <algorithm>
#include <filesystem>

namespace terrain
{
    float HeightmapData::sample(float u, float v) const
    {
        if (!isValid())
            return 0.0f;

        u = std::clamp(u, 0.0f, 1.0f);
        v = std::clamp(v, 0.0f, 1.0f);

        float maxIndexX = static_cast<float>(width - 1);
        float maxIndexZ = static_cast<float>(height - 1);

        float fx = u * maxIndexX;
        float fz = v * maxIndexZ;

        uint32_t x0 = static_cast<uint32_t>(fx);
        uint32_t z0 = static_cast<uint32_t>(fz);
        uint32_t x1 = std::min(x0 + 1, width - 1);
        uint32_t z1 = std::min(z0 + 1, height - 1);

        float fracX = fx - static_cast<float>(x0);
        float fracZ = fz - static_cast<float>(z0);

        // Bilinear interpolation
        float h00 = heights[z0 * width + x0];
        float h10 = heights[z0 * width + x1];
        float h01 = heights[z1 * width + x0];
        float h11 = heights[z1 * width + x1];

        float h0 = h00 * (1.0f - fracX) + h10 * fracX;
        float h1 = h01 * (1.0f - fracX) + h11 * fracX;

        return h0 * (1.0f - fracZ) + h1 * fracZ;
    }

    std::shared_ptr<HeightmapData> HeightmapLoader::load(const std::string& filePath)
    {
        std::string ext = getExtension(filePath);

        if (ext == ".vfimage")
        {
            return loadVFImage(filePath);
        }
        else if (ext == ".vfsvt")
        {
            return loadVFSVT(filePath);
        }
        else
        {
            vfLogError("HeightmapLoader: Unsupported file format: {}. Use .vfImage or .vfSVT", ext);
            return nullptr;
        }
    }

    std::shared_ptr<HeightmapData> HeightmapLoader::loadVFImage(const std::string& filePath)
    {
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open())
        {
            vfLogError("HeightmapLoader: Failed to open vfImage file: {}", filePath);
            return nullptr;
        }

        uint8_t fileType = resource::endian::readLE<uint8_t>(file);
        uint32_t vMajor = resource::endian::readLE<uint32_t>(file);
        uint32_t vMinor = resource::endian::readLE<uint32_t>(file);
        uint32_t vPatch = resource::endian::readLE<uint32_t>(file);
        uint32_t width = resource::endian::readLE<uint32_t>(file);
        uint32_t height = resource::endian::readLE<uint32_t>(file);
        uint32_t channels = resource::endian::readLE<uint32_t>(file);
        uint32_t mipLevels = resource::endian::readLE<uint32_t>(file);

        (void)fileType;
        (void)vMajor;
        (void)vMinor;
        (void)vPatch;
        (void)channels;
        (void)mipLevels;

        // Read compression format byte (added by VK-914 texture compression)
        uint8_t compressionFormat = resource::endian::readLE<uint8_t>(file);
        (void)compressionFormat;

        uint32_t mipWidth = resource::endian::readLE<uint32_t>(file);
        uint32_t mipHeight = resource::endian::readLE<uint32_t>(file);
        uint32_t dataSize = resource::endian::readLE<uint32_t>(file);
        (void)dataSize;

        if (mipWidth != width || mipHeight != height)
        {
            vfLogWarning("HeightmapLoader: vfImage mip0 dimensions don't match header");
        }

        // Pixel data is BGRA format
        size_t pixelCount = static_cast<size_t>(mipWidth) * mipHeight;
        std::vector<uint8_t> pixelData(pixelCount * 4);
        file.read(reinterpret_cast<char*>(pixelData.data()), static_cast<std::streamsize>(pixelData.size()));
        file.close();

        auto result = std::make_shared<HeightmapData>();
        result->width = mipWidth;
        result->height = mipHeight;
        result->heights.resize(pixelCount);

        // Convert BGRA to grayscale using luminance formula
        for (size_t i = 0; i < pixelCount; ++i)
        {
            size_t idx = i * 4;
            float b = static_cast<float>(pixelData[idx]) / 255.0f;
            float g = static_cast<float>(pixelData[idx + 1]) / 255.0f;
            float r = static_cast<float>(pixelData[idx + 2]) / 255.0f;
            result->heights[i] = 0.299f * r + 0.587f * g + 0.114f * b;
        }

        vfLogInfo("HeightmapLoader: Loaded {}x{} vfImage heightmap from {}", mipWidth, mipHeight, filePath);
        return result;
    }

    std::shared_ptr<HeightmapData> HeightmapLoader::loadVFSVT(const std::string& filePath)
    {
        render::svt::SVTFileReader reader;
        if (!reader.open(filePath))
        {
            vfLogError("HeightmapLoader: Failed to open SVT file: {}", filePath);
            return nullptr;
        }

        const auto& header = reader.getHeader();
        uint32_t tileSize = 1u << header.tileSizeLog2;
        uint32_t border = header.borderSize;
        uint32_t physTileSize = tileSize + 2 * border;
        uint32_t mipLevels = render::svt::computeMipLevelCount(
            header.virtualSizeLog2, header.tileSizeLog2);

        // Use mip 0 for full resolution, or a coarser mip if it's too large
        uint32_t selectedMip = 0;
        uint32_t virtualSize = 1u << header.virtualSizeLog2;
        while (virtualSize > 8192 && selectedMip < mipLevels - 1)
        {
            virtualSize >>= 1;
            ++selectedMip;
        }

        uint32_t tilesPerSide = render::svt::computeTilesPerMipSide(
            selectedMip, header.virtualSizeLog2, header.tileSizeLog2);
        if (tilesPerSide == 0) tilesPerSide = 1;

        uint32_t outputWidth = tilesPerSide * tileSize;
        uint32_t outputHeight = tilesPerSide * tileSize;

        auto result = std::make_shared<HeightmapData>();
        result->width = outputWidth;
        result->height = outputHeight;
        result->heights.resize(static_cast<size_t>(outputWidth) * outputHeight, 0.0f);

        for (uint32_t ty = 0; ty < tilesPerSide; ++ty)
        {
            for (uint32_t tx = 0; tx < tilesPerSide; ++tx)
            {
                std::vector<uint8_t> tileData;
                render::svt::VirtualTileCoord coord{tx, ty, selectedMip};
                if (!reader.readTile(coord, tileData) || tileData.empty())
                    continue;

                // Decompress BC7 tile
                auto decoded = resource::BC7Decoder::decompress(
                    tileData.data(), physTileSize, physTileSize);
                if (decoded.empty()) continue;

                // Copy inner tile region (skip borders), convert to grayscale height
                for (uint32_t py = 0; py < tileSize; ++py)
                {
                    for (uint32_t px = 0; px < tileSize; ++px)
                    {
                        uint32_t imgX = tx * tileSize + px;
                        uint32_t imgY = ty * tileSize + py;
                        if (imgX >= outputWidth || imgY >= outputHeight) continue;

                        uint32_t srcX = px + border;
                        uint32_t srcY = py + border;
                        size_t srcIdx = (static_cast<size_t>(srcY) * physTileSize + srcX) * 4;

                        if (srcIdx + 2 < decoded.size())
                        {
                            float r = decoded[srcIdx + 0] / 255.0f;
                            float g = decoded[srcIdx + 1] / 255.0f;
                            float b = decoded[srcIdx + 2] / 255.0f;
                            float height = 0.299f * r + 0.587f * g + 0.114f * b;
                            result->heights[static_cast<size_t>(imgY) * outputWidth + imgX] = height;
                        }
                    }
                }
            }
        }

        reader.close();
        vfLogInfo("HeightmapLoader: Loaded {}x{} SVT heightmap from {} (mip {})",
                  outputWidth, outputHeight, filePath, selectedMip);
        return result;
    }

    std::string HeightmapLoader::getExtension(const std::string& filePath)
    {
        std::filesystem::path path(filePath);
        std::string ext = path.extension().string();

        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        return ext;
    }

    HeightSampler createHeightSamplerFromMap(
        std::shared_ptr<const HeightmapData> heightmap,
        float terrainMinX,
        float terrainMinZ,
        float terrainWidth,
        float terrainDepth,
        float minHeight,
        float maxHeight)
    {
        return [heightmap, terrainMinX, terrainMinZ, terrainWidth, terrainDepth, minHeight, maxHeight]
        (float worldX, float worldZ) -> float
        {
            float u = (worldX - terrainMinX) / terrainWidth;
            float v = (worldZ - terrainMinZ) / terrainDepth;

            float normalizedHeight = heightmap->sample(u, v);

            return minHeight + normalizedHeight * (maxHeight - minHeight);
        };
    }
}
