#include "MaterialTextureCache.hpp"
#include "../../core/Device.hpp"
#include "../../core/Texture.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/ImageUtilities.hpp"
#include "material/MaterialTypes.hpp"
#include "resource/AssetLifecycleManager.hpp"
#include "print/Log.hpp"

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
        descriptorSetLayout = layout;

        if (!descriptorPoolCreated)
        {
            createDescriptorPool();
        }

        if (!defaultTextureCreated)
        {
            if (commandPool)
            {
                createDefaultTexture();
            }
            else
            {
                vfLogWarning("MaterialTextureCache: commandPool not set, cannot create default texture");
            }
        }
    }

    void MaterialTextureCache::createDescriptorPool()
    {
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
    }

    vk::DescriptorSet MaterialTextureCache::allocateDescriptorSet()
    {
        if (!descriptorPoolCreated || !descriptorSetLayout)
        {
            vfLogError("Cannot allocate descriptor set: pool or layout not initialized");
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
        if (!defaultTextureCreated && commandPool)
        {
            createDefaultTexture();
        }
        
        if (!defaultTextureCreated || !defaultTexture.view || !defaultTexture.sampler)
        {
            vfLogError("Cannot update material descriptor set: default texture not available");
            return;
        }

        std::array<vk::DescriptorImageInfo, material::MAX_MATERIAL_TEXTURES> imageInfos;

        auto getViewSampler = [this](const std::string& path) -> std::pair<vk::ImageView, vk::Sampler>
        {
            if (path.empty())
            {
                return {defaultTexture.view, defaultTexture.sampler};
            }
            auto it = textureCache.find(path);
            if (it != textureCache.end() && it->second)
            {
                return {it->second->getImageView(), it->second->getSampler()};
            }
            return {defaultTexture.view, defaultTexture.sampler};
        };
        
        for (int i = 0; i < material::MAX_MATERIAL_TEXTURES; ++i)
        {
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
        if (!descriptorPoolCreated)
        {
            vfLogWarning("Descriptor pool not initialized, cannot create material descriptor set");
            return nullptr;
        }

        // Check cache first
        auto it = materialDescriptorSets.find(materialPath);
        if (it != materialDescriptorSets.end())
        {
            return it->second;
        }
        
        for (int i = 0; i < material::MAX_MATERIAL_TEXTURES; ++i)
        {
            const std::string& path = textures.getPath(i);
            if (!path.empty())
            {
                loadTexture(path);
            }
        }

        // Register texture dependencies so textures stay alive while material is tracked
        auto& lifecycle = resource::AssetLifecycleManager::instance();
        if (lifecycle.isTracked(materialPath))
        {
            for (int i = 0; i < material::MAX_MATERIAL_TEXTURES; ++i)
            {
                const std::string& path = textures.getPath(i);
                if (!path.empty())
                {
                    lifecycle.addDependency(materialPath, path, resource::AssetType::Texture);
                }
            }
        }

        vk::DescriptorSet set = allocateDescriptorSet();
        if (!set)
        {
            vfLogError("Failed to allocate descriptor set for material: {}", materialPath);
            return nullptr;
        }

        updateMaterialDescriptorSet(set, textures);
        materialDescriptorSets[materialPath] = set;

        return set;
    }

    void MaterialTextureCache::invalidateMaterialDescriptorSet(const std::string& materialPath)
    {
        auto it = materialDescriptorSets.find(materialPath);
        if (it != materialDescriptorSets.end())
        {
            if (descriptorPool && it->second)
            {
                device.getLogicalDevice().freeDescriptorSets(descriptorPool, it->second);
            }
            materialDescriptorSets.erase(it);
            vfLogInfo("Invalidated descriptor set for material: {}", materialPath);
        }
    }

    void MaterialTextureCache::resetDescriptorResources()
    {
        if (descriptorPoolCreated && descriptorPool)
        {
            for (auto& [path, set] : materialDescriptorSets)
            {
                if (set)
                {
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
        vfLogInfo("Reset MaterialTextureCache descriptor resources");
    }

    vk::ImageView MaterialTextureCache::getViewForPath(const std::string& path) const
    {
        if (path.empty()) return defaultTexture.view;
        auto it = textureCache.find(path);
        if (it != textureCache.end() && it->second)
        {
            return it->second->getImageView();
        }
        return defaultTexture.view;
    }

    vk::Sampler MaterialTextureCache::getSamplerForPath(const std::string& path) const
    {
        if (path.empty()) return defaultTexture.sampler;
        auto it = textureCache.find(path);
        if (it != textureCache.end() && it->second)
        {
            return it->second->getSampler();
        }
        return defaultTexture.sampler;
    }

    void MaterialTextureCache::cleanUp()
    {
        if (descriptorPoolCreated && descriptorPool)
        {
            for (auto& [path, set] : materialDescriptorSets)
            {
                if (set)
                {
                    device.getLogicalDevice().freeDescriptorSets(descriptorPool, set);
                }
            }
            materialDescriptorSets.clear();
            device.getLogicalDevice().destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
            descriptorPoolCreated = false;
        }

        // Texture objects clean themselves up via destructor
        textureCache.clear();

        if (defaultTextureCreated)
        {
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

        core::ImageInfoRequest imageInfo(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            DEFAULT_TEXTURE_SIZE, DEFAULT_TEXTURE_SIZE, 1, 1,
            vk::Format::eR8G8B8A8Unorm,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::ImageUtilities::createImage(imageInfo, defaultTexture.image, defaultTexture.memory);

        // Create staging buffer and copy
        constexpr vk::DeviceSize imageSize = sizeof(DEFAULT_TEXTURE_PIXELS);
        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingMemory;

        core::BufferInfoRequest stagingRequest(device.getLogicalDevice(), device.getPhysicalDevice());
        stagingRequest.size = imageSize;
        stagingRequest.usage = vk::BufferUsageFlagBits::eTransferSrc;
        stagingRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(stagingRequest, stagingBuffer, stagingMemory);

        void* data;
        [[maybe_unused]] auto mapResult = device.getLogicalDevice().mapMemory(stagingMemory, 0, imageSize, {}, &data);
        memcpy(data, DEFAULT_TEXTURE_PIXELS.data(), static_cast<size_t>(imageSize));
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
        copyRegion.imageExtent = vk::Extent3D{DEFAULT_TEXTURE_SIZE, DEFAULT_TEXTURE_SIZE, 1};
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
        
        core::ImageViewInfoRequest viewInfo(
            device.getLogicalDevice(),
            defaultTexture.image,
            vk::Format::eR8G8B8A8Unorm,
            vk::ImageAspectFlagBits::eColor,
            vk::ImageViewType::e2D
        );
        core::ImageUtilities::createImageView(viewInfo, defaultTexture.view);
        
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
    }
    
    void MaterialTextureCache::unloadTexture(const std::string& path)
    {
        if (path.empty()) return;

        auto it = textureCache.find(path);
        if (it == textureCache.end()) return;

        // Invalidate any material descriptor sets that reference this texture
        std::vector<std::string> materialsToInvalidate;
        for (const auto& [matPath, set] : materialDescriptorSets)
        {
            // We can't easily check which materials use this texture without
            // storing the association. Just remove the texture and let
            // descriptor sets be rebuilt on next use.
        }

        // The unique_ptr destructor will clean up the Vulkan resources
        textureCache.erase(it);
        vfLogInfo("MaterialTextureCache: Unloaded texture '{}'", path);
    }

    bool MaterialTextureCache::loadTexture(const std::string& path)
    {
        if (path.empty()) return false;
        if (textureCache.contains(path)) return true;

        if (!defaultTextureCreated)
        {
            createDefaultTexture();
        }

        auto texture = std::make_unique<core::Texture>(device);
        try
        {
            texture->loadTextureFromFile(path, vk::Format::eR8G8B8A8Unorm, false);
        }
        catch (const std::exception& e)
        {
            vfLogWarning("Failed to load texture '{}': {}", path, e.what());
            return false;
        }

        const auto& imgData = texture->getImageData();
        vfLogInfo("Loaded material texture: {} ({}x{}, {} mip levels)", path,
                   imgData.width, imgData.height, imgData.mipLevels);

        textureCache[path] = std::move(texture);
        return true;
    }
}
