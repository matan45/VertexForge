#include "ShadowGPUDataManager.hpp"
#include "ShadowAtlasManager.hpp"
#include "ShadowResourcePool.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "print/Log.hpp"

namespace render::shadow
{
    ShadowGPUDataManager::ShadowGPUDataManager(core::Device& device)
        : device(device)
    {
    }

    ShadowGPUDataManager::~ShadowGPUDataManager()
    {
        cleanup();
    }

    void ShadowGPUDataManager::init()
    {
        if (initialized)
            return;

        createShadowDataBuffer();
        createDescriptorResources();
        updateDescriptorSet();
        createShadowTextureDescriptor();

        gpuShadowData.reserve(ShadowConstants::MAX_TOTAL_SHADOW_VIEWS);
        initialized = true;
    }

    void ShadowGPUDataManager::cleanup()
    {
        if (!initialized)
            return;

        const auto& logicalDevice = device.getLogicalDevice();

        destroyShadowTextureDescriptor();

        if (shadowDataPool)
        {
            logicalDevice.destroyDescriptorPool(shadowDataPool);
            shadowDataPool = nullptr;
        }
        if (shadowDataLayout)
        {
            logicalDevice.destroyDescriptorSetLayout(shadowDataLayout);
            shadowDataLayout = nullptr;
        }

        destroyShadowDataBuffer();
        gpuShadowData.clear();

        initialized = false;
    }

    void ShadowGPUDataManager::createShadowDataBuffer()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();

        vk::DeviceSize bufferSize = sizeof(GPUShadowData) * ShadowConstants::MAX_TOTAL_SHADOW_VIEWS;

        // Create device-local buffer
        {
            core::BufferInfoRequest request(
                logicalDevice,
                physicalDevice,
                bufferSize,
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
                vk::MemoryPropertyFlagBits::eDeviceLocal
            );
            core::BufferUtilities::createBuffer(request, shadowDataBuffer, shadowDataMemory);
        }

        // Create staging buffer (host-visible)
        {
            core::BufferInfoRequest request(
                logicalDevice,
                physicalDevice,
                bufferSize,
                vk::BufferUsageFlagBits::eTransferSrc,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
            );
            core::BufferUtilities::createBuffer(request, shadowDataStagingBuffer, shadowDataStagingMemory);
        }

