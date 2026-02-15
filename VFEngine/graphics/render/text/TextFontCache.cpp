#include "TextFontCache.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/Utilities.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/Types.hpp"
#include "print/Logger.hpp"
#include <cstring>

namespace render::text
{
    TextFontCache::TextFontCache(core::Device& device)
        : device{device}
    {
    }

    TextFontCache::~TextFontCache() = default;

    void TextFontCache::init()
    {
        createDefaultTexture();
    }

    void TextFontCache::cleanUp()
    {
        auto& dev = device.getLogicalDevice();

        for (auto& [path, cached] : fontCache)
        {
            if (cached.atlasSampler)
                dev.destroySampler(cached.atlasSampler);
            if (cached.atlasImageView)
                dev.destroyImageView(cached.atlasImageView);
            if (cached.atlasImage)
            {
                dev.destroyImage(cached.atlasImage);
                dev.freeMemory(cached.atlasImageMemory);
            }
        }
        fontCache.clear();
        pendingLoads.clear();

        if (defaultSampler)
        {
            dev.destroySampler(defaultSampler);
            defaultSampler = nullptr;
        }
        if (defaultImageView)
        {
            dev.destroyImageView(defaultImageView);
            defaultImageView = nullptr;
        }
        if (defaultImage)
        {
            dev.destroyImage(defaultImage);
            dev.freeMemory(defaultImageMemory);
            defaultImage = nullptr;
        }
    }

