#include "OrmTexturePacker.hpp"
#include "../resource/ResourceManager.hpp"
#include "../resource/EndianUtils.hpp"
#include "../config/Config.hpp"
#include <filesystem>
#include <fstream>

namespace texture
{
    // ============================================================================
    // Helper: Get grayscale value from mip data
    // ============================================================================
    static uint8_t getGrayscaleValue(const resource::MipLevelData& mipData,
                                     uint32_t x, uint32_t y,
                                     uint32_t width, [[maybe_unused]] uint32_t channels)
    {
        if (mipData.data.empty())
        {
            return 128;
        }

        uint32_t idx = (y * width + x) * 4;
        if (idx >= mipData.data.size())
        {
            return 128;
        }

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
        const resource::MipLevelData& aoMip,
        const resource::MipLevelData& roughnessMip,
        const resource::MipLevelData& metallicMip,
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
        return true;
    }

    // ============================================================================
    // Helper: Pack pixels from source textures into ORM format (RGBA)
    // ============================================================================
    static void packPixels(
        resource::MipLevelData& ormMip,
        uint32_t width,
        uint32_t height,
        const resource::TextureData* aoTexture,
        const resource::TextureData* roughnessTexture,
        const resource::TextureData* metallicTexture,
        const resource::MipLevelData& aoMip,
        const resource::MipLevelData& roughnessMip,
        const resource::MipLevelData& metallicMip,
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

                uint8_t ao = aoTexture
                                 ? getGrayscaleValue(aoMip, x, y, width, aoTexture->numbersOfChannels)
                                 : OrmTexturePacker::DEFAULT_AO;
                uint8_t roughness = roughnessTexture
                                        ? getGrayscaleValue(roughnessMip, x, y, width,
                                                            roughnessTexture->numbersOfChannels)
                                        : OrmTexturePacker::DEFAULT_ROUGHNESS;
                uint8_t metallic = metallicTexture
                                       ? getGrayscaleValue(metallicMip, x, y, width, metallicTexture->numbersOfChannels)
                                       : OrmTexturePacker::DEFAULT_METALLIC;

                // ORM: R=AO, G=Roughness, B=Metallic, A=255
                // BGRA order for .vfImage format (TGAReader swaps B<->R when loading)
                ormMip.data[outIdx + 0] = metallic;
                ormMip.data[outIdx + 1] = roughness;
                ormMip.data[outIdx + 2] = ao;
                ormMip.data[outIdx + 3] = 255;
            }

