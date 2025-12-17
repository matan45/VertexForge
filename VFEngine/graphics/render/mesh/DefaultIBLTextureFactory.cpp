#include "DefaultIBLTextureFactory.hpp"
#include "../../core/Device.hpp"
#include "../../core/Utilities.hpp"
#include <cstring>

namespace render::mesh
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
        // Completely uniform IBL for smooth shading - no cubemap face boundaries visible
        // All faces identical to eliminate any banding from face transitions
        std::array<std::array<float, 4>, 6> studioIrradiance = {{
            {0.8f, 0.8f, 0.85f, 1.0f},   // +X
            {0.8f, 0.8f, 0.85f, 1.0f},   // -X
            {0.8f, 0.8f, 0.85f, 1.0f},   // +Y
            {0.8f, 0.8f, 0.85f, 1.0f},   // -Y
            {0.8f, 0.8f, 0.85f, 1.0f},   // +Z
            {0.8f, 0.8f, 0.85f, 1.0f}    // -Z
        }};

        std::array<std::array<float, 4>, 6> studioPrefilter = {{
            {0.6f, 0.6f, 0.65f, 1.0f},   // +X
            {0.6f, 0.6f, 0.65f, 1.0f},   // -X
            {0.6f, 0.6f, 0.65f, 1.0f},   // +Y
            {0.6f, 0.6f, 0.65f, 1.0f},   // -Y
            {0.6f, 0.6f, 0.65f, 1.0f},   // +Z
            {0.6f, 0.6f, 0.65f, 1.0f}    // -Z
        }};

        // Irradiance: studio ambient lighting
        createCubemap(commandPool, irradiance, studioIrradiance);
        // Prefilter: studio reflections
        createCubemap(commandPool, prefilter, studioPrefilter);
        // BRDF LUT: 2D texture
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
        if (irradiance.imageMemory) logicalDevice.freeMemory(irradiance.imageMemory);

        // Cleanup prefilter
        if (prefilter.sampler) logicalDevice.destroySampler(prefilter.sampler);
        if (prefilter.imageView) logicalDevice.destroyImageView(prefilter.imageView);
        if (prefilter.image) logicalDevice.destroyImage(prefilter.image);
        if (prefilter.imageMemory) logicalDevice.freeMemory(prefilter.imageMemory);

        // Cleanup BRDF LUT
        if (brdfLUT.sampler) logicalDevice.destroySampler(brdfLUT.sampler);
        if (brdfLUT.imageView) logicalDevice.destroyImageView(brdfLUT.imageView);
        if (brdfLUT.image) logicalDevice.destroyImage(brdfLUT.image);
        if (brdfLUT.imageMemory) logicalDevice.freeMemory(brdfLUT.imageMemory);

        texturesCreated = false;
    }

    void DefaultIBLTextureFactory::createCubemap(
        vk::CommandPool commandPool,
        ibl::ImageData& imageData,
        const std::array<std::array<float, 4>, 6>& faceColors)
    {
        const uint32_t size = 1;
        const uint32_t mipLevels = 1;

        // Create image
        vk::ImageCreateInfo imageInfo{};
        imageInfo.imageType = vk::ImageType::e2D;
        imageInfo.extent = vk::Extent3D{size, size, 1};
        imageInfo.mipLevels = mipLevels;
        imageInfo.arrayLayers = 6;  // Cubemap
        imageInfo.format = vk::Format::eR32G32B32A32Sfloat;
        imageInfo.tiling = vk::ImageTiling::eOptimal;
        imageInfo.initialLayout = vk::ImageLayout::eUndefined;
        imageInfo.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
        imageInfo.samples = vk::SampleCountFlagBits::e1;
        imageInfo.sharingMode = vk::SharingMode::eExclusive;
        imageInfo.flags = vk::ImageCreateFlagBits::eCubeCompatible;

        imageData.image = device.getLogicalDevice().createImage(imageInfo);

        // Allocate memory
        vk::MemoryRequirements memRequirements = device.getLogicalDevice().getImageMemoryRequirements(imageData.image);
        vk::MemoryAllocateInfo allocInfo{};
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = core::Utilities::findMemoryType(device.getPhysicalDevice(),
            memRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        imageData.imageMemory = device.getLogicalDevice().allocateMemory(allocInfo);
        device.getLogicalDevice().bindImageMemory(imageData.image, imageData.imageMemory, 0);

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
        vk::DeviceMemory stagingMemory;

        core::BufferInfoRequest stagingRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        stagingRequest.size = imageSize;
        stagingRequest.usage = vk::BufferUsageFlagBits::eTransferSrc;
        stagingRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        core::Utilities::createBuffer(stagingRequest, stagingBuffer, stagingMemory);

        void* data;
        [[maybe_unused]] auto mapResult = device.getLogicalDevice().mapMemory(stagingMemory, 0, imageSize, {}, &data);
        memcpy(data, pixels.data(), imageSize);
        device.getLogicalDevice().unmapMemory(stagingMemory);

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
        device.getGraphicsQueue().submit(submitInfo);
        device.getGraphicsQueue().waitIdle();

        device.getLogicalDevice().freeCommandBuffers(commandPool, cmd);
        device.getLogicalDevice().destroyBuffer(stagingBuffer);
        device.getLogicalDevice().freeMemory(stagingMemory);

        // Create image view
        vk::ImageViewCreateInfo viewInfo{};
        viewInfo.image = imageData.image;
        viewInfo.viewType = vk::ImageViewType::eCube;
        viewInfo.format = vk::Format::eR32G32B32A32Sfloat;
        viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = mipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 6;
        imageData.imageView = device.getLogicalDevice().createImageView(viewInfo);

        // Create sampler - use nearest filtering for 1x1 cubemap to prevent face blending
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eNearest;
        samplerInfo.minFilter = vk::Filter::eNearest;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = static_cast<float>(mipLevels);
        samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;
        imageData.sampler = device.getLogicalDevice().createSampler(samplerInfo);
    }

    void DefaultIBLTextureFactory::create2DTexture(
        vk::CommandPool commandPool,
        ibl::ImageData& imageData)
    {
        const uint32_t size = 1;

        vk::ImageCreateInfo imageInfo{};
        imageInfo.imageType = vk::ImageType::e2D;
        imageInfo.extent = vk::Extent3D{size, size, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = vk::Format::eR32G32B32A32Sfloat;
        imageInfo.tiling = vk::ImageTiling::eOptimal;
        imageInfo.initialLayout = vk::ImageLayout::eUndefined;
        imageInfo.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
        imageInfo.samples = vk::SampleCountFlagBits::e1;
        imageInfo.sharingMode = vk::SharingMode::eExclusive;

        imageData.image = device.getLogicalDevice().createImage(imageInfo);

        vk::MemoryRequirements memRequirements = device.getLogicalDevice().getImageMemoryRequirements(imageData.image);
        vk::MemoryAllocateInfo allocInfo{};
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = core::Utilities::findMemoryType(device.getPhysicalDevice(),
            memRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        imageData.imageMemory = device.getLogicalDevice().allocateMemory(allocInfo);
        device.getLogicalDevice().bindImageMemory(imageData.image, imageData.imageMemory, 0);

        // Default BRDF LUT value: (scale=1.0, bias=0.0) for specular = prefilteredColor * F
        std::array<float, 4> pixel = {1.0f, 0.0f, 0.0f, 1.0f};
        vk::DeviceSize imageSize = sizeof(pixel);
        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingMemory;

        core::BufferInfoRequest stagingRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        stagingRequest.size = imageSize;
        stagingRequest.usage = vk::BufferUsageFlagBits::eTransferSrc;
        stagingRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        core::Utilities::createBuffer(stagingRequest, stagingBuffer, stagingMemory);

        void* data;
        [[maybe_unused]] auto mapResult = device.getLogicalDevice().mapMemory(stagingMemory, 0, imageSize, {}, &data);
        memcpy(data, pixel.data(), imageSize);
        device.getLogicalDevice().unmapMemory(stagingMemory);

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
        device.getGraphicsQueue().submit(submitInfo);
        device.getGraphicsQueue().waitIdle();

        device.getLogicalDevice().freeCommandBuffers(commandPool, cmd);
        device.getLogicalDevice().destroyBuffer(stagingBuffer);
        device.getLogicalDevice().freeMemory(stagingMemory);

        vk::ImageViewCreateInfo viewInfo{};
        viewInfo.image = imageData.image;
        viewInfo.viewType = vk::ImageViewType::e2D;
        viewInfo.format = vk::Format::eR32G32B32A32Sfloat;
        viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        imageData.imageView = device.getLogicalDevice().createImageView(viewInfo);

        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 1.0f;
        samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueWhite;
        imageData.sampler = device.getLogicalDevice().createSampler(samplerInfo);
    }
}
