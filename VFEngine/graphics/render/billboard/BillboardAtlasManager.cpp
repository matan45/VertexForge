#include "BillboardAtlasManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/Texture.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/Utilities.hpp"
#include "print/Log.hpp"
#include <cstring>
#include <filesystem>

namespace render::billboard
{
    BillboardAtlasManager::BillboardAtlasManager(core::Device& device)
        : device{device}
    {
    }

    BillboardAtlasManager::~BillboardAtlasManager() = default;

    void BillboardAtlasManager::init()
    {
        createDefaultAtlas();
    }

    void BillboardAtlasManager::cleanUp()
    {
        auto& dev = device.getLogicalDevice();

        atlasTexture.reset();

        if (defaultAtlasSampler)
        {
            dev.destroySampler(defaultAtlasSampler);
            defaultAtlasSampler = nullptr;
        }
        if (defaultAtlasImageView)
        {
            dev.destroyImageView(defaultAtlasImageView);
            defaultAtlasImageView = nullptr;
        }
        if (defaultAtlasImage)
        {
            dev.destroyImage(defaultAtlasImage);
            dev.freeMemory(defaultAtlasImageMemory);
            defaultAtlasImage = nullptr;
        }

        atlasLoaded = false;
    }

    void BillboardAtlasManager::createDefaultAtlas()
    {
        constexpr uint32_t atlasSize = AtlasConfig::ATLAS_SIZE;

        core::ImageInfoRequest imageInfo(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            atlasSize, atlasSize, 1, 1,
            vk::Format::eR8G8B8A8Unorm,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::ImageUtilities::createImage(imageInfo, defaultAtlasImage, defaultAtlasImageMemory);

        core::ImageViewInfoRequest viewInfo(
            device.getLogicalDevice(),
            defaultAtlasImage,
            vk::Format::eR8G8B8A8Unorm,
            vk::ImageAspectFlagBits::eColor,
            vk::ImageViewType::e2D
        );
        core::ImageUtilities::createImageView(viewInfo, defaultAtlasImageView);

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

        defaultAtlasSampler = device.getLogicalDevice().createSampler(samplerInfo);

        // Generate simple magenta placeholder (common "missing texture" color)
        std::vector<uint8_t> atlasData(atlasSize * atlasSize * 4);
        for (uint32_t i = 0; i < atlasSize * atlasSize; ++i)
        {
            atlasData[i * 4 + 0] = 255;  // R
            atlasData[i * 4 + 1] = 0;    // G
            atlasData[i * 4 + 2] = 255;  // B
            atlasData[i * 4 + 3] = 255;  // A
        }

        vk::DeviceSize imageSize = atlasData.size();

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
                std::memcpy(data, atlasData.data(), imageSize);
                device.getLogicalDevice().unmapMemory(stagingMemory);
            }

            auto cmd = core::Utilities::beginSingleTimeCommands(device.getLogicalDevice(), device.getStagingCommandPool());

            core::ImageUtilities::transitionImageLayout(cmd.get(), defaultAtlasImage,
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
            region.imageExtent = vk::Extent3D{atlasSize, atlasSize, 1};

            cmd->copyBufferToImage(stagingBuffer, defaultAtlasImage, vk::ImageLayout::eTransferDstOptimal, region);

            core::ImageUtilities::transitionImageLayout(cmd.get(), defaultAtlasImage,
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

    bool BillboardAtlasManager::loadAtlas(const std::string& atlasPath)
    {
        if (!std::filesystem::exists(atlasPath))
        {
            vfLogWarning("Billboard atlas file not found: {}, using default atlas", atlasPath);
            return false;
        }

        device.getLogicalDevice().waitIdle();

        atlasTexture.reset();

        atlasTexture = std::make_unique<core::Texture>(device);
        atlasTexture->loadTextureFromFile(atlasPath, vk::Format::eR8G8B8A8Unorm, false);

        atlasLoaded = true;
        const auto& imgData = atlasTexture->getImageData();
        vfLogInfo("Billboard atlas loaded successfully: {} ({}x{})", atlasPath, imgData.width, imgData.height);
        return true;
    }

    vk::ImageView BillboardAtlasManager::getImageView() const
    {
        if (atlasTexture)
        {
            return atlasTexture->getImageView();
        }
        return defaultAtlasImageView;
    }

    vk::Sampler BillboardAtlasManager::getSampler() const
    {
        if (atlasTexture)
        {
            return atlasTexture->getSampler();
        }
        return defaultAtlasSampler;
    }
}
