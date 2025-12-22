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

    // ============================================================================
    // Helper: Get grayscale value from mip data
    // ============================================================================
    static uint8_t getGrayscaleValue(const resource::MipLevelData& mipData,
                                     uint32_t x, uint32_t y,
                                     uint32_t width, [[maybe_unused]] uint32_t channels)
    {
        // Early return for empty data
        if (mipData.data.empty())
        {
            return 128; // Default mid-gray
        }

        // TGAReader always outputs RGBA (4 channels per pixel)
        uint32_t idx = (y * width + x) * 4;
        if (idx >= mipData.data.size())
        {
            return 128; // Default mid-gray if out of bounds
        }

        // Use red channel (first channel after B<->R swap in TGAReader)
        return mipData.data[idx];
    }

    // ============================================================================
    // Helper: Validate texture dimensions match
    // ============================================================================
    static bool validateTextureDimensions(
        const std::vector<const resource::TextureData*>& textures,
        uint32_t& outWidth,
        uint32_t& outHeight,
        std::string& errorMessage)
    {
        if (textures.empty())
        {
            errorMessage = "At least one texture must be provided to determine output dimensions";
            return false;
        }

        outWidth = textures[0]->width;
        outHeight = textures[0]->height;

        for (size_t i = 1; i < textures.size(); ++i)
        {
            if (textures[i]->width != outWidth || textures[i]->height != outHeight)
            {
                errorMessage = "Texture dimensions do not match. All provided textures must have the same size.";
                return false;
            }
        }

        return true;
    }

    // ============================================================================
    // Helper: Validate mip data exists for provided textures
    // ============================================================================
    static bool validateMipData(
        const resource::TextureData* aoTexture,
        const resource::TextureData* roughnessTexture,
        const resource::TextureData* metallicTexture,
        const resource::TextureData* emissiveTexture,
        const resource::MipLevelData& aoMip,
        const resource::MipLevelData& roughnessMip,
        const resource::MipLevelData& metallicMip,
        const resource::MipLevelData& emissiveMip,
        std::string& errorMessage)
    {
        if (aoTexture && aoMip.data.empty())
        {
            errorMessage = "AO texture has no mip level 0 data";
            return false;
        }
        if (roughnessTexture && roughnessMip.data.empty())
        {
            errorMessage = "Roughness texture has no mip level 0 data";
            return false;
        }
        if (metallicTexture && metallicMip.data.empty())
        {
            errorMessage = "Metallic texture has no mip level 0 data";
            return false;
        }
        if (emissiveTexture && emissiveMip.data.empty())
        {
            errorMessage = "Emissive texture has no mip level 0 data";
            return false;
        }
        return true;
    }

    // ============================================================================
    // Helper: Pack pixels from source textures into ORM format
    // ============================================================================
    static void packPixels(
        resource::MipLevelData& ormMip,
        uint32_t width,
        uint32_t height,
        const resource::TextureData* aoTexture,
        const resource::TextureData* roughnessTexture,
        const resource::TextureData* metallicTexture,
        const resource::TextureData* emissiveTexture,
        const resource::MipLevelData& aoMip,
        const resource::MipLevelData& roughnessMip,
        const resource::MipLevelData& metallicMip,
        const resource::MipLevelData& emissiveMip,
        OrmPackProgressCallback progressCallback)
    {
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
                ormMip.data[outIdx + 0] = metallic;  // B position -> becomes R after swap
                ormMip.data[outIdx + 1] = roughness; // G = Roughness (unchanged)
                ormMip.data[outIdx + 2] = ao;        // R position -> becomes B after swap
                ormMip.data[outIdx + 3] = emissive;  // A = Emissive intensity
            }

            if (progressCallback && (y % (height / 10 + 1) == 0))
            {
                float packProgress = static_cast<float>(y) / static_cast<float>(height);
                progressCallback(0.65f + packProgress * 0.25f);
            }
        }
    }

    // ============================================================================
    // Helper: Generate mipmaps using box filter
    // ============================================================================
    static void generateMipmaps(resource::TextureData& ormTexture)
    {
        uint32_t mipWidth = ormTexture.width;
        uint32_t mipHeight = ormTexture.height;

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

                    // Safety check to prevent division by zero
                    if (samples == 0)
                    {
                        continue;
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
    }

    // ============================================================================
    // Helper: Serialize texture to file
    // ============================================================================
    static bool serializeToFile(
        const resource::TextureData& ormTexture,
        const std::string& outputPath,
        std::string& errorMessage)
    {
        try
        {
            std::filesystem::path outPath(outputPath);
            std::filesystem::create_directories(outPath.parent_path());

            std::ofstream file(outputPath, std::ios::binary);
            if (!file)
            {
                errorMessage = "Failed to create output file: " + outputPath;
                return false;
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

            // Write mip data
            for (const auto& mip : ormTexture.mipData)
            {
                file.write(reinterpret_cast<const char*>(&mip.width), sizeof(mip.width));
                file.write(reinterpret_cast<const char*>(&mip.height), sizeof(mip.height));
                file.write(reinterpret_cast<const char*>(mip.data.data()), mip.data.size());
            }

            file.close();
            return true;
        }
        catch (const std::exception& e)
        {
            errorMessage = std::string("Exception while saving ORM texture: ") + e.what();
            return false;
        }
    }

    // ============================================================================
    // Public: Pack ORM from file paths
    // ============================================================================
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

        // Start all async loads in parallel - maximize I/O parallelism
        std::future<std::shared_ptr<resource::TextureData>> aoFuture;
        std::future<std::shared_ptr<resource::TextureData>> roughnessFuture;
        std::future<std::shared_ptr<resource::TextureData>> metallicFuture;
        std::future<std::shared_ptr<resource::TextureData>> emissiveFuture;

        if (hasAo) aoFuture = resource::ResourceManager::loadTextureAsync(input.aoPath);
        if (hasRoughness) roughnessFuture = resource::ResourceManager::loadTextureAsync(input.roughnessPath);
        if (hasMetallic) metallicFuture = resource::ResourceManager::loadTextureAsync(input.metallicPath);
        if (hasEmissive) emissiveFuture = resource::ResourceManager::loadTextureAsync(input.emissivePath);

        if (progressCallback) progressCallback(0.1f);

        // Now wait for all futures - I/O happens in parallel while we wait
        std::shared_ptr<resource::TextureData> aoData;
        std::shared_ptr<resource::TextureData> roughnessData;
        std::shared_ptr<resource::TextureData> metallicData;
        std::shared_ptr<resource::TextureData> emissiveData;

        // Collect results - by the time we call .get(), most/all loads should be complete
        if (hasAo) aoData = aoFuture.get();
        if (hasRoughness) roughnessData = roughnessFuture.get();
        if (hasMetallic) metallicData = metallicFuture.get();
        if (hasEmissive) emissiveData = emissiveFuture.get();

        if (progressCallback) progressCallback(0.5f);

        // Validate loaded textures
        if (hasAo && (!aoData || aoData->textureData().empty()))
        {
            result.errorMessage = "Failed to load AO texture: " + input.aoPath;
            return result;
        }
        if (hasRoughness && (!roughnessData || roughnessData->textureData().empty()))
        {
            result.errorMessage = "Failed to load roughness texture: " + input.roughnessPath;
            return result;
        }
        if (hasMetallic && (!metallicData || metallicData->textureData().empty()))
        {
            result.errorMessage = "Failed to load metallic texture: " + input.metallicPath;
            return result;
        }
        if (hasEmissive && (!emissiveData || emissiveData->textureData().empty()))
        {
            result.errorMessage = "Failed to load emissive texture: " + input.emissivePath;
            return result;
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

    // ============================================================================
    // Public: Pack ORM from loaded texture data
    // ============================================================================
    OrmPackResult OrmTexturePacker::packORMFromData(
        const resource::TextureData* aoTexture,
        const resource::TextureData* roughnessTexture,
        const resource::TextureData* metallicTexture,
        const resource::TextureData* emissiveTexture,
        const std::string& outputPath,
        OrmPackProgressCallback progressCallback)
    {
        OrmPackResult result;

        // Collect provided textures
        std::vector<const resource::TextureData*> providedTextures;
        if (aoTexture) providedTextures.push_back(aoTexture);
        if (roughnessTexture) providedTextures.push_back(roughnessTexture);
        if (metallicTexture) providedTextures.push_back(metallicTexture);
        if (emissiveTexture) providedTextures.push_back(emissiveTexture);

        // Validate dimensions
        uint32_t width, height;
        if (!validateTextureDimensions(providedTextures, width, height, result.errorMessage))
        {
            return result;
        }

        if (progressCallback) progressCallback(0.65f);

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

        // Validate mip data
        if (!validateMipData(aoTexture, roughnessTexture, metallicTexture, emissiveTexture,
                             aoMip, roughnessMip, metallicMip, emissiveMip, result.errorMessage))
        {
            return result;
        }

        // Create output texture
        resource::TextureData ormTexture;
        ormTexture.width = width;
        ormTexture.height = height;
        ormTexture.numbersOfChannels = 4;
        ormTexture.mipLevels = 1;

        // Pack pixels
        resource::MipLevelData ormMip;
        packPixels(ormMip, width, height,
                   aoTexture, roughnessTexture, metallicTexture, emissiveTexture,
                   aoMip, roughnessMip, metallicMip, emissiveMip,
                   progressCallback);
        ormTexture.mipData.push_back(std::move(ormMip));

        // Generate mipmaps
        generateMipmaps(ormTexture);

        if (progressCallback) progressCallback(0.9f);

        // Serialize to file
        if (!serializeToFile(ormTexture, outputPath, result.errorMessage))
        {
            return result;
        }

        result.success = true;
        result.outputPath = outputPath;

        if (progressCallback) progressCallback(1.0f);

        return result;
    }

}
