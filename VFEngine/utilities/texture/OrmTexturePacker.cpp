#include "OrmTexturePacker.hpp"
#include "../resource/ResourceManager.hpp"
#include "../config/Config.hpp"
#include <filesystem>
#include <fstream>

namespace texture
{
    OrmPackResult OrmTexturePacker::packORM(
        const std::string& aoPath,
        const std::string& roughnessPath,
        const std::string& metallicPath,
        const std::string& outputPath,
        OrmPackProgressCallback progressCallback)
    {
        OrmPackResult result;

        if (progressCallback) progressCallback(0.0f);

        // Load all three textures
        auto aoFuture = resource::ResourceManager::loadTextureAsync(aoPath);
        auto roughnessFuture = resource::ResourceManager::loadTextureAsync(roughnessPath);
        auto metallicFuture = resource::ResourceManager::loadTextureAsync(metallicPath);

        if (progressCallback) progressCallback(0.1f);

        auto aoData = aoFuture.get();
        if (!aoData || aoData->textureData().empty())
        {
            result.errorMessage = "Failed to load AO texture: " + aoPath;
            return result;
        }

        if (progressCallback) progressCallback(0.3f);

        auto roughnessData = roughnessFuture.get();
        if (!roughnessData || roughnessData->textureData().empty())
        {
            result.errorMessage = "Failed to load roughness texture: " + roughnessPath;
            return result;
        }

        if (progressCallback) progressCallback(0.5f);

        auto metallicData = metallicFuture.get();
        if (!metallicData || metallicData->textureData().empty())
        {
            result.errorMessage = "Failed to load metallic texture: " + metallicPath;
            return result;
        }

        if (progressCallback) progressCallback(0.6f);

        return packORMFromData(*aoData, *roughnessData, *metallicData, outputPath, progressCallback);
    }

