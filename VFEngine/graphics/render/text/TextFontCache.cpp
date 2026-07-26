#include "TextFontCache.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/Utilities.hpp"
#include "../../core/VulkanMemoryManager.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/Types.hpp"
#include "resource/DefaultFont.hpp"
#include "resource/FontResource.hpp"
#include "resource/PathResolver.hpp"
#include "asset/AssetRef.hpp"
#include "print/Log.hpp"

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
        loadDefaultFont();
    }

    // VK-1628: the engine-shipped fallback font, registered under a sentinel key so
    // text with no (or an unresolvable) fontRef still renders. Loaded straight off
    // disk via FontResource — deliberately NOT through ResourceManager/AssetRef,
    // which would try to resolve an engine resource against the project's asset
    // database. See resource/DefaultFont.hpp.
    void TextFontCache::loadDefaultFont()
    {
        const std::string path =
            resource::PathResolver::resolveEnginePath(resource::DEFAULT_FONT_ENGINE_PATH);

        auto fontData = std::make_shared<resource::FontData>(resource::FontResource::loadFont(path));
        if (uploadFontAtlas(resource::DEFAULT_FONT_SENTINEL, std::move(fontData)))
        {
            vfLogInfo("Default font loaded: {}", path);
            return;
        }

        // Logged once at init, never retried — text without a font stays invisible,
        // which is the pre-VK-1628 behavior rather than a per-frame load storm.
        vfLogError("Failed to load default font from {}. Text with no font assigned "
                   "will not render.", path);
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
                device.getMemoryManager().free(cached.atlasImageAllocation);
                cached.atlasImageAllocation = {};
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
            device.getMemoryManager().free(defaultImageAllocation);
            defaultImageAllocation = {};
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
        core::ImageUtilities::createImage(imageInfo, defaultImage, defaultImageAllocation, device.getMemoryManager());

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
        core::VulkanAllocation stagingAllocation;
        core::BufferUtilities::createBuffer(stagingRequest, stagingBuffer, stagingAllocation, device.getMemoryManager());

        auto cleanupStaging = [&]() {
            core::BufferUtilities::destroyBuffer(device.getLogicalDevice(), stagingBuffer, stagingAllocation, device.getMemoryManager());
        };

        try
        {
            void* data = stagingAllocation.mappedPtr;
            if (data)
            {
                std::memcpy(data, &whitePixel, imageSize);
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

            core::Utilities::endSingleTimeCommands(device, cmd);

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

        // The sentinel is a cache key, not a path. It is populated once by
        // loadDefaultFont(); if that failed there is nothing to retry, and feeding
        // the literal to AssetRef::fromPath below would register it as an asset.
        if (fontPath == resource::DEFAULT_FONT_SENTINEL)
        {
            return;
        }

        PendingLoad pending;
        pending.future = resource::ResourceManager::loadFontAsync(asset::AssetRef::fromPath(fontPath));
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
                        vfLogInfo("Text font loaded: {}", path);
                    }
                    else
                    {
                        vfLogError("Failed to upload font atlas: {}", path);
                    }
                }
                else
                {
                    vfLogError("Failed to load font: {}", path);
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
        core::ImageUtilities::createImage(imageInfo, cached.atlasImage, cached.atlasImageAllocation, device.getMemoryManager());

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
        core::VulkanAllocation stagingAllocation;
        core::BufferUtilities::createBuffer(stagingRequest, stagingBuffer, stagingAllocation, device.getMemoryManager());

        auto cleanupStaging = [&]() {
            core::BufferUtilities::destroyBuffer(device.getLogicalDevice(), stagingBuffer, stagingAllocation, device.getMemoryManager());
        };

        try
        {
            void* data = stagingAllocation.mappedPtr;
            if (data)
            {
                std::memcpy(data, atlas.pixels.data(), imageSize);
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

            core::Utilities::endSingleTimeCommands(device, cmd);

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
                device.getMemoryManager().free(cached.atlasImageAllocation);
                cached.atlasImageAllocation = {};
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

    // VK-1628: the single place that decides font substitution. A font that is still
    // loading, failed to load, or failed to upload resolves to the default instead of
    // dropping the text.
    //
    // Callers MUST group instances and key descriptor sets by the returned key, never
    // by the raw path — otherwise a descriptor built while the real font was still
    // loading gets cached against the default atlas and never heals. getFont() stays
    // an exact lookup for that reason: no caller should get a default font back under
    // a key that is not the sentinel.
    const std::string& TextFontCache::resolveFontKey(const std::string& fontPath) const
    {
        static const std::string sentinel{resource::DEFAULT_FONT_SENTINEL};
        if (!fontPath.empty() && fontCache.contains(fontPath))
        {
            return fontPath;
        }
        return sentinel;
    }
}
