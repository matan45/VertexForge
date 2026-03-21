#include "ShadowGPUDataManager.hpp"
#include "VSMPhysicalTilePool.hpp"
#include "ShadowResourcePool.hpp"
#include "../../core/Device.hpp"
#include "../../core/BufferUtilities.hpp"
#include "print/Log.hpp"
#include <cmath>

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

        vk::DeviceSize bufferSize = sizeof(vsm::GPUVSMLight) * ShadowConstants::MAX_TOTAL_SHADOW_VIEWS;

        // Device-local buffer
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

        // Per-frame staging buffers
        for (auto& sf : stagingFrames)
        {
            core::BufferInfoRequest request(
                logicalDevice,
                physicalDevice,
                bufferSize,
                vk::BufferUsageFlagBits::eTransferSrc,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
            );
            core::BufferUtilities::createBuffer(request, sf.buffer, sf.memory);
            sf.mapped = logicalDevice.mapMemory(sf.memory, 0, bufferSize);
        }
    }

    void ShadowGPUDataManager::destroyShadowDataBuffer()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        for (auto& sf : stagingFrames)
        {
            if (sf.mapped) { logicalDevice.unmapMemory(sf.memory); sf.mapped = nullptr; }
            if (sf.buffer) { logicalDevice.destroyBuffer(sf.buffer); sf.buffer = nullptr; }
            if (sf.memory) { logicalDevice.freeMemory(sf.memory); sf.memory = nullptr; }
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

        // Set 9: binding 0 = GPUVSMLight[] SSBO, binding 1 = pageTable[] SSBO
        std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};

        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eCompute;

        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute;

        std::array<vk::DescriptorBindingFlags, 2> sdBindingFlags;
        sdBindingFlags.fill(vk::DescriptorBindingFlagBits::eUpdateAfterBind);
        vk::DescriptorSetLayoutBindingFlagsCreateInfo sdFlagsInfo{};
        sdFlagsInfo.bindingCount = static_cast<uint32_t>(sdBindingFlags.size());
        sdFlagsInfo.pBindingFlags = sdBindingFlags.data();

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        layoutInfo.pNext = &sdFlagsInfo;
        layoutInfo.flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;

        shadowDataLayout = logicalDevice.createDescriptorSetLayout(layoutInfo);

        std::array<vk::DescriptorPoolSize, 1> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[0].descriptorCount = 2;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind;

        shadowDataPool = logicalDevice.createDescriptorPool(poolInfo);

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = shadowDataPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &shadowDataLayout;

        shadowDataDescSet = logicalDevice.allocateDescriptorSets(allocInfo)[0];
    }

    void ShadowGPUDataManager::setPageTableBuffer(vk::Buffer buffer, vk::DeviceSize size)
    {
        pageTableBufferRef = buffer;
        pageTableBufferSize = size;
        updateDescriptorSet();
    }

    void ShadowGPUDataManager::updateDescriptorSet()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        std::vector<vk::WriteDescriptorSet> writes;

        // Binding 0: GPUVSMLight SSBO
        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = shadowDataBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(vsm::GPUVSMLight) * ShadowConstants::MAX_TOTAL_SHADOW_VIEWS;

        vk::WriteDescriptorSet write0{};
        write0.dstSet = shadowDataDescSet;
        write0.dstBinding = 0;
        write0.dstArrayElement = 0;
        write0.descriptorType = vk::DescriptorType::eStorageBuffer;
        write0.descriptorCount = 1;
        write0.pBufferInfo = &bufferInfo;
        writes.push_back(write0);

        // Binding 1: Page table SSBO
        vk::DescriptorBufferInfo pageTableInfo{};
        if (pageTableBufferRef && pageTableBufferSize > 0)
        {
            pageTableInfo.buffer = pageTableBufferRef;
            pageTableInfo.offset = 0;
            pageTableInfo.range = pageTableBufferSize;
        }
        else
        {
            // Use shadow data buffer as dummy until page table is set
            pageTableInfo.buffer = shadowDataBuffer;
            pageTableInfo.offset = 0;
            pageTableInfo.range = sizeof(uint32_t);
        }

        vk::WriteDescriptorSet write1{};
        write1.dstSet = shadowDataDescSet;
        write1.dstBinding = 1;
        write1.dstArrayElement = 0;
        write1.descriptorType = vk::DescriptorType::eStorageBuffer;
        write1.descriptorCount = 1;
        write1.pBufferInfo = &pageTableInfo;
        writes.push_back(write1);

        logicalDevice.updateDescriptorSets(static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    void ShadowGPUDataManager::createShadowTextureDescriptor()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        // Set 10:
        // Binding 0: Physical pool comparison sampler (replaces atlas)
        // Binding 1: Physical pool depth sampler (replaces atlas depth)
        // Binding 2: Cubemap comparison sampler[] (kept for point lights)
        // Binding 3: Cubemap depth sampler[] (kept for point lights)
        std::array<vk::DescriptorSetLayoutBinding, 4> bindings{};

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

        bindings[3].binding = 3;
        bindings[3].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[3].descriptorCount = ShadowConstants::MAX_POINT_SHADOW_CASTERS;
        bindings[3].stageFlags = vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        std::array<vk::DescriptorBindingFlags, 4> bindingFlags{};
        bindingFlags[0] = vk::DescriptorBindingFlagBits::eUpdateAfterBind;
        bindingFlags[1] = vk::DescriptorBindingFlagBits::eUpdateAfterBind;
        bindingFlags[2] = vk::DescriptorBindingFlagBits::ePartiallyBound | vk::DescriptorBindingFlagBits::eUpdateAfterBind;
        bindingFlags[3] = vk::DescriptorBindingFlagBits::ePartiallyBound | vk::DescriptorBindingFlagBits::eUpdateAfterBind;

        vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
        bindingFlagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
        bindingFlagsInfo.pBindingFlags = bindingFlags.data();
        layoutInfo.pNext = &bindingFlagsInfo;
        layoutInfo.flags = vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;

        shadowTextureLayout = logicalDevice.createDescriptorSetLayout(layoutInfo);

        std::array<vk::DescriptorPoolSize, 1> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[0].descriptorCount = 2 + 2 * ShadowConstants::MAX_POINT_SHADOW_CASTERS;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind;

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
        VSMPhysicalTilePool* tilePool,
        ShadowResourcePool* resourcePool,
        const std::unordered_map<uint32_t, LightShadowData>& lightShadowData)
    {
        if (!tilePool || !resourcePool)
            return;

        const auto& logicalDevice = device.getLogicalDevice();
        std::vector<vk::WriteDescriptorSet> writes;

        // Binding 0: Physical pool comparison sampler
        vk::DescriptorImageInfo poolComparisonInfo{};
        poolComparisonInfo.sampler = tilePool->getComparisonSampler();
        poolComparisonInfo.imageView = tilePool->getPoolImageView();
        poolComparisonInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::WriteDescriptorSet poolCompWrite{};
        poolCompWrite.dstSet = shadowTextureDescSet;
        poolCompWrite.dstBinding = 0;
        poolCompWrite.dstArrayElement = 0;
        poolCompWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        poolCompWrite.descriptorCount = 1;
        poolCompWrite.pImageInfo = &poolComparisonInfo;
        writes.push_back(poolCompWrite);

        // Binding 1: Physical pool depth sampler (for PCSS blocker search)
        vk::DescriptorImageInfo poolDepthInfo{};
        poolDepthInfo.sampler = tilePool->getDepthSampler();
        poolDepthInfo.imageView = tilePool->getPoolImageView();
        poolDepthInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::WriteDescriptorSet poolDepthWrite{};
        poolDepthWrite.dstSet = shadowTextureDescSet;
        poolDepthWrite.dstBinding = 1;
        poolDepthWrite.dstArrayElement = 0;
        poolDepthWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        poolDepthWrite.descriptorCount = 1;
        poolDepthWrite.pImageInfo = &poolDepthInfo;
        writes.push_back(poolDepthWrite);

        // Collect cube maps for point lights
        std::vector<vk::DescriptorImageInfo> cubeInfos;
        std::vector<vk::DescriptorImageInfo> cubeDepthInfos;
        cubeInfos.reserve(ShadowConstants::MAX_POINT_SHADOW_CASTERS);
        cubeDepthInfos.reserve(ShadowConstants::MAX_POINT_SHADOW_CASTERS);

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
                break;

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

        // Binding 3: Cube depth samplers
        if (!cubeDepthInfos.empty())
        {
            vk::WriteDescriptorSet cubeDepthWrite{};
            cubeDepthWrite.dstSet = shadowTextureDescSet;
            cubeDepthWrite.dstBinding = 3;
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

    namespace
    {
        enum class ViewType { Directional, Point, Spot };

        void packRangeParams(vsm::GPUVSMLight& gpu, const ShadowView& view,
                             const LightShadowData* ld, ViewType viewType, bool isClipmap)
        {
            if (isClipmap && ld)
            {
                float baseExtent = ld->settings.clipmapBaseExtent;
                float worldExtent = baseExtent * std::pow(2.0f, static_cast<float>(view.cascadeIndex));
                float levelCount = static_cast<float>(ld->settings.clipmapLevelCount);
                gpu.rangeParams = glm::vec4(baseExtent, worldExtent, levelCount,
                                             static_cast<float>(view.cascadeIndex));
            }
            else if (viewType == ViewType::Directional)
            {
                float cascadeCount = ld ? static_cast<float>(ld->settings.cascadeCount) : 4.0f;
                gpu.rangeParams = glm::vec4(view.nearPlane, view.farPlane,
                                             cascadeCount, static_cast<float>(view.cascadeIndex));
            }
            else
            {
                float range = view.farPlane - view.nearPlane;
                float invRange = (range > 0.0001f) ? (1.0f / range) : 0.0f;
                gpu.rangeParams = glm::vec4(view.nearPlane, view.farPlane,
                                             invRange, static_cast<float>(view.cascadeIndex));
            }
        }

        void packPCSSParams(vsm::GPUVSMLight& gpu, const ShadowView& view,
                            ViewType viewType,
                            const std::unordered_map<uint32_t, uint32_t>& entityToCubeIndex)
        {
            float cubeMapIndex = -1.0f;
            if (viewType == ViewType::Point)
            {
                auto it = entityToCubeIndex.find(view.entityId);
                if (it != entityToCubeIndex.end())
                    cubeMapIndex = static_cast<float>(it->second);
            }
            float searchRadius = view.lightSize * view.texelSize * 20.0f;
            gpu.pcssParams = glm::vec4(view.lightSize, searchRadius,
                                        view.filterEnabled ? 1.0f : 0.0f, cubeMapIndex);
        }

        void packPageTableInfo(vsm::GPUVSMLight& gpu, const ShadowView& view,
                               const LightShadowData* ld, ViewType viewType, bool isClipmap)
        {
            int lightType = 0;
            if (isClipmap) lightType = 3;
            else if (viewType == ViewType::Spot) lightType = 1;
            else if (viewType == ViewType::Point) lightType = 2;

            if (!ld || !ld->usesVSM())
            {
                gpu.pageTableInfo = glm::ivec4(0, 0, 0, lightType);
                return;
            }

            uint32_t pagesX = ld->vsmPagesX;
            uint32_t pagesY = ld->vsmPagesY;
            uint32_t ptOffset = ld->vsmPageTableOffset;

            if (ld->type == ShadowMapType::DirectionalCSM)
            {
                uint32_t ppc = ld->vsmPagesX;
                pagesX = ppc;
                pagesY = ppc;
                ptOffset = ld->vsmPageTableOffset + view.cascadeIndex * ppc * ppc;
            }
            else if (ld->type == ShadowMapType::DirectionalClipmap)
            {
                uint32_t levelIdx = view.cascadeIndex;
                if (levelIdx < ld->clipmapLevelPagesPerSide.size())
                {
                    pagesX = ld->clipmapLevelPagesPerSide[levelIdx];
                    pagesY = pagesX;
                    ptOffset = ld->vsmPageTableOffset + ld->clipmapLevelPageOffsets[levelIdx];
                }
            }

            gpu.pageTableInfo = glm::ivec4(
                static_cast<int>(pagesX), static_cast<int>(pagesY),
                static_cast<int>(ptOffset), lightType);
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

        auto addViews = [&](const std::vector<ShadowView>& views, ViewType viewType)
        {
            for (const auto& view : views)
            {
                auto itData = lightShadowData.find(view.entityId);
                const LightShadowData* ld = (itData != lightShadowData.end()) ? &itData->second : nullptr;
                bool isClipmap = ld && ld->type == ShadowMapType::DirectionalClipmap;

                vsm::GPUVSMLight gpu{};
                gpu.viewProjection = view.viewProjectionMatrix;
                gpu.biasParams = glm::vec4(view.depthBias, view.slopeBias, view.normalBias, view.texelSize);

                packRangeParams(gpu, view, ld, viewType, isClipmap);
                packPCSSParams(gpu, view, viewType, entityToCubeIndex);
                packPageTableInfo(gpu, view, ld, viewType, isClipmap);

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

        auto& sf = stagingFrames[currentStagingFrame];

        size_t dataSize = sizeof(vsm::GPUVSMLight) * gpuShadowData.size();
        std::memcpy(sf.mapped, gpuShadowData.data(), dataSize);

        vk::BufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = dataSize;

        cmd.copyBuffer(sf.buffer, shadowDataBuffer, 1, &copyRegion);

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