    OrmPackResult OrmTexturePacker::packORMFromData(
        const resource::TextureData& aoTexture,
        const resource::TextureData& roughnessTexture,
        const resource::TextureData& metallicTexture,
        const std::string& outputPath,
        OrmPackProgressCallback progressCallback)
    {
        OrmPackResult result;

        // Validate dimensions match
        if (aoTexture.width != roughnessTexture.width ||
            aoTexture.width != metallicTexture.width ||
            aoTexture.height != roughnessTexture.height ||
            aoTexture.height != metallicTexture.height)
        {
            result.errorMessage = "Texture dimensions do not match. All textures must have the same size.";
            return result;
        }

        uint32_t width = aoTexture.width;
        uint32_t height = aoTexture.height;
        uint32_t aoChannels = aoTexture.numbersOfChannels;
        uint32_t roughnessChannels = roughnessTexture.numbersOfChannels;
        uint32_t metallicChannels = metallicTexture.numbersOfChannels;

        if (progressCallback) progressCallback(0.65f);

        // Create output texture data
        resource::TextureData ormTexture;
        ormTexture.width = width;
        ormTexture.height = height;
        ormTexture.numbersOfChannels = 4; // RGBA output
        ormTexture.mipLevels = 1;

        // Get mip level 0 data from each texture
        const auto& aoMip = aoTexture.mipData.empty() ? resource::MipLevelData{} : aoTexture.mipData[0];
        const auto& roughnessMip = roughnessTexture.mipData.empty()
                                       ? resource::MipLevelData{}
                                       : roughnessTexture.mipData[0];
        const auto& metallicMip = metallicTexture.mipData.empty()
                                      ? resource::MipLevelData{}
                                      : metallicTexture.mipData[0];

        if (aoMip.data.empty() || roughnessMip.data.empty() || metallicMip.data.empty())
        {
            result.errorMessage = "One or more textures have no mip level 0 data";
            return result;
        }

        // Pack textures in BGR format (TGAReader swaps B<->R on load)
        // After load: R=AO, G=Roughness, B=Metallic
        resource::MipLevelData ormMip;
        ormMip.width = width;
        ormMip.height = height;
        ormMip.data.resize(width * height * 4);

        for (uint32_t y = 0; y < height; ++y)
        {
            for (uint32_t x = 0; x < width; ++x)
            {
                uint32_t outIdx = (y * width + x) * 4;

                uint8_t ao = getGrayscaleValue(aoMip, x, y, width, aoChannels);
                uint8_t roughness = getGrayscaleValue(roughnessMip, x, y, width, roughnessChannels);
                uint8_t metallic = getGrayscaleValue(metallicMip, x, y, width, metallicChannels);

                // Write in BGR order - TGAReader will swap [0] and [2] to get RGB
                ormMip.data[outIdx + 0] = metallic; // B position -> becomes R after swap
                ormMip.data[outIdx + 1] = roughness; // G = Roughness (unchanged)
                ormMip.data[outIdx + 2] = ao; // R position -> becomes B after swap
                ormMip.data[outIdx + 3] = 255; // A = 1.0
            }

            if (progressCallback && (y % (height / 10 + 1) == 0))
            {
                float packProgress = static_cast<float>(y) / static_cast<float>(height);
                progressCallback(0.65f + packProgress * 0.25f);
            }
        }

        ormTexture.mipData.push_back(std::move(ormMip));

        // Generate mipmaps using box filter
        uint32_t mipWidth = width;
        uint32_t mipHeight = height;
        while (mipWidth > 1 || mipHeight > 1)
        {
            const auto& srcMip = ormTexture.mipData.back();

            uint32_t newWidth = std::max(1u, mipWidth / 2);
            uint32_t newHeight = std::max(1u, mipHeight / 2);

            resource::MipLevelData newMip;
            newMip.width = newWidth;
            newMip.height = newHeight;
            newMip.data.resize(newWidth * newHeight * 4);

            // Box filter: average 2x2 pixels from source
            for (uint32_t y = 0; y < newHeight; ++y)
            {
                for (uint32_t x = 0; x < newWidth; ++x)
                {
                    uint32_t srcX = x * 2;
                    uint32_t srcY = y * 2;

                    // Sample up to 4 pixels (handle edge cases)
                    uint32_t samples = 0;
                    uint32_t sumR = 0, sumG = 0, sumB = 0, sumA = 0;

                    for (uint32_t dy = 0; dy < 2 && (srcY + dy) < mipHeight; ++dy)
                    {
                        for (uint32_t dx = 0; dx < 2 && (srcX + dx) < mipWidth; ++dx)
                        {
                            uint32_t srcIdx = ((srcY + dy) * mipWidth + (srcX + dx)) * 4;
                            sumR += srcMip.data[srcIdx + 0];
                            sumG += srcMip.data[srcIdx + 1];
                            sumB += srcMip.data[srcIdx + 2];
                            sumA += srcMip.data[srcIdx + 3];
                            ++samples;
                        }
                    }

                    uint32_t dstIdx = (y * newWidth + x) * 4;
                    newMip.data[dstIdx + 0] = static_cast<uint8_t>(sumR / samples);
                    newMip.data[dstIdx + 1] = static_cast<uint8_t>(sumG / samples);
                    newMip.data[dstIdx + 2] = static_cast<uint8_t>(sumB / samples);
                    newMip.data[dstIdx + 3] = static_cast<uint8_t>(sumA / samples);
                }
            }

            mipWidth = newWidth;
            mipHeight = newHeight;
            ormTexture.mipData.push_back(std::move(newMip));
        }

        ormTexture.mipLevels = static_cast<uint32_t>(ormTexture.mipData.size());

        if (progressCallback) progressCallback(0.9f);

        // Save to file
        try
        {
            std::filesystem::path outPath(outputPath);
            std::filesystem::create_directories(outPath.parent_path());

            std::ofstream file(outputPath, std::ios::binary);
            if (!file)
            {
                result.errorMessage = "Failed to create output file: " + outputPath;
                return result;
            }

            // Write header (version 0.0.3 = mip format expected by TextureResource loader)
            resource::FileType fileType = resource::FileType::TEXTURE;
            FileVersion version{0, 0, 3};

            file.write(reinterpret_cast<const char*>(&fileType), sizeof(fileType));
            file.write(reinterpret_cast<const char*>(&version), sizeof(version));
            file.write(reinterpret_cast<const char*>(&ormTexture.width), sizeof(ormTexture.width));
            file.write(reinterpret_cast<const char*>(&ormTexture.height), sizeof(ormTexture.height));
            file.write(reinterpret_cast<const char*>(&ormTexture.numbersOfChannels),
                       sizeof(ormTexture.numbersOfChannels));

            uint32_t mipLevels = static_cast<uint32_t>(ormTexture.mipData.size());
            file.write(reinterpret_cast<const char*>(&mipLevels), sizeof(mipLevels));

            // Write mip data (TGAReader expects just width, height, then raw pixel data)
            for (const auto& mip : ormTexture.mipData)
            {
                file.write(reinterpret_cast<const char*>(&mip.width), sizeof(mip.width));
                file.write(reinterpret_cast<const char*>(&mip.height), sizeof(mip.height));
                // No dataSize prefix - TGAReader calculates size as width*height*4
                file.write(reinterpret_cast<const char*>(mip.data.data()), mip.data.size());
            }

            file.close();

            result.success = true;
            result.outputPath = outputPath;
        }
        catch (const std::exception& e)
        {
            result.errorMessage = std::string("Exception while saving ORM texture: ") + e.what();
            return result;
        }

        if (progressCallback) progressCallback(1.0f);

        return result;
    }

    uint8_t OrmTexturePacker::getGrayscaleValue(const resource::MipLevelData& mipData,
                                                uint32_t x, uint32_t y,
                                                uint32_t width, uint32_t channels)
    {
        uint32_t idx = (y * width + x) * channels;
        if (idx >= mipData.data.size())
        {
            return 128; // Default mid-gray if out of bounds
        }

        if (channels == 1)
        {
            return mipData.data[idx];
        }

        // For multi-channel textures, use red channel
        return mipData.data[idx];
    }
}
