#include "OrmTexturePacker.hpp"
#include "../resource/ResourceManager.hpp"
#include "../config/Config.hpp"
#include <filesystem>
#include <fstream>

namespace texture
{
    // Default values for missing textures
    constexpr uint8_t DEFAULT_AO = 255;        // No occlusion (fully lit)
    constexpr uint8_t DEFAULT_ROUGHNESS = 128; // Mid roughness (~0.5)
    constexpr uint8_t DEFAULT_METALLIC = 0;    // Non-metallic
    constexpr uint8_t DEFAULT_EMISSIVE = 255;  // Full alpha (visible in previews), shader treats as no emission

    OrmPackResult OrmTexturePacker::packORM(
        const OrmPackInput& input,
        OrmPackProgressCallback progressCallback)
    {
        OrmPackResult result;

        if (input.outputPath.empty())
        {
            result.errorMessage = "Output path is required";
            return result;
        }

        // Check if at least one texture is provided
        bool hasAo = !input.aoPath.empty();
        bool hasRoughness = !input.roughnessPath.empty();
        bool hasMetallic = !input.metallicPath.empty();
        bool hasEmissive = !input.emissivePath.empty();

        if (!hasAo && !hasRoughness && !hasMetallic && !hasEmissive)
        {
            result.errorMessage = "At least one texture must be provided to determine output dimensions";
            return result;
        }

        if (progressCallback) progressCallback(0.0f);

        // Load provided textures asynchronously
        std::future<std::shared_ptr<resource::TextureData>> aoFuture;
        std::future<std::shared_ptr<resource::TextureData>> roughnessFuture;
        std::future<std::shared_ptr<resource::TextureData>> metallicFuture;
        std::future<std::shared_ptr<resource::TextureData>> emissiveFuture;

        if (hasAo) aoFuture = resource::ResourceManager::loadTextureAsync(input.aoPath);
        if (hasRoughness) roughnessFuture = resource::ResourceManager::loadTextureAsync(input.roughnessPath);
        if (hasMetallic) metallicFuture = resource::ResourceManager::loadTextureAsync(input.metallicPath);
        if (hasEmissive) emissiveFuture = resource::ResourceManager::loadTextureAsync(input.emissivePath);

        if (progressCallback) progressCallback(0.1f);

        // Get loaded textures
        std::shared_ptr<resource::TextureData> aoData;
        std::shared_ptr<resource::TextureData> roughnessData;
        std::shared_ptr<resource::TextureData> metallicData;
        std::shared_ptr<resource::TextureData> emissiveData;

        if (hasAo)
        {
            aoData = aoFuture.get();
            if (!aoData || aoData->textureData().empty())
            {
                result.errorMessage = "Failed to load AO texture: " + input.aoPath;
                return result;
            }
        }

        if (progressCallback) progressCallback(0.25f);

        if (hasRoughness)
        {
            roughnessData = roughnessFuture.get();
            if (!roughnessData || roughnessData->textureData().empty())
            {
                result.errorMessage = "Failed to load roughness texture: " + input.roughnessPath;
                return result;
            }
        }

        if (progressCallback) progressCallback(0.4f);

        if (hasMetallic)
        {
            metallicData = metallicFuture.get();
            if (!metallicData || metallicData->textureData().empty())
            {
                result.errorMessage = "Failed to load metallic texture: " + input.metallicPath;
                return result;
            }
        }

        if (progressCallback) progressCallback(0.5f);

        if (hasEmissive)
        {
            emissiveData = emissiveFuture.get();
            if (!emissiveData || emissiveData->textureData().empty())
            {
                result.errorMessage = "Failed to load emissive texture: " + input.emissivePath;
                return result;
            }
        }

        if (progressCallback) progressCallback(0.6f);

        return packORMFromData(
            aoData.get(),
            roughnessData.get(),
            metallicData.get(),
            emissiveData.get(),
            input.outputPath,
            progressCallback);
    }