    void TextFontCache::createDefaultTexture()
    {
        constexpr uint32_t size = 1;

        core::ImageInfoRequest imageInfo(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            size, size, 1, 1,
            vk::Format::eR8Unorm,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::ImageUtilities::createImage(imageInfo, defaultImage, defaultImageMemory);

        core::ImageViewInfoRequest viewInfo(
            device.getLogicalDevice(),
            defaultImage,
            vk::Format::eR8Unorm,
            vk::ImageAspectFlagBits::eColor,
            vk::ImageViewType::e2D
        );
        core::ImageUtilities::createImageView(viewInfo, defaultImageView);

        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = vk::BorderColor::eFloatTransparentBlack;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;

        defaultSampler = device.getLogicalDevice().createSampler(samplerInfo);

        // Upload 1x1 white pixel
        uint8_t whitePixel = 255;
        vk::DeviceSize imageSize = sizeof(uint8_t);

        core::BufferInfoRequest stagingRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        stagingRequest.size = imageSize;
        stagingRequest.usage = vk::BufferUsageFlagBits::eTransferSrc;
        stagingRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                    vk::MemoryPropertyFlagBits::eHostCoherent;

        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingMemory;
        core::BufferUtilities::createBuffer(stagingRequest, stagingBuffer, stagingMemory);

        auto cleanupStaging = [&]() {
            if (stagingBuffer) device.getLogicalDevice().destroyBuffer(stagingBuffer);
            if (stagingMemory) device.getLogicalDevice().freeMemory(stagingMemory);
        };

        try
        {
            void* data;
            vk::Result mapResult = device.getLogicalDevice().mapMemory(stagingMemory, 0, imageSize, {}, &data);
            if (mapResult == vk::Result::eSuccess)
            {
                std::memcpy(data, &whitePixel, imageSize);
                device.getLogicalDevice().unmapMemory(stagingMemory);
            }

            auto cmd = core::Utilities::beginSingleTimeCommands(device.getLogicalDevice(), device.getStagingCommandPool());

            core::ImageUtilities::transitionImageLayout(cmd.get(), defaultImage,
                vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
                vk::ImageAspectFlagBits::eColor);

            vk::BufferImageCopy region{};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = vk::Offset3D{0, 0, 0};
            region.imageExtent = vk::Extent3D{size, size, 1};

            cmd->copyBufferToImage(stagingBuffer, defaultImage, vk::ImageLayout::eTransferDstOptimal, region);

            core::ImageUtilities::transitionImageLayout(cmd.get(), defaultImage,
                vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageAspectFlagBits::eColor);

            core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), cmd);

            cleanupStaging();
        }
        catch (...)
        {
            cleanupStaging();
            throw;
        }
    }

    void TextFontCache::requestFont(const std::string& fontPath)
    {
        if (fontCache.contains(fontPath) || pendingLoads.contains(fontPath))
        {
            return;
        }

        PendingLoad pending;
        pending.future = resource::ResourceManager::loadFontAsync(fontPath);
        pendingLoads.emplace(fontPath, std::move(pending));
    }

    void TextFontCache::processPendingLoads()
    {
        std::vector<std::string> completed;

        for (auto& [path, pending] : pendingLoads)
        {
            if (pending.future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
            {
                auto fontData = pending.future.get();
                if (fontData)
                {
                    if (uploadFontAtlas(path, fontData))
                    {
                        loggerInfo("Text font loaded: {}", path);
                    }
                    else
                    {
                        loggerError("Failed to upload font atlas: {}", path);
                    }
                }
                else
                {
                    loggerError("Failed to load font: {}", path);
                }
                completed.push_back(path);
            }
        }

        for (const auto& path : completed)
        {
            pendingLoads.erase(path);
        }
    }

    bool TextFontCache::uploadFontAtlas(const std::string& fontPath, std::shared_ptr<resource::FontData> fontData)
    {
        const auto& atlas = fontData->atlas;
        if (atlas.width == 0 || atlas.height == 0 || atlas.pixels.empty())
        {
            return false;
        }

        CachedFont cached;
        cached.fontData = fontData;
        cached.isColorFont = (atlas.format == resource::FontAtlasFormat::RGBA_32);

        // Select format based on atlas type
        vk::Format atlasFormat = cached.isColorFont
            ? vk::Format::eR8G8B8A8Unorm
            : vk::Format::eR8Unorm;

        core::ImageInfoRequest imageInfo(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            atlas.width, atlas.height, 1, 1,
            atlasFormat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::ImageUtilities::createImage(imageInfo, cached.atlasImage, cached.atlasImageMemory);

        core::ImageViewInfoRequest viewInfo(
            device.getLogicalDevice(),
            cached.atlasImage,
            atlasFormat,
            vk::ImageAspectFlagBits::eColor,
            vk::ImageViewType::e2D
        );
        core::ImageUtilities::createImageView(viewInfo, cached.atlasImageView);

        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = vk::BorderColor::eFloatTransparentBlack;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;

        cached.atlasSampler = device.getLogicalDevice().createSampler(samplerInfo);

        // Upload atlas pixels
        vk::DeviceSize imageSize = static_cast<vk::DeviceSize>(atlas.pixels.size());

        core::BufferInfoRequest stagingRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        stagingRequest.size = imageSize;
        stagingRequest.usage = vk::BufferUsageFlagBits::eTransferSrc;
        stagingRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                    vk::MemoryPropertyFlagBits::eHostCoherent;

        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingMemory;
        core::BufferUtilities::createBuffer(stagingRequest, stagingBuffer, stagingMemory);

        auto cleanupStaging = [&]() {
            if (stagingBuffer) device.getLogicalDevice().destroyBuffer(stagingBuffer);
            if (stagingMemory) device.getLogicalDevice().freeMemory(stagingMemory);
        };

        try
        {
            void* data;
            vk::Result mapResult = device.getLogicalDevice().mapMemory(stagingMemory, 0, imageSize, {}, &data);
            if (mapResult == vk::Result::eSuccess)
            {
                std::memcpy(data, atlas.pixels.data(), imageSize);
                device.getLogicalDevice().unmapMemory(stagingMemory);
            }

            auto cmd = core::Utilities::beginSingleTimeCommands(device.getLogicalDevice(), device.getStagingCommandPool());

            core::ImageUtilities::transitionImageLayout(cmd.get(), cached.atlasImage,
                vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
                vk::ImageAspectFlagBits::eColor);

            vk::BufferImageCopy region{};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = vk::Offset3D{0, 0, 0};
            region.imageExtent = vk::Extent3D{atlas.width, atlas.height, 1};

            cmd->copyBufferToImage(stagingBuffer, cached.atlasImage, vk::ImageLayout::eTransferDstOptimal, region);

            core::ImageUtilities::transitionImageLayout(cmd.get(), cached.atlasImage,
                vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageAspectFlagBits::eColor);

            core::Utilities::endSingleTimeCommands(device.getGraphicsQueue(), cmd);

            cleanupStaging();
        }
        catch (...)
        {
            cleanupStaging();

            auto& dev = device.getLogicalDevice();
            if (cached.atlasSampler) dev.destroySampler(cached.atlasSampler);
            if (cached.atlasImageView) dev.destroyImageView(cached.atlasImageView);
            if (cached.atlasImage)
            {
                dev.destroyImage(cached.atlasImage);
                dev.freeMemory(cached.atlasImageMemory);
            }
            return false;
        }

        fontCache.emplace(fontPath, std::move(cached));
        return true;
    }

    bool TextFontCache::isFontReady(const std::string& fontPath) const
    {
        return fontCache.contains(fontPath);
    }

    const CachedFont* TextFontCache::getFont(const std::string& fontPath) const
    {
        auto it = fontCache.find(fontPath);
        if (it != fontCache.end())
        {
            return &it->second;
        }
        return nullptr;
    }
}
