#include "DefaultIBLTextureFactory.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/Utilities.hpp"
#include "../../core/VulkanMemoryManager.hpp"
#include <cstring>

namespace render::ibl
{
    DefaultIBLTextureFactory::DefaultIBLTextureFactory(core::Device& device)
        : device(device)
    {
    }

    DefaultIBLTextureFactory::~DefaultIBLTextureFactory()
    {
        cleanup();
    }

    void DefaultIBLTextureFactory::createDefaultTextures(vk::CommandPool commandPool)
    {
        createCubemap(commandPool, irradiance, studioIrradiance);
        createCubemap(commandPool, prefilter, studioPrefilter);
        create2DTexture(commandPool, brdfLUT);
        texturesCreated = true;
    }

    void DefaultIBLTextureFactory::cleanup()
    {
        if (!texturesCreated) return;

        auto& logicalDevice = device.getLogicalDevice();

        // Cleanup irradiance
        if (irradiance.sampler) logicalDevice.destroySampler(irradiance.sampler);
        if (irradiance.imageView) logicalDevice.destroyImageView(irradiance.imageView);
        if (irradiance.image) logicalDevice.destroyImage(irradiance.image);
        if (irradiance.imageAllocation) { device.getMemoryManager().free(irradiance.imageAllocation); irradiance.imageAllocation = {}; }

        // Cleanup prefilter
        if (prefilter.sampler) logicalDevice.destroySampler(prefilter.sampler);
        if (prefilter.imageView) logicalDevice.destroyImageView(prefilter.imageView);
        if (prefilter.image) logicalDevice.destroyImage(prefilter.image);
        if (prefilter.imageAllocation) { device.getMemoryManager().free(prefilter.imageAllocation); prefilter.imageAllocation = {}; }

        // Cleanup BRDF LUT
        if (brdfLUT.sampler) logicalDevice.destroySampler(brdfLUT.sampler);
        if (brdfLUT.imageView) logicalDevice.destroyImageView(brdfLUT.imageView);
        if (brdfLUT.image) logicalDevice.destroyImage(brdfLUT.image);
        if (brdfLUT.imageAllocation) { device.getMemoryManager().free(brdfLUT.imageAllocation); brdfLUT.imageAllocation = {}; }

        texturesCreated = false;
    }

    void DefaultIBLTextureFactory::createCubemap(
        vk::CommandPool commandPool,
        ImageData& imageData,
        const std::array<std::array<float, 4>, 6>& faceColors)
    {
        constexpr uint32_t size = 1;
        constexpr uint32_t mipLevels = 1;

        // Create cubemap image
        core::ImageInfoRequest imageRequest(device.getLogicalDevice(), device.getPhysicalDevice(),
            size, size, 6, mipLevels);
        imageRequest.format = vk::Format::eR32G32B32A32Sfloat;
        imageRequest.tiling = vk::ImageTiling::eOptimal;
        imageRequest.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
        imageRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        imageRequest.imageFlags = vk::ImageCreateFlagBits::eCubeCompatible;
        core::ImageUtilities::createImage(imageRequest, imageData.image, imageData.imageAllocation, device.getMemoryManager());

        // Create staging buffer with color data for all 6 faces
        std::vector<float> pixels(6 * 4);  // 6 faces * 4 components (RGBA)
        for (int i = 0; i < 6; i++) {
            pixels[i * 4 + 0] = faceColors[i][0];
            pixels[i * 4 + 1] = faceColors[i][1];
            pixels[i * 4 + 2] = faceColors[i][2];
            pixels[i * 4 + 3] = faceColors[i][3];
        }

        vk::DeviceSize imageSize = pixels.size() * sizeof(float);
        vk::Buffer stagingBuffer;
        core::VulkanAllocation stagingAllocation;

        core::BufferInfoRequest stagingRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        stagingRequest.size = imageSize;
        stagingRequest.usage = vk::BufferUsageFlagBits::eTransferSrc;
        stagingRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(stagingRequest, stagingBuffer, stagingAllocation, device.getMemoryManager());

        void* data = stagingAllocation.mappedPtr;
        memcpy(data, pixels.data(), imageSize);

        // Transition image layout and copy data
        vk::CommandBufferAllocateInfo cmdAllocInfo{};
        cmdAllocInfo.level = vk::CommandBufferLevel::ePrimary;
        cmdAllocInfo.commandPool = commandPool;
        cmdAllocInfo.commandBufferCount = 1;
        auto cmdBuffers = device.getLogicalDevice().allocateCommandBuffers(cmdAllocInfo);
        vk::CommandBuffer cmd = cmdBuffers[0];

        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        cmd.begin(beginInfo);

        // Transition to transfer destination
        vk::ImageMemoryBarrier barrier{};
        barrier.oldLayout = vk::ImageLayout::eUndefined;
        barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = imageData.image;
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 6;
        barrier.srcAccessMask = {};
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
            {}, nullptr, nullptr, barrier);