    OrmPackResult OrmTexturePacker::packORMFromData(
        const resource::TextureData* aoTexture,
        const resource::TextureData* roughnessTexture,
        const resource::TextureData* metallicTexture,
        const resource::TextureData* emissiveTexture,
        const std::string& outputPath,
        OrmPackProgressCallback progressCallback)
    {
        OrmPackResult result;

        // Collect all provided textures to determine dimensions
        std::vector<const resource::TextureData*> providedTextures;
        if (aoTexture) providedTextures.push_back(aoTexture);
        if (roughnessTexture) providedTextures.push_back(roughnessTexture);
        if (metallicTexture) providedTextures.push_back(metallicTexture);
        if (emissiveTexture) providedTextures.push_back(emissiveTexture);

        if (providedTextures.empty())
        {
            result.errorMessage = "At least one texture must be provided to determine output dimensions";
            return result;
        }

        // Get dimensions from first provided texture
        uint32_t width = providedTextures[0]->width;
        uint32_t height = providedTextures[0]->height;

        // Validate all provided textures have matching dimensions
        for (size_t i = 1; i < providedTextures.size(); ++i)
        {
            if (providedTextures[i]->width != width || providedTextures[i]->height != height)
            {
                result.errorMessage = "Texture dimensions do not match. All provided textures must have the same size.";
                return result;
            }
        }

        if (progressCallback) progressCallback(0.65f);

        // Create output texture data
        resource::TextureData ormTexture;
        ormTexture.width = width;
        ormTexture.height = height;
        ormTexture.numbersOfChannels = 4; // RGBA output
        ormTexture.mipLevels = 1;

        // Get mip level 0 data from each provided texture
        resource::MipLevelData emptyMip;
        const auto& aoMip = (aoTexture && !aoTexture->mipData.empty())
            ? aoTexture->mipData[0] : emptyMip;
        const auto& roughnessMip = (roughnessTexture && !roughnessTexture->mipData.empty())
            ? roughnessTexture->mipData[0] : emptyMip;
        const auto& metallicMip = (metallicTexture && !metallicTexture->mipData.empty())
            ? metallicTexture->mipData[0] : emptyMip;
        const auto& emissiveMip = (emissiveTexture && !emissiveTexture->mipData.empty())
            ? emissiveTexture->mipData[0] : emptyMip;

        // Validate provided textures have mip data
        if (aoTexture && aoMip.data.empty())
        {
            result.errorMessage = "AO texture has no mip level 0 data";
            return result;
        }
        if (roughnessTexture && roughnessMip.data.empty())
        {
            result.errorMessage = "Roughness texture has no mip level 0 data";
            return result;
        }
        if (metallicTexture && metallicMip.data.empty())
        {
            result.errorMessage = "Metallic texture has no mip level 0 data";
            return result;
        }
        if (emissiveTexture && emissiveMip.data.empty())
        {
            result.errorMessage = "Emissive texture has no mip level 0 data";
            return result;
        }

        // Pack textures in BGR format (TGAReader swaps B<->R on load)
        // After load: R=AO, G=Roughness, B=Metallic, A=Emissive
        resource::MipLevelData ormMip;
        ormMip.width = width;
        ormMip.height = height;
        ormMip.data.resize(width * height * 4);

        for (uint32_t y = 0; y < height; ++y)
        {
            for (uint32_t x = 0; x < width; ++x)
            {
                uint32_t outIdx = (y * width + x) * 4;

                // Get values from textures or use defaults
                uint8_t ao = aoTexture
                    ? getGrayscaleValue(aoMip, x, y, width, aoTexture->numbersOfChannels)
                    : DEFAULT_AO;
                uint8_t roughness = roughnessTexture
                    ? getGrayscaleValue(roughnessMip, x, y, width, roughnessTexture->numbersOfChannels)
                    : DEFAULT_ROUGHNESS;
                uint8_t metallic = metallicTexture
                    ? getGrayscaleValue(metallicMip, x, y, width, metallicTexture->numbersOfChannels)
                    : DEFAULT_METALLIC;
                uint8_t emissive = emissiveTexture
                    ? getGrayscaleValue(emissiveMip, x, y, width, emissiveTexture->numbersOfChannels)
                    : DEFAULT_EMISSIVE;

                // Write in BGR order - TGAReader will swap [0] and [2] to get RGB
                ormMip.data[outIdx + 0] = metallic; // B position -> becomes R after swap
                ormMip.data[outIdx + 1] = roughness; // G = Roughness (unchanged)
                ormMip.data[outIdx + 2] = ao; // R position -> becomes B after swap
                ormMip.data[outIdx + 3] = emissive; // A = Emissive intensity
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
                                                uint32_t width, [[maybe_unused]] uint32_t channels)
    {
        // TGAReader always outputs RGBA (4 channels per pixel), regardless of original channel count
        // So we always use stride of 4 here
        uint32_t idx = (y * width + x) * 4;
        if (idx >= mipData.data.size())
        {
            return 128; // Default mid-gray if out of bounds
        }

        // Use red channel (first channel after B<->R swap in TGAReader)
        return mipData.data[idx];
    }
}