            if (progressCallback && (y % (height / 10 + 1) == 0))
            {
                float packProgress = static_cast<float>(y) / static_cast<float>(height);
                progressCallback(0.65f + packProgress * 0.25f);
            }
        }
    }

    // ============================================================================
    // Helper: Sample a 2x2 block from source and write averaged pixel to dest
    // ============================================================================
    static void sampleBoxFilter(
        const resource::MipLevelData& srcMip,
        uint32_t srcWidth, uint32_t srcHeight,
        uint32_t srcX, uint32_t srcY,
        std::vector<uint8_t>& dstData,
        uint32_t dstIdx)
    {
        uint32_t samples = 0;
        uint32_t sumR = 0, sumG = 0, sumB = 0;

        for (uint32_t dy = 0; dy < 2 && (srcY + dy) < srcHeight; ++dy)
        {
            for (uint32_t dx = 0; dx < 2 && (srcX + dx) < srcWidth; ++dx)
            {
                uint32_t srcIdx = ((srcY + dy) * srcWidth + (srcX + dx)) * 4;
                sumR += srcMip.data[srcIdx + 0];
                sumG += srcMip.data[srcIdx + 1];
                sumB += srcMip.data[srcIdx + 2];
                ++samples;
            }
        }

        if (samples == 0)
        {
            return;
        }

        dstData[dstIdx + 0] = static_cast<uint8_t>(sumR / samples);
        dstData[dstIdx + 1] = static_cast<uint8_t>(sumG / samples);
        dstData[dstIdx + 2] = static_cast<uint8_t>(sumB / samples);
        dstData[dstIdx + 3] = 255;
    }

    // ============================================================================
    // Helper: Downsample a single mip level using box filter (RGBA)
    // ============================================================================
    static resource::MipLevelData downsampleLevel(
        const resource::MipLevelData& srcMip,
        uint32_t srcWidth,
        uint32_t srcHeight,
        uint32_t newWidth,
        uint32_t newHeight)
    {
        resource::MipLevelData newMip;
        newMip.width = newWidth;
        newMip.height = newHeight;
        newMip.data.resize(newWidth * newHeight * 4);

        for (uint32_t y = 0; y < newHeight; ++y)
        {
            for (uint32_t x = 0; x < newWidth; ++x)
            {
                uint32_t dstIdx = (y * newWidth + x) * 4;
                sampleBoxFilter(srcMip, srcWidth, srcHeight,
                                x * 2, y * 2, newMip.data, dstIdx);
            }
        }

        return newMip;
    }

    // ============================================================================
    // Helper: Generate mipmaps using box filter (RGBA)
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

            auto newMip = downsampleLevel(srcMip, mipWidth, mipHeight, newWidth, newHeight);

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

            resource::endian::writeLE<uint8_t>(file, static_cast<uint8_t>(resource::FileType::TEXTURE));
            resource::endian::writeLE<uint32_t>(file, Version::major);
            resource::endian::writeLE<uint32_t>(file, Version::minor);
            resource::endian::writeLE<uint32_t>(file, Version::patch);
            resource::endian::writeLE<uint32_t>(file, ormTexture.width);
            resource::endian::writeLE<uint32_t>(file, ormTexture.height);
            resource::endian::writeLE<uint32_t>(file, ormTexture.numbersOfChannels);

            uint32_t mipLevels = static_cast<uint32_t>(ormTexture.mipData.size());
            resource::endian::writeLE<uint32_t>(file, mipLevels);

            for (const auto& mip : ormTexture.mipData)
            {
                resource::endian::writeLE<uint32_t>(file, mip.width);
                resource::endian::writeLE<uint32_t>(file, mip.height);
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
    // Helper: Load textures asynchronously and wait for results
    // ============================================================================
    static void loadTextures(
        const OrmPackInput& input,
        bool hasAo, bool hasRoughness, bool hasMetallic,
        std::shared_ptr<resource::TextureData>& aoData,
        std::shared_ptr<resource::TextureData>& roughnessData,
        std::shared_ptr<resource::TextureData>& metallicData,
        OrmPackProgressCallback progressCallback)
    {
        std::future<std::shared_ptr<resource::TextureData>> aoFuture;
        std::future<std::shared_ptr<resource::TextureData>> roughnessFuture;
        std::future<std::shared_ptr<resource::TextureData>> metallicFuture;

        if (hasAo) aoFuture = resource::ResourceManager::loadTextureAsync(input.aoPath);
        if (hasRoughness) roughnessFuture = resource::ResourceManager::loadTextureAsync(input.roughnessPath);
        if (hasMetallic) metallicFuture = resource::ResourceManager::loadTextureAsync(input.metallicPath);

        if (progressCallback) progressCallback(0.1f);

        if (hasAo) aoData = aoFuture.get();
        if (hasRoughness) roughnessData = roughnessFuture.get();
        if (hasMetallic) metallicData = metallicFuture.get();
    }

    // ============================================================================
    // Helper: Validate loaded texture data
    // ============================================================================
    static bool validateLoadedTextures(
        bool hasAo, bool hasRoughness, bool hasMetallic,
        const std::shared_ptr<resource::TextureData>& aoData,
        const std::shared_ptr<resource::TextureData>& roughnessData,
        const std::shared_ptr<resource::TextureData>& metallicData,
        const OrmPackInput& input,
        std::string& errorMessage)
    {
        if (hasAo && (!aoData || aoData->textureData().empty()))
        {
            errorMessage = "Failed to load AO texture: " + input.aoPath;
            return false;
        }
        if (hasRoughness && (!roughnessData || roughnessData->textureData().empty()))
        {
            errorMessage = "Failed to load roughness texture: " + input.roughnessPath;
            return false;
        }
        if (hasMetallic && (!metallicData || metallicData->textureData().empty()))
        {
            errorMessage = "Failed to load metallic texture: " + input.metallicPath;
            return false;
        }
        return true;
    }

    // ============================================================================
    // Helper: Retrieve mip level 0 references for each channel texture
    // ============================================================================
    struct ChannelMipRefs
    {
        resource::MipLevelData emptyMip;
        const resource::MipLevelData* aoMip;
        const resource::MipLevelData* roughnessMip;
        const resource::MipLevelData* metallicMip;
    };

    static ChannelMipRefs getChannelMipRefs(
        const resource::TextureData* aoTexture,
        const resource::TextureData* roughnessTexture,
        const resource::TextureData* metallicTexture)
    {
        ChannelMipRefs refs;
        refs.aoMip = (aoTexture && !aoTexture->mipData.empty())
                         ? &aoTexture->mipData[0]
                         : &refs.emptyMip;
        refs.roughnessMip = (roughnessTexture && !roughnessTexture->mipData.empty())
                                ? &roughnessTexture->mipData[0]
                                : &refs.emptyMip;
        refs.metallicMip = (metallicTexture && !metallicTexture->mipData.empty())
                               ? &metallicTexture->mipData[0]
                               : &refs.emptyMip;
        return refs;
    }

    // ============================================================================
    // Helper: Build ORM texture from channel data, generate mipmaps
    // ============================================================================
    static resource::TextureData buildOrmTexture(
        uint32_t width, uint32_t height,
        const resource::TextureData* aoTexture,
        const resource::TextureData* roughnessTexture,
        const resource::TextureData* metallicTexture,
        const ChannelMipRefs& mipRefs,
        OrmPackProgressCallback progressCallback)
    {
        resource::TextureData ormTexture;
        ormTexture.width = width;
        ormTexture.height = height;
        ormTexture.numbersOfChannels = 4;
        ormTexture.mipLevels = 1;

        resource::MipLevelData ormMip;
        packPixels(ormMip, width, height,
                   aoTexture, roughnessTexture, metallicTexture,
                   *mipRefs.aoMip, *mipRefs.roughnessMip, *mipRefs.metallicMip,
                   progressCallback);
        ormTexture.mipData.push_back(std::move(ormMip));

        generateMipmaps(ormTexture);

        return ormTexture;
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

        bool hasAo = !input.aoPath.empty();
        bool hasRoughness = !input.roughnessPath.empty();
        bool hasMetallic = !input.metallicPath.empty();

        if (!hasAo && !hasRoughness && !hasMetallic)
        {
            result.errorMessage = "At least one texture must be provided to determine output dimensions";
            return result;
        }

        if (progressCallback) progressCallback(0.0f);

        std::shared_ptr<resource::TextureData> aoData;
        std::shared_ptr<resource::TextureData> roughnessData;
        std::shared_ptr<resource::TextureData> metallicData;

        loadTextures(input, hasAo, hasRoughness, hasMetallic,
                     aoData, roughnessData, metallicData, progressCallback);

        if (progressCallback) progressCallback(0.5f);

        if (!validateLoadedTextures(hasAo, hasRoughness, hasMetallic,
                                    aoData, roughnessData, metallicData,
                                    input, result.errorMessage))
        {
            return result;
        }

        if (progressCallback) progressCallback(0.6f);

        return packORMFromData(
            aoData.get(), roughnessData.get(), metallicData.get(),
            input.outputPath, progressCallback);
    }

    // ============================================================================
    // Public: Pack ORM from loaded texture data
    // ============================================================================
    OrmPackResult OrmTexturePacker::packORMFromData(
        const resource::TextureData* aoTexture,
        const resource::TextureData* roughnessTexture,
        const resource::TextureData* metallicTexture,
        const std::string& outputPath,
        OrmPackProgressCallback progressCallback)
    {
        OrmPackResult result;

        std::vector<const resource::TextureData*> providedTextures;
        if (aoTexture) providedTextures.push_back(aoTexture);
        if (roughnessTexture) providedTextures.push_back(roughnessTexture);
        if (metallicTexture) providedTextures.push_back(metallicTexture);

        uint32_t width, height;
        if (!validateTextureDimensions(providedTextures, width, height, result.errorMessage))
        {
            return result;
        }

        if (progressCallback) progressCallback(0.65f);

        auto mipRefs = getChannelMipRefs(aoTexture, roughnessTexture, metallicTexture);

        if (!validateMipData(aoTexture, roughnessTexture, metallicTexture,
                             *mipRefs.aoMip, *mipRefs.roughnessMip, *mipRefs.metallicMip,
                             result.errorMessage))
        {
            return result;
        }

        auto ormTexture = buildOrmTexture(width, height,
                                          aoTexture, roughnessTexture, metallicTexture,
                                          mipRefs, progressCallback);

        if (progressCallback) progressCallback(0.9f);

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