        shadowDataMapped = logicalDevice.mapMemory(shadowDataStagingMemory, 0, bufferSize);
    }

    void ShadowGPUDataManager::destroyShadowDataBuffer()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        if (shadowDataMapped)
        {
            logicalDevice.unmapMemory(shadowDataStagingMemory);
            shadowDataMapped = nullptr;
        }

        if (shadowDataStagingBuffer)
        {
            logicalDevice.destroyBuffer(shadowDataStagingBuffer);
            shadowDataStagingBuffer = nullptr;
        }
        if (shadowDataStagingMemory)
        {
            logicalDevice.freeMemory(shadowDataStagingMemory);
            shadowDataStagingMemory = nullptr;
        }

        if (shadowDataBuffer)
        {
            logicalDevice.destroyBuffer(shadowDataBuffer);
            shadowDataBuffer = nullptr;
        }
        if (shadowDataMemory)
        {
            logicalDevice.freeMemory(shadowDataMemory);
            shadowDataMemory = nullptr;
        }
    }

    void ShadowGPUDataManager::createDescriptorResources()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        vk::DescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = vk::DescriptorType::eStorageBuffer;
        binding.descriptorCount = 1;
        binding.stageFlags = vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;

        shadowDataLayout = logicalDevice.createDescriptorSetLayout(layoutInfo);

        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eStorageBuffer;
        poolSize.descriptorCount = 1;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;

        shadowDataPool = logicalDevice.createDescriptorPool(poolInfo);

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = shadowDataPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &shadowDataLayout;

        shadowDataDescSet = logicalDevice.allocateDescriptorSets(allocInfo)[0];
    }

    void ShadowGPUDataManager::updateDescriptorSet()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = shadowDataBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(GPUShadowData) * ShadowConstants::MAX_TOTAL_SHADOW_VIEWS;

        vk::WriteDescriptorSet write{};
        write.dstSet = shadowDataDescSet;
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorType = vk::DescriptorType::eStorageBuffer;
        write.descriptorCount = 1;
        write.pBufferInfo = &bufferInfo;

        logicalDevice.updateDescriptorSets(1, &write, 0, nullptr);
    }

    void ShadowGPUDataManager::createShadowTextureDescriptor()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        // Bindings 0-2: comparison samplers (for final shadow filtering)
        // Bindings 3-5: depth samplers (for PCSS blocker search)
        std::array<vk::DescriptorSetLayoutBinding, 6> bindings{};

        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute;

        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute;

        bindings[2].binding = 2;
        bindings[2].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[2].descriptorCount = ShadowConstants::MAX_POINT_SHADOW_CASTERS;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute;

        // PCSS blocker search depth samplers (same images, non-comparison samplers)
        bindings[3].binding = 3;
        bindings[3].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute;

        bindings[4].binding = 4;
        bindings[4].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[4].descriptorCount = 1;
        bindings[4].stageFlags = vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute;

        bindings[5].binding = 5;
        bindings[5].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[5].descriptorCount = ShadowConstants::MAX_POINT_SHADOW_CASTERS;
        bindings[5].stageFlags = vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        std::array<vk::DescriptorBindingFlags, 6> bindingFlags{};
        bindingFlags[0] = {};
        bindingFlags[1] = {};
        bindingFlags[2] = vk::DescriptorBindingFlagBits::ePartiallyBound;
        bindingFlags[3] = {};
        bindingFlags[4] = {};
        bindingFlags[5] = vk::DescriptorBindingFlagBits::ePartiallyBound;

        vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
        bindingFlagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
        bindingFlagsInfo.pBindingFlags = bindingFlags.data();
        layoutInfo.pNext = &bindingFlagsInfo;

        shadowTextureLayout = logicalDevice.createDescriptorSetLayout(layoutInfo);

        std::array<vk::DescriptorPoolSize, 1> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[0].descriptorCount = 2 * (2 + ShadowConstants::MAX_POINT_SHADOW_CASTERS);

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();

        shadowTexturePool = logicalDevice.createDescriptorPool(poolInfo);

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = shadowTexturePool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &shadowTextureLayout;

        shadowTextureDescSet = logicalDevice.allocateDescriptorSets(allocInfo)[0];
    }

    void ShadowGPUDataManager::destroyShadowTextureDescriptor()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        if (shadowTexturePool)
        {
            logicalDevice.destroyDescriptorPool(shadowTexturePool);
            shadowTexturePool = nullptr;
        }
        if (shadowTextureLayout)
        {
            logicalDevice.destroyDescriptorSetLayout(shadowTextureLayout);
            shadowTextureLayout = nullptr;
        }
    }

    void ShadowGPUDataManager::updateShadowTextureDescriptor(
        ShadowAtlasManager* atlasManager,
        ShadowResourcePool* resourcePool,
        const std::unordered_map<uint32_t, LightShadowData>& lightShadowData)
    {
        if (!atlasManager || !resourcePool)
            return;

        const auto& logicalDevice = device.getLogicalDevice();
        std::vector<vk::WriteDescriptorSet> writes;

        // Comparison samplers (bindings 0-2)
        std::vector<vk::DescriptorImageInfo> atlasInfos(1);
        std::vector<vk::DescriptorImageInfo> csmInfos(1);
        std::vector<vk::DescriptorImageInfo> cubeInfos;
        cubeInfos.reserve(ShadowConstants::MAX_POINT_SHADOW_CASTERS);

        // Depth samplers for PCSS blocker search (bindings 3-5)
        std::vector<vk::DescriptorImageInfo> atlasDepthInfos(1);
        std::vector<vk::DescriptorImageInfo> csmDepthInfos(1);
        std::vector<vk::DescriptorImageInfo> cubeDepthInfos;
        cubeDepthInfos.reserve(ShadowConstants::MAX_POINT_SHADOW_CASTERS);

        // Binding 0: Atlas comparison sampler
        atlasInfos[0].sampler = atlasManager->getComparisonSampler();
        atlasInfos[0].imageView = atlasManager->getAtlasImageView();
        atlasInfos[0].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::WriteDescriptorSet atlasWrite{};
        atlasWrite.dstSet = shadowTextureDescSet;
        atlasWrite.dstBinding = 0;
        atlasWrite.dstArrayElement = 0;
        atlasWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        atlasWrite.descriptorCount = 1;
        atlasWrite.pImageInfo = atlasInfos.data();
        writes.push_back(atlasWrite);

        // Binding 3: Atlas depth sampler (same image, non-comparison)
        atlasDepthInfos[0].sampler = atlasManager->getDepthSampler();
        atlasDepthInfos[0].imageView = atlasManager->getAtlasImageView();
        atlasDepthInfos[0].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::WriteDescriptorSet atlasDepthWrite{};
        atlasDepthWrite.dstSet = shadowTextureDescSet;
        atlasDepthWrite.dstBinding = 3;
        atlasDepthWrite.dstArrayElement = 0;
        atlasDepthWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        atlasDepthWrite.descriptorCount = 1;
        atlasDepthWrite.pImageInfo = atlasDepthInfos.data();
        writes.push_back(atlasDepthWrite);

        // Find CSM array view
        vk::ImageView csmArrayView = resourcePool->getPlaceholderArrayView();
        for (const auto& [entityId, data] : lightShadowData)
        {
            if (data.type == ShadowMapType::DirectionalCSM &&
                data.settings.enabled && data.settings.castShadows &&
                data.resourceHandle.isValid())
            {
                ShadowDepthArray* array = resourcePool->getArray(data.resourceHandle);
                if (array && array->isInitialized())
                {
                    csmArrayView = array->getArrayView();
                    break;
                }
            }
        }

        // Binding 1: CSM comparison sampler
        csmInfos[0].sampler = resourcePool->getComparisonSampler();
        csmInfos[0].imageView = csmArrayView;
        csmInfos[0].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::WriteDescriptorSet csmWrite{};
        csmWrite.dstSet = shadowTextureDescSet;
        csmWrite.dstBinding = 1;
        csmWrite.dstArrayElement = 0;
        csmWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        csmWrite.descriptorCount = 1;
        csmWrite.pImageInfo = csmInfos.data();
        writes.push_back(csmWrite);

        // Binding 4: CSM depth sampler (same image, non-comparison)
        csmDepthInfos[0].sampler = resourcePool->getDepthSampler();
        csmDepthInfos[0].imageView = csmArrayView;
        csmDepthInfos[0].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::WriteDescriptorSet csmDepthWrite{};
        csmDepthWrite.dstSet = shadowTextureDescSet;
        csmDepthWrite.dstBinding = 4;
        csmDepthWrite.dstArrayElement = 0;
        csmDepthWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        csmDepthWrite.descriptorCount = 1;
        csmDepthWrite.pImageInfo = csmDepthInfos.data();
        writes.push_back(csmDepthWrite);

        // Collect cube maps
        uint32_t cubeIndex = 0;
        for (const auto& [entityId, data] : lightShadowData)
        {
            if (data.type != ShadowMapType::PointCube ||
                !data.settings.enabled || !data.settings.castShadows ||
                !data.resourceHandle.isValid())
                continue;

            ShadowCubeMap* cube = resourcePool->getCube(data.resourceHandle);
            if (!cube || !cube->isInitialized())
                continue;

            if (cubeIndex >= ShadowConstants::MAX_POINT_SHADOW_CASTERS)
            {
                vfLogWarning("ShadowGPUDataManager: Exceeded max point shadow casters ({}), skipping light {}",
                              ShadowConstants::MAX_POINT_SHADOW_CASTERS, entityId);
                break;
            }

            vk::DescriptorImageInfo cubeInfo{};
            cubeInfo.sampler = resourcePool->getCubeComparisonSampler();
            cubeInfo.imageView = cube->getCubeView();
            cubeInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            cubeInfos.push_back(cubeInfo);

            vk::DescriptorImageInfo cubeDepthInfo{};
            cubeDepthInfo.sampler = resourcePool->getCubeDepthSampler();
            cubeDepthInfo.imageView = cube->getCubeView();
            cubeDepthInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            cubeDepthInfos.push_back(cubeDepthInfo);

            ++cubeIndex;
        }

        if (cubeInfos.empty())
        {
            vk::ImageView placeholderCubeView = resourcePool->getPlaceholderCubeView();
            if (placeholderCubeView)
            {
                vk::DescriptorImageInfo placeholderInfo{};
                placeholderInfo.sampler = resourcePool->getCubeComparisonSampler();
                placeholderInfo.imageView = placeholderCubeView;
                placeholderInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                cubeInfos.push_back(placeholderInfo);

                vk::DescriptorImageInfo placeholderDepthInfo{};
                placeholderDepthInfo.sampler = resourcePool->getCubeDepthSampler();
                placeholderDepthInfo.imageView = placeholderCubeView;
                placeholderDepthInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                cubeDepthInfos.push_back(placeholderDepthInfo);
            }
        }

        // Binding 2: Cube comparison samplers
        if (!cubeInfos.empty())
        {
            vk::WriteDescriptorSet cubeWrite{};
            cubeWrite.dstSet = shadowTextureDescSet;
            cubeWrite.dstBinding = 2;
            cubeWrite.dstArrayElement = 0;
            cubeWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            cubeWrite.descriptorCount = static_cast<uint32_t>(cubeInfos.size());
            cubeWrite.pImageInfo = cubeInfos.data();
            writes.push_back(cubeWrite);
        }

        // Binding 5: Cube depth samplers
        if (!cubeDepthInfos.empty())
        {
            vk::WriteDescriptorSet cubeDepthWrite{};
            cubeDepthWrite.dstSet = shadowTextureDescSet;
            cubeDepthWrite.dstBinding = 5;
            cubeDepthWrite.dstArrayElement = 0;
            cubeDepthWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            cubeDepthWrite.descriptorCount = static_cast<uint32_t>(cubeDepthInfos.size());
            cubeDepthWrite.pImageInfo = cubeDepthInfos.data();
            writes.push_back(cubeDepthWrite);
        }

        if (!writes.empty())
        {
            logicalDevice.updateDescriptorSets(static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }

    void ShadowGPUDataManager::buildGPUShadowData(
        const std::vector<ShadowView>& directionalViews,
        const std::vector<ShadowView>& pointViews,
        const std::vector<ShadowView>& spotViews,
        const std::unordered_map<uint32_t, LightShadowData>& lightShadowData,
        const std::unordered_map<uint32_t, uint32_t>& entityToCubeIndex)
    {
        gpuShadowData.clear();

        enum class ViewType { Directional, Point, Spot };

        auto addViews = [&](const std::vector<ShadowView>& views, ViewType viewType)
        {
            for (const auto& view : views)
            {
                GPUShadowData gpu{};
                gpu.viewProjection = view.viewProjectionMatrix;
                gpu.atlasViewport = view.atlasViewport;

                gpu.biasParams = glm::vec4(
                    view.depthBias,
                    view.slopeBias,
                    view.normalBias,
                    view.texelSize
                );

                float rangeZ = 0.0f;
                if (viewType == ViewType::Directional)
                {
                    auto it = lightShadowData.find(view.entityId);
                    if (it != lightShadowData.end())
                    {
                        rangeZ = static_cast<float>(it->second.settings.cascadeCount);
                    }
                }
                else
                {
                    float range = view.farPlane - view.nearPlane;
                    rangeZ = (range > 0.0001f) ? (1.0f / range) : 0.0f;
                }

                gpu.rangeParams = glm::vec4(
                    view.nearPlane,
                    view.farPlane,
                    rangeZ,
                    static_cast<float>(view.handle.cascadeIndex)
                );

                float cubeMapIndex = -1.0f;
                if (viewType == ViewType::Point)
                {
                    auto it = entityToCubeIndex.find(view.entityId);
                    if (it != entityToCubeIndex.end())
                    {
                        cubeMapIndex = static_cast<float>(it->second);
                    }
                }

                float searchRadius = view.lightSize * view.texelSize * 20.0f;
                gpu.pcssParams = glm::vec4(
                    view.lightSize,
                    searchRadius,
                    view.filterEnabled ? 1.0f : 0.0f,
                    cubeMapIndex
                );

                gpuShadowData.push_back(gpu);
            }
        };

        addViews(directionalViews, ViewType::Directional);
        addViews(pointViews, ViewType::Point);
        addViews(spotViews, ViewType::Spot);
    }

    void ShadowGPUDataManager::uploadToGPU(vk::CommandBuffer cmd)
    {
        if (!initialized || gpuShadowData.empty())
            return;

        size_t dataSize = sizeof(GPUShadowData) * gpuShadowData.size();
        std::memcpy(shadowDataMapped, gpuShadowData.data(), dataSize);

        vk::BufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = dataSize;

        cmd.copyBuffer(shadowDataStagingBuffer, shadowDataBuffer, 1, &copyRegion);

        vk::BufferMemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = shadowDataBuffer;
        barrier.offset = 0;
        barrier.size = dataSize;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eVertexShader,
            {},
            0, nullptr,
            1, &barrier,
            0, nullptr
        );
    }
}