        // Copy buffer to image (all 6 faces)
        std::vector<vk::BufferImageCopy> copyRegions(6);
        for (uint32_t face = 0; face < 6; face++) {
            copyRegions[face].bufferOffset = face * 4 * sizeof(float);
            copyRegions[face].bufferRowLength = 0;
            copyRegions[face].bufferImageHeight = 0;
            copyRegions[face].imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            copyRegions[face].imageSubresource.mipLevel = 0;
            copyRegions[face].imageSubresource.baseArrayLayer = face;
            copyRegions[face].imageSubresource.layerCount = 1;
            copyRegions[face].imageOffset = vk::Offset3D{0, 0, 0};
            copyRegions[face].imageExtent = vk::Extent3D{size, size, 1};
        }
        cmd.copyBufferToImage(stagingBuffer, imageData.image, vk::ImageLayout::eTransferDstOptimal, copyRegions);

        // Transition to shader read
        barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
            {}, nullptr, nullptr, barrier);

        cmd.end();

        vk::SubmitInfo submitInfo{};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        device.submitGraphics(submitInfo);
        device.waitGraphicsIdle();

        device.getLogicalDevice().freeCommandBuffers(commandPool, cmd);
        core::BufferUtilities::destroyBuffer(device.getLogicalDevice(), stagingBuffer, stagingAllocation, device.getMemoryManager());

        // Create image view
        core::ImageViewInfoRequest viewRequest(device.getLogicalDevice(), imageData.image,
            vk::Format::eR32G32B32A32Sfloat, vk::ImageAspectFlagBits::eColor,
            vk::ImageViewType::eCube, 6, mipLevels);
        core::ImageUtilities::createImageView(viewRequest, imageData.imageView);

        // Create sampler - use nearest filtering for 1x1 cubemap
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eNearest;
        samplerInfo.minFilter = vk::Filter::eNearest;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.maxLod = static_cast<float>(mipLevels);
        samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;
        imageData.sampler = device.getLogicalDevice().createSampler(samplerInfo);
    }

    void DefaultIBLTextureFactory::create2DTexture(
        vk::CommandPool commandPool,
        ImageData& imageData)
    {
        constexpr uint32_t size = 1;

        // Create 2D image
        core::ImageInfoRequest imageRequest(device.getLogicalDevice(), device.getPhysicalDevice(),
            size, size, 1, 1);
        imageRequest.format = vk::Format::eR32G32B32A32Sfloat;
        imageRequest.tiling = vk::ImageTiling::eOptimal;
        imageRequest.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
        imageRequest.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        core::ImageUtilities::createImage(imageRequest, imageData.image, imageData.imageAllocation, device.getMemoryManager());

        vk::DeviceSize imageSize = sizeof(defaultBrdfPixel);
        vk::Buffer stagingBuffer;
        core::VulkanAllocation stagingAllocation;

        core::BufferInfoRequest stagingRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        stagingRequest.size = imageSize;
        stagingRequest.usage = vk::BufferUsageFlagBits::eTransferSrc;
        stagingRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(stagingRequest, stagingBuffer, stagingAllocation, device.getMemoryManager());

        void* data = stagingAllocation.mappedPtr;
        memcpy(data, defaultBrdfPixel.data(), imageSize);

        vk::CommandBufferAllocateInfo cmdAllocInfo{};
        cmdAllocInfo.level = vk::CommandBufferLevel::ePrimary;
        cmdAllocInfo.commandPool = commandPool;
        cmdAllocInfo.commandBufferCount = 1;
        auto cmdBuffers = device.getLogicalDevice().allocateCommandBuffers(cmdAllocInfo);
        vk::CommandBuffer cmd = cmdBuffers[0];

        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        cmd.begin(beginInfo);

        vk::ImageMemoryBarrier barrier{};
        barrier.oldLayout = vk::ImageLayout::eUndefined;
        barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = imageData.image;
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = {};
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
            {}, nullptr, nullptr, barrier);

        vk::BufferImageCopy copyRegion{};
        copyRegion.bufferOffset = 0;
        copyRegion.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageExtent = vk::Extent3D{size, size, 1};
        cmd.copyBufferToImage(stagingBuffer, imageData.image, vk::ImageLayout::eTransferDstOptimal, copyRegion);

        barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
            {}, nullptr, nullptr, barrier);

        cmd.end();

        vk::SubmitInfo submitInfo{};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        device.submitGraphics(submitInfo);
        device.waitGraphicsIdle();

        device.getLogicalDevice().freeCommandBuffers(commandPool, cmd);
        core::BufferUtilities::destroyBuffer(device.getLogicalDevice(), stagingBuffer, stagingAllocation, device.getMemoryManager());

        // Create image view
        core::ImageViewInfoRequest viewRequest(device.getLogicalDevice(), imageData.image,
            vk::Format::eR32G32B32A32Sfloat);
        core::ImageUtilities::createImageView(viewRequest, imageData.imageView);

        // Create sampler
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.maxLod = 1.0f;
        samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;
        imageData.sampler = device.getLogicalDevice().createSampler(samplerInfo);
    }
}
