#include "MaterialTextureCache.hpp"
#include "../../core/Device.hpp"
#include "../../core/Utilities.hpp"
#include "resource/ResourceManager.hpp"
#include "print/Logger.hpp"

namespace render::mesh
{
    MaterialTextureCache::MaterialTextureCache(core::Device& device)
        : device(device)
    {
    }

    MaterialTextureCache::~MaterialTextureCache()
    {
        cleanUp();
    }

    void MaterialTextureCache::init(vk::CommandPool cmdPool)
    {
        commandPool = cmdPool;
    }

    void MaterialTextureCache::initDescriptorResources(vk::DescriptorSetLayout layout)
    {
        // Always update the layout (in case it was recreated)
        descriptorSetLayout = layout;

        if (!descriptorPoolCreated) {
            createDescriptorPool();
        }
        
        if (!defaultTextureCreated) {
            if (commandPool) {
                createDefaultTexture();
            } else {
                loggerWarning("MaterialTextureCache: commandPool not set, cannot create default texture");
            }
        }
    }

    void MaterialTextureCache::createDescriptorPool()
    {
        // Pool for per-material descriptor sets
        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eCombinedImageSampler;
        poolSize.descriptorCount = MAX_MATERIAL_DESCRIPTOR_SETS * material::MAX_MATERIAL_TEXTURES;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = MAX_MATERIAL_DESCRIPTOR_SETS;

        descriptorPool = device.getLogicalDevice().createDescriptorPool(poolInfo);
        descriptorPoolCreated = true;
        loggerInfo("Created material descriptor pool (max {} materials)", MAX_MATERIAL_DESCRIPTOR_SETS);
    }

    vk::DescriptorSet MaterialTextureCache::allocateDescriptorSet()
    {
        if (!descriptorPoolCreated || !descriptorSetLayout) {
            loggerError("Cannot allocate descriptor set: pool or layout not initialized");
            return nullptr;
        }

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        auto sets = device.getLogicalDevice().allocateDescriptorSets(allocInfo);
        return sets[0];
    }

    void MaterialTextureCache::updateMaterialDescriptorSet(vk::DescriptorSet set, const MaterialTexturePaths& textures)
    {
        // Ensure default texture exists
        if (!defaultTextureCreated && commandPool) {
            createDefaultTexture();
        }

        // Safety check - if default texture still not created, we can't proceed
        if (!defaultTextureCreated || !defaultTexture.view || !defaultTexture.sampler) {
            loggerError("Cannot update material descriptor set: default texture not available");
            return;
        }

        std::array<vk::DescriptorImageInfo, material::MAX_MATERIAL_TEXTURES> imageInfos;

        // Helper to get view/sampler for a texture path
        auto getViewSampler = [this](const std::string& path) -> std::pair<vk::ImageView, vk::Sampler> {
            if (path.empty()) {
                return {defaultTexture.view, defaultTexture.sampler};
            }
            auto it = textureCache.find(path);
            if (it != textureCache.end()) {
                return {it->second.view, it->second.sampler};
            }
            return {defaultTexture.view, defaultTexture.sampler};
        };

        // Fill all 16 texture slots using the getPath helper
        for (int i = 0; i < material::MAX_MATERIAL_TEXTURES; ++i) {
            auto [view, sampler] = getViewSampler(textures.getPath(i));
            imageInfos[i].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            imageInfos[i].imageView = view;
            imageInfos[i].sampler = sampler;
        }

        vk::WriteDescriptorSet writeSet{};
        writeSet.dstSet = set;
        writeSet.dstBinding = 0;
        writeSet.dstArrayElement = 0;
        writeSet.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writeSet.descriptorCount = material::MAX_MATERIAL_TEXTURES;
        writeSet.pImageInfo = imageInfos.data();

        device.getLogicalDevice().updateDescriptorSets(writeSet, nullptr);
    }

