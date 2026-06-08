#include "PluginTextureManager.hpp"
#include <algorithm>
#include "../../core/Device.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/BufferUtilities.hpp"
#include "print/Log.hpp"

namespace render::custom
{
    PluginTextureManager::PluginTextureManager(core::Device& device)
        : device{device}
    {
    }

    PluginTextureManager::~PluginTextureManager() = default;

    vk::Format PluginTextureManager::toVkFormat(plugin::TextureFormat format)
    {
        switch (format)
        {
        case plugin::TextureFormat::R8:    return vk::Format::eR8Unorm;
        case plugin::TextureFormat::RGBA8: return vk::Format::eR8G8B8A8Unorm;
        }
        return vk::Format::eR8Unorm;
    }

    plugin::PluginTextureHandle PluginTextureManager::createTexture2D(uint32_t width, uint32_t height,
                                                                      plugin::TextureFormat format)
    {
        if (width == 0 || height == 0)
        {
            vfLogError("PluginTextureManager: texture rejected — zero dimension ({}x{})", width, height);
            return {};
        }

        std::lock_guard<std::recursive_mutex> lock(stateMutex);

        // TextureEntry holds a PerFrameBuffer (non-movable) — construct in place.
        const uint64_t id = nextId++;
        TextureEntry& entry = textures[id];
        entry.width = width;
        entry.height = height;
        entry.format = format;

        const vk::Format vkFormat = toVkFormat(format);
        const vk::DeviceSize byteSize = static_cast<vk::DeviceSize>(width) * height *
                                        plugin::textureFormatBytesPerPixel(format);

        core::ImageInfoRequest imageRequest(
            device.getLogicalDevice(), device.getPhysicalDevice(),
            width, height, 1, 1,
            vkFormat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::ImageUtilities::createImage(imageRequest, entry.image, entry.imageAllocation,
                                          device.getMemoryManager());

        core::ImageViewInfoRequest viewRequest(
            device.getLogicalDevice(), entry.image,
            vkFormat, vk::ImageAspectFlagBits::eColor,
            vk::ImageViewType::e2D, 1, 1
        );
        core::ImageUtilities::createImageView(viewRequest, entry.view);

        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        entry.sampler = device.getLogicalDevice().createSampler(samplerInfo);

        entry.staging.createStaging(device.getLogicalDevice(), device.getPhysicalDevice(),
                                    byteSize, device.getMemoryManager());

        // Zero-init so the image is in ShaderReadOnlyOptimal from the start — it can be
        // bound as the world mask before the first updateTexture2D lands.
        std::vector<std::byte> zeros(static_cast<size_t>(byteSize));
        core::ImageUtilities::uploadStagedPixelData(device, entry.image, zeros.data(), byteSize,
                                                    width, height);

        vfLogInfo("PluginTextureManager: created texture {} ({}x{}, format {})",
                  id, width, height, static_cast<uint32_t>(format));
        return plugin::PluginTextureHandle{id};
    }

    void PluginTextureManager::updateTexture2D(plugin::PluginTextureHandle handle,
                                               std::vector<std::byte>&& data)
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);

        auto it = textures.find(handle.id);
        if (it == textures.end()) return;

        TextureEntry& entry = it->second;
        const size_t expectedSize = static_cast<size_t>(entry.width) * entry.height *
                                    plugin::textureFormatBytesPerPixel(entry.format);
        if (data.size() != expectedSize)
        {
            vfLogError("PluginTextureManager: updateTexture2D size mismatch for texture {} — got {}, expected {}",
                       handle.id, data.size(), expectedSize);
            return;
        }

        entry.pendingData = std::move(data);
        entry.dirty = true;
    }

    void PluginTextureManager::destroyTexture2D(plugin::PluginTextureHandle handle)
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);

        auto it = textures.find(handle.id);
        if (it == textures.end()) return;

        if (handle.id == boundMaskId)
        {
            unbindWorldMask();
        }

        // Rare operation (plugin unload) — wait so no in-flight frame uses the image.
        device.getLogicalDevice().waitIdle();
        destroyTextureEntry(it->second);
        textures.erase(it);
    }

    void PluginTextureManager::bindWorldMask(plugin::PluginTextureHandle handle,
                                             const glm::vec3& worldMin, const glm::vec3& worldMax,
                                             const plugin::WorldMaskParams& params)
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);

        auto it = textures.find(handle.id);
        if (it == textures.end())
        {
            vfLogError("PluginTextureManager: bindWorldMask rejected — invalid texture handle {}", handle.id);
            return;
        }
        if (worldMax.x <= worldMin.x || worldMax.z <= worldMin.z)
        {
            vfLogError("PluginTextureManager: bindWorldMask rejected — degenerate world bounds");
            return;
        }

        ensureMaskResources();

        boundMaskId = handle.id;
        maskParams = params;
        maskUBOData.worldMinMax = glm::vec4(worldMin.x, worldMin.z, worldMax.x, worldMax.z);
        maskUBOData.terrainDimMin = params.terrainDimMin;
        maskUBOData.entityDiscardBelow = params.entityDiscardBelow;
        maskUBOData.flags = packMaskFlags(params, debugForceDisabled);
        writeMaskParamsUBO();

        // Bind/unbind is rare — wait so descriptor rewrites don't race in-flight frames.
        device.getLogicalDevice().waitIdle();
        updateEntityMaskDescriptor();
        ++maskDescriptorVersion;

        vfLogInfo("PluginTextureManager: bound world mask texture {} over [{}, {}] - [{}, {}]",
                  handle.id, worldMin.x, worldMin.z, worldMax.x, worldMax.z);
    }

    void PluginTextureManager::unbindWorldMask()
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);

        if (boundMaskId == 0) return;

        boundMaskId = 0;
        maskUBOData.flags &= ~MASK_FLAG_ENABLED;
        writeMaskParamsUBO();

        device.getLogicalDevice().waitIdle();
        updateEntityMaskDescriptor();   // repoint at the dummy texture
        ++maskDescriptorVersion;
    }

    void PluginTextureManager::setWorldMaskParams(const plugin::WorldMaskParams& params)
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);

        maskParams = params;
        if (!maskResourcesCreated) return;

        maskUBOData.terrainDimMin = params.terrainDimMin;
        maskUBOData.entityDiscardBelow = params.entityDiscardBelow;
        maskUBOData.flags = boundMaskId != 0 ? packMaskFlags(params, debugForceDisabled) : 0;
        writeMaskParamsUBO();
    }

    float PluginTextureManager::sampleWorldMask(float worldX, float worldZ) const
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);

        if (boundMaskId == 0) return 1.0f;
        if (!(maskUBOData.flags & MASK_FLAG_ENABLED)) return 1.0f;

        auto it = textures.find(boundMaskId);
        if (it == textures.end()) return 1.0f;

        const TextureEntry& entry = it->second;
        if (entry.pendingData.empty()) return 1.0f;   // nothing uploaded yet

        const float minX = maskUBOData.worldMinMax.x;
        const float minZ = maskUBOData.worldMinMax.y;
        const float maxX = maskUBOData.worldMinMax.z;
        const float maxZ = maskUBOData.worldMinMax.w;
        if (worldX < minX || worldX > maxX || worldZ < minZ || worldZ > maxZ) return 1.0f;

        const float u = (worldX - minX) / (maxX - minX);
        const float v = (worldZ - minZ) / (maxZ - minZ);
        const int x = std::clamp(static_cast<int>(u * static_cast<float>(entry.width)),
                                 0, static_cast<int>(entry.width) - 1);
        const int z = std::clamp(static_cast<int>(v * static_cast<float>(entry.height)),
                                 0, static_cast<int>(entry.height) - 1);

        // Red channel = first byte of the texel for both R8 and RGBA8.
        const size_t idx = (static_cast<size_t>(z) * entry.width + x) *
                           plugin::textureFormatBytesPerPixel(entry.format);
        if (idx >= entry.pendingData.size()) return 1.0f;

        return static_cast<float>(std::to_integer<uint8_t>(entry.pendingData[idx])) / 255.0f;
    }

    void PluginTextureManager::setDebugMaskEnabled(bool enabled)
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);

        debugForceDisabled = !enabled;
        if (!maskResourcesCreated) return;

        maskUBOData.flags = boundMaskId != 0 ? packMaskFlags(maskParams, debugForceDisabled) : 0;
        writeMaskParamsUBO();
    }

    void PluginTextureManager::flushUploads(const vk::CommandBuffer& commandBuffer)
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);

        for (auto& [id, entry] : textures)
        {
            if (!entry.dirty) continue;

            entry.staging.advance();
            entry.staging.write(entry.pendingData.data(),
                                static_cast<vk::DeviceSize>(entry.pendingData.size()));

            core::ImageUtilities::transitionImageLayout(commandBuffer, entry.image,
                vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eTransferDstOptimal,
                vk::ImageAspectFlagBits::eColor);

            vk::BufferImageCopy region{};
            region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = vk::Extent3D{entry.width, entry.height, 1};
            commandBuffer.copyBufferToImage(entry.staging.getBuffer(), entry.image,
                                            vk::ImageLayout::eTransferDstOptimal, region);

            core::ImageUtilities::transitionImageLayout(commandBuffer, entry.image,
                vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageAspectFlagBits::eColor);

            entry.dirty = false;
        }
    }

    bool PluginTextureManager::consumeNeedsPipelineRecreate()
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);

        if (!needsPipelineRecreate) return false;
        needsPipelineRecreate = false;
        return true;
    }

    vk::ImageView PluginTextureManager::getMaskImageView() const
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);

        auto it = textures.find(boundMaskId);
        return it != textures.end() ? it->second.view : dummyView;
    }

    vk::Sampler PluginTextureManager::getMaskSampler() const
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);

        auto it = textures.find(boundMaskId);
        return it != textures.end() ? it->second.sampler : dummySampler;
    }

    void PluginTextureManager::ensureMaskResources()
    {
        if (maskResourcesCreated) return;

        vk::Device vkDevice = device.getLogicalDevice();

        // Params UBO — persistent host-coherent; full-struct writes, no per-frame rebind.
        core::BufferInfoRequest paramsRequest(vkDevice, device.getPhysicalDevice());
        paramsRequest.size = sizeof(WorldMaskUBOData);
        paramsRequest.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        paramsRequest.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                                   vk::MemoryPropertyFlagBits::eHostCoherent;
        core::BufferUtilities::createBuffer(paramsRequest, maskParamsBuffer, maskParamsAllocation,
                                            device.getMemoryManager());

        // 1x1 white R8 dummy — sampled (mask = 1.0 => no effect) whenever no mask is bound.
        core::ImageInfoRequest dummyRequest(
            vkDevice, device.getPhysicalDevice(),
            1, 1, 1, 1,
            vk::Format::eR8Unorm,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::ImageUtilities::createImage(dummyRequest, dummyImage, dummyAllocation,
                                          device.getMemoryManager());

        core::ImageViewInfoRequest dummyViewRequest(
            vkDevice, dummyImage,
            vk::Format::eR8Unorm, vk::ImageAspectFlagBits::eColor,
            vk::ImageViewType::e2D, 1, 1
        );
        core::ImageUtilities::createImageView(dummyViewRequest, dummyView);

        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        dummySampler = vkDevice.createSampler(samplerInfo);

        const uint8_t white = 0xFF;
        core::ImageUtilities::uploadStagedPixelData(device, dummyImage, &white, 1, 1, 1);

        // Entity mask descriptor set: b0 = mask sampler, b1 = params UBO (fragment stage).
        std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
        bindings[0] = {0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment};
        bindings[1] = {1, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eFragment};

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        entityMaskLayout = vkDevice.createDescriptorSetLayout(layoutInfo);

        std::array<vk::DescriptorPoolSize, 2> poolSizes{};
        poolSizes[0] = {vk::DescriptorType::eCombinedImageSampler, 1};
        poolSizes[1] = {vk::DescriptorType::eUniformBuffer, 1};

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        entityMaskPool = vkDevice.createDescriptorPool(poolInfo);

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = entityMaskPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &entityMaskLayout;
        entityMaskDescriptorSet = vkDevice.allocateDescriptorSets(allocInfo)[0];

        maskResourcesCreated = true;
        needsPipelineRecreate = true;
    }

    void PluginTextureManager::writeMaskParamsUBO()
    {
        if (maskParamsAllocation.mappedPtr)
        {
            std::memcpy(maskParamsAllocation.mappedPtr, &maskUBOData, sizeof(WorldMaskUBOData));
        }
    }

    void PluginTextureManager::updateEntityMaskDescriptor()
    {
        if (!maskResourcesCreated) return;

        vk::DescriptorImageInfo imageInfo{};
        imageInfo.sampler = getMaskSampler();
        imageInfo.imageView = getMaskImageView();
        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = maskParamsBuffer;
        bufferInfo.range = sizeof(WorldMaskUBOData);

        std::array<vk::WriteDescriptorSet, 2> writes{};
        writes[0].dstSet = entityMaskDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[0].pImageInfo = &imageInfo;
        writes[1].dstSet = entityMaskDescriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = vk::DescriptorType::eUniformBuffer;
        writes[1].pBufferInfo = &bufferInfo;

        device.getLogicalDevice().updateDescriptorSets(static_cast<uint32_t>(writes.size()),
                                                       writes.data(), 0, nullptr);
    }

    void PluginTextureManager::destroyTextureEntry(TextureEntry& entry)
    {
        vk::Device vkDevice = device.getLogicalDevice();
        if (entry.sampler)
        {
            vkDevice.destroySampler(entry.sampler);
            entry.sampler = nullptr;
        }
        if (entry.view)
        {
            vkDevice.destroyImageView(entry.view);
            entry.view = nullptr;
        }
        if (entry.image)
        {
            vkDevice.destroyImage(entry.image);
            entry.image = nullptr;
        }
        if (entry.imageAllocation)
        {
            device.getMemoryManager().free(entry.imageAllocation);
            entry.imageAllocation = {};
        }
        entry.staging.destroy(vkDevice, device.getMemoryManager());
    }

    void PluginTextureManager::cleanUp()
    {
        std::lock_guard<std::recursive_mutex> lock(stateMutex);

        vk::Device vkDevice = device.getLogicalDevice();

        for (auto& [id, entry] : textures)
        {
            destroyTextureEntry(entry);
        }
        textures.clear();
        boundMaskId = 0;

        if (maskResourcesCreated)
        {
            if (entityMaskPool)
            {
                vkDevice.destroyDescriptorPool(entityMaskPool);
                entityMaskPool = nullptr;
                entityMaskDescriptorSet = nullptr;
            }
            if (entityMaskLayout)
            {
                vkDevice.destroyDescriptorSetLayout(entityMaskLayout);
                entityMaskLayout = nullptr;
            }
            if (dummySampler)
            {
                vkDevice.destroySampler(dummySampler);
                dummySampler = nullptr;
            }
            if (dummyView)
            {
                vkDevice.destroyImageView(dummyView);
                dummyView = nullptr;
            }
            if (dummyImage)
            {
                vkDevice.destroyImage(dummyImage);
                dummyImage = nullptr;
            }
            if (dummyAllocation)
            {
                device.getMemoryManager().free(dummyAllocation);
                dummyAllocation = {};
            }
            if (maskParamsBuffer)
            {
                core::BufferUtilities::destroyBuffer(vkDevice, maskParamsBuffer, maskParamsAllocation,
                                                     device.getMemoryManager());
                maskParamsBuffer = nullptr;
            }
            maskResourcesCreated = false;
        }
    }
}