    vk::DescriptorSet MaterialTextureCache::getOrCreateMaterialDescriptorSet(
        const std::string& materialPath,
        const MaterialTexturePaths& textures)
    {
        if (!descriptorPoolCreated) {
            loggerWarning("Descriptor pool not initialized, cannot create material descriptor set");
            return nullptr;
        }

        // Check cache first
        auto it = materialDescriptorSets.find(materialPath);
        if (it != materialDescriptorSets.end()) {
            return it->second;
        }

        // Load all textures before creating descriptor set
        // This ensures textures are in cache when we update the descriptor set
        for (int i = 0; i < material::MAX_MATERIAL_TEXTURES; ++i) {
            const std::string& path = textures.getPath(i);
            if (!path.empty()) {
                loadTexture(path);
            }
        }

        // Allocate and update new descriptor set
        vk::DescriptorSet set = allocateDescriptorSet();
        if (!set) {
            loggerError("Failed to allocate descriptor set for material: {}", materialPath);
            return nullptr;
        }

        updateMaterialDescriptorSet(set, textures);
        materialDescriptorSets[materialPath] = set;

        loggerInfo("Created descriptor set for material: {}", materialPath);
        return set;
    }

    void MaterialTextureCache::invalidateMaterialDescriptorSet(const std::string& materialPath)
    {
        auto it = materialDescriptorSets.find(materialPath);
        if (it != materialDescriptorSets.end()) {
            if (descriptorPool && it->second) {
                device.getLogicalDevice().freeDescriptorSets(descriptorPool, it->second);
            }
            materialDescriptorSets.erase(it);
            loggerInfo("Invalidated descriptor set for material: {}", materialPath);
        }
    }

    void MaterialTextureCache::resetDescriptorResources()
    {
        if (descriptorPoolCreated && descriptorPool) {
            for (auto& [path, set] : materialDescriptorSets) {
                if (set) {
                    device.getLogicalDevice().freeDescriptorSets(descriptorPool, set);
                }
            }
            materialDescriptorSets.clear();
            
            device.getLogicalDevice().destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }

        // Reset state so initDescriptorResources will recreate
        descriptorPoolCreated = false;
        descriptorSetLayout = nullptr;
        loggerInfo("Reset MaterialTextureCache descriptor resources");
    }

    vk::ImageView MaterialTextureCache::getViewForPath(const std::string& path) const
    {
        if (path.empty()) return defaultTexture.view;
        auto it = textureCache.find(path);
        if (it != textureCache.end()) {
            return it->second.view;
        }
        return defaultTexture.view;
    }

    vk::Sampler MaterialTextureCache::getSamplerForPath(const std::string& path) const
    {
        if (path.empty()) return defaultTexture.sampler;
        auto it = textureCache.find(path);
        if (it != textureCache.end()) {
            return it->second.sampler;
        }
        return defaultTexture.sampler;
    }

    void MaterialTextureCache::cleanUp()
    {
        // Cleanup per-material descriptor sets
        if (descriptorPoolCreated && descriptorPool) {
            for (auto& [path, set] : materialDescriptorSets) {
                if (set) {
                    device.getLogicalDevice().freeDescriptorSets(descriptorPool, set);
                }
            }
            materialDescriptorSets.clear();
            device.getLogicalDevice().destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
            descriptorPoolCreated = false;
        }

        // Cleanup cached textures
        for (auto& [path, tex] : textureCache) {
            if (tex.sampler) device.getLogicalDevice().destroySampler(tex.sampler);
            if (tex.view) device.getLogicalDevice().destroyImageView(tex.view);
            if (tex.image) device.getLogicalDevice().destroyImage(tex.image);
            if (tex.memory) device.getLogicalDevice().freeMemory(tex.memory);
        }
        textureCache.clear();

        // Cleanup default texture
        if (defaultTextureCreated) {
            if (defaultTexture.sampler) device.getLogicalDevice().destroySampler(defaultTexture.sampler);
            if (defaultTexture.view) device.getLogicalDevice().destroyImageView(defaultTexture.view);
            if (defaultTexture.image) device.getLogicalDevice().destroyImage(defaultTexture.image);
            if (defaultTexture.memory) device.getLogicalDevice().freeMemory(defaultTexture.memory);
            defaultTexture = {};
            defaultTextureCreated = false;
        }
    }

    void MaterialTextureCache::createDefaultTexture()
    {
        if (defaultTextureCreated) return;

        // Create a 1x1 white texture for empty slots
        const uint32_t size = 1;
        std::array<uint8_t, 4> pixels = {255, 255, 255, 255};  // RGBA white

        vk::ImageCreateInfo imageInfo{};
        imageInfo.imageType = vk::ImageType::e2D;
        imageInfo.extent = vk::Extent3D{size, size, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = vk::Format::eR8G8B8A8Unorm;
        imageInfo.tiling = vk::ImageTiling::eOptimal;
        imageInfo.initialLayout = vk::ImageLayout::eUndefined;
        imageInfo.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
        imageInfo.samples = vk::SampleCountFlagBits::e1;
        imageInfo.sharingMode = vk::SharingMode::eExclusive;

        defaultTexture.image = device.getLogicalDevice().createImage(imageInfo);

        vk::MemoryRequirements memReqs = device.getLogicalDevice().getImageMemoryRequirements(defaultTexture.image);
        vk::MemoryAllocateInfo allocInfo{};
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = core::Utilities::findMemoryType(device.getPhysicalDevice(),
            memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        defaultTexture.memory = device.getLogicalDevice().allocateMemory(allocInfo);
        device.getLogicalDevice().bindImageMemory(defaultTexture.image, defaultTexture.memory, 0);

        // Create staging buffer and copy
        vk::DeviceSize imageSize = sizeof(pixels);
        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingMemory;

        core::BufferInfoRequest stagingRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        stagingRequest.size = imageSize;
        stagingRequest.usage = vk::BufferUsageFlagBits::eTransferSrc;
        stagingRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        core::Utilities::createBuffer(stagingRequest, stagingBuffer, stagingMemory);

        void* data;
        [[maybe_unused]] auto mapResult = device.getLogicalDevice().mapMemory(stagingMemory, 0, imageSize, {}, &data);
        memcpy(data, pixels.data(), static_cast<size_t>(imageSize));
        device.getLogicalDevice().unmapMemory(stagingMemory);

        // Transition and copy
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
        barrier.image = defaultTexture.image;
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
        copyRegion.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageExtent = vk::Extent3D{size, size, 1};
        cmd.copyBufferToImage(stagingBuffer, defaultTexture.image, vk::ImageLayout::eTransferDstOptimal, copyRegion);

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
        viewInfo.image = defaultTexture.image;
        viewInfo.viewType = vk::ImageViewType::e2D;
        viewInfo.format = vk::Format::eR8G8B8A8Unorm;
        viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        defaultTexture.view = device.getLogicalDevice().createImageView(viewInfo);

        // Create sampler
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 1.0f;
        defaultTexture.sampler = device.getLogicalDevice().createSampler(samplerInfo);

        defaultTextureCreated = true;
        loggerInfo("Created default 1x1 white texture for material slots");
    }

    // Calculate number of mip levels for a texture
    static uint32_t calculateMipLevels(uint32_t width, uint32_t height) {
        return static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
    }

    bool MaterialTextureCache::loadTexture(const std::string& path)
    {
        if (path.empty()) return false;
        if (textureCache.contains(path)) return true;  // Already loaded

        // Ensure default texture exists
        if (!defaultTextureCreated) {
            createDefaultTexture();
        }

        // Load texture from .vfImage file
        auto textureFuture = resource::ResourceManager::loadTextureAsync(path);
        auto textureData = textureFuture.get();

        if (!textureData || textureData->textureData().empty()) {
            loggerWarning("Failed to load texture: {}", path);
            return false;
        }

        TextureGPU tex{};

        // Calculate mip levels
        uint32_t mipLevels = calculateMipLevels(textureData->width, textureData->height);

        // Create image with mipmap support
        vk::ImageCreateInfo imageInfo{};
        imageInfo.imageType = vk::ImageType::e2D;
        imageInfo.extent = vk::Extent3D{textureData->width, textureData->height, 1};
        imageInfo.mipLevels = mipLevels;
        imageInfo.arrayLayers = 1;
        imageInfo.format = vk::Format::eR8G8B8A8Unorm;
        imageInfo.tiling = vk::ImageTiling::eOptimal;
        imageInfo.initialLayout = vk::ImageLayout::eUndefined;
        imageInfo.usage = vk::ImageUsageFlagBits::eSampled |
                          vk::ImageUsageFlagBits::eTransferDst |
                          vk::ImageUsageFlagBits::eTransferSrc;  // Need TransferSrc for mipmap generation
        imageInfo.samples = vk::SampleCountFlagBits::e1;
        imageInfo.sharingMode = vk::SharingMode::eExclusive;

        tex.image = device.getLogicalDevice().createImage(imageInfo);

        vk::MemoryRequirements memReqs = device.getLogicalDevice().getImageMemoryRequirements(tex.image);
        vk::MemoryAllocateInfo allocInfo{};
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = core::Utilities::findMemoryType(device.getPhysicalDevice(),
            memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        tex.memory = device.getLogicalDevice().allocateMemory(allocInfo);
        device.getLogicalDevice().bindImageMemory(tex.image, tex.memory, 0);

        // Create staging buffer and copy
        vk::DeviceSize imageSize = textureData->textureData().size();
        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingMemory;

        core::BufferInfoRequest stagingRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        stagingRequest.size = imageSize;
        stagingRequest.usage = vk::BufferUsageFlagBits::eTransferSrc;
        stagingRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        core::Utilities::createBuffer(stagingRequest, stagingBuffer, stagingMemory);

        void* data;
        [[maybe_unused]] auto mapResult = device.getLogicalDevice().mapMemory(stagingMemory, 0, imageSize, {}, &data);
        memcpy(data, textureData->textureData().data(), static_cast<size_t>(imageSize));
        device.getLogicalDevice().unmapMemory(stagingMemory);

        // Transition and copy
        vk::CommandBufferAllocateInfo cmdAllocInfo{};
        cmdAllocInfo.level = vk::CommandBufferLevel::ePrimary;
        cmdAllocInfo.commandPool = commandPool;
        cmdAllocInfo.commandBufferCount = 1;
        auto cmdBuffers = device.getLogicalDevice().allocateCommandBuffers(cmdAllocInfo);
        vk::CommandBuffer cmd = cmdBuffers[0];

        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        cmd.begin(beginInfo);

        // Transition mip level 0 to transfer dst for initial copy
        vk::ImageMemoryBarrier barrier{};
        barrier.oldLayout = vk::ImageLayout::eUndefined;
        barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = tex.image;
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = {};
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
            {}, nullptr, nullptr, barrier);

        // Copy base mip level from staging buffer
        vk::BufferImageCopy copyRegion{};
        copyRegion.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageExtent = vk::Extent3D{textureData->width, textureData->height, 1};
        cmd.copyBufferToImage(stagingBuffer, tex.image, vk::ImageLayout::eTransferDstOptimal, copyRegion);

        // Generate mipmaps using blit commands
        int32_t mipWidth = static_cast<int32_t>(textureData->width);
        int32_t mipHeight = static_cast<int32_t>(textureData->height);

        for (uint32_t i = 1; i < mipLevels; i++) {
            // Transition previous mip level to transfer src
            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.subresourceRange.levelCount = 1;
            barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
            barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer,
                {}, nullptr, nullptr, barrier);

            // Blit from previous mip to current mip
            vk::ImageBlit blit{};
            blit.srcOffsets[0] = vk::Offset3D{0, 0, 0};
            blit.srcOffsets[1] = vk::Offset3D{mipWidth, mipHeight, 1};
            blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            blit.srcSubresource.mipLevel = i - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount = 1;

            int32_t nextMipWidth = mipWidth > 1 ? mipWidth / 2 : 1;
            int32_t nextMipHeight = mipHeight > 1 ? mipHeight / 2 : 1;

            blit.dstOffsets[0] = vk::Offset3D{0, 0, 0};
            blit.dstOffsets[1] = vk::Offset3D{nextMipWidth, nextMipHeight, 1};
            blit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            blit.dstSubresource.mipLevel = i;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount = 1;

            cmd.blitImage(tex.image, vk::ImageLayout::eTransferSrcOptimal,
                         tex.image, vk::ImageLayout::eTransferDstOptimal,
                         blit, vk::Filter::eLinear);

            // Transition previous mip to shader read optimal
            barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
            barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
                {}, nullptr, nullptr, barrier);

            mipWidth = nextMipWidth;
            mipHeight = nextMipHeight;
        }

        // Transition last mip level to shader read optimal
        barrier.subresourceRange.baseMipLevel = mipLevels - 1;
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

        // Create image view with all mip levels
        vk::ImageViewCreateInfo viewInfo{};
        viewInfo.image = tex.image;
        viewInfo.viewType = vk::ImageViewType::e2D;
        viewInfo.format = vk::Format::eR8G8B8A8Unorm;
        viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = mipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        tex.view = device.getLogicalDevice().createImageView(viewInfo);

        // Create sampler with mipmap support
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = 16.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = static_cast<float>(mipLevels);
        tex.sampler = device.getLogicalDevice().createSampler(samplerInfo);

        textureCache[path] = tex;
        loggerInfo("Loaded material texture: {} ({}x{}, {} mip levels)", path, textureData->width, textureData->height, mipLevels);
        return true;
    }
}
