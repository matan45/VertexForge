#include "AnimationComputePipeline.hpp"
#include "../core/Device.hpp"
#include "../core/Shader.hpp"
#include "../core/BufferUtilities.hpp"
#include "../core/PipelineUtilities.hpp"
#include "print/Log.hpp"

namespace animation
{
    AnimationComputePipeline::AnimationComputePipeline(core::Device& device)
        : device(device)
    {
    }

    AnimationComputePipeline::~AnimationComputePipeline()
    {
        cleanup();
    }

    void AnimationComputePipeline::init(uint32_t maxAnimatedEntities)
    {
        if (initialized)
            return;

        maxEntities = maxAnimatedEntities;

        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayoutBinding, 8> bindings{};
        for (uint32_t i = 0; i < 8; ++i)
        {
            bindings[i].binding = i;
            bindings[i].descriptorType = vk::DescriptorType::eStorageBuffer;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = vk::ShaderStageFlagBits::eCompute;
        }

        descriptorSetLayout = core::PipelineUtilities::createUpdateAfterBindLayout(vkDevice, bindings.data(), static_cast<uint32_t>(bindings.size()));

        vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
        pipelineLayout = vkDevice.createPipelineLayout(pipelineLayoutInfo);

        shader = std::make_unique<core::Shader>(device);
        shader->readShader("../../resources/shaders/animation/bone_evaluate.glsl");

        const auto& stages = shader->getShaderStages();
        if (stages.empty())
        {
            vfLogError("AnimationComputePipeline: Failed to load compute shader");
            return;
        }
        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stages[0];
        pipelineInfo.layout = pipelineLayout;
        computePipeline = core::PipelineUtilities::createComputePipeline(vkDevice, pipelineInfo);
        if (!computePipeline)
        {
            vfLogError("AnimationComputePipeline: Failed to create compute pipeline");
            return;
        }

        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eStorageBuffer;
        poolSize.descriptorCount = 8;

        descriptorPool = core::PipelineUtilities::createUpdateAfterBindPool(vkDevice, 1, &poolSize, 1);

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;
        descriptorSet = vkDevice.allocateDescriptorSets(allocInfo)[0];

        {
            const auto& logicalDevice = device.getLogicalDevice();
            const auto& physicalDevice = device.getPhysicalDevice();

            core::BufferInfoRequest bufReq(logicalDevice, physicalDevice);
            bufReq.size = maxEntities * sizeof(GPUAnimEvalRequest);
            bufReq.usage = vk::BufferUsageFlagBits::eStorageBuffer;
            bufReq.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                vk::MemoryPropertyFlagBits::eHostCoherent;
            core::BufferUtilities::createBuffer(bufReq, requestBuffer, requestBufferMemory);
            requestMapped = logicalDevice.mapMemory(requestBufferMemory, 0, bufReq.size, vk::MemoryMapFlags{});
        }

        initialized = true;
        vfLogInfo("AnimationComputePipeline: Initialized for {} max entities", maxEntities);
    }

    void AnimationComputePipeline::cleanup()
    {
        if (!initialized)
            return;

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        auto destroyBuf = [&](vk::Buffer& buf, vk::DeviceMemory& mem) {
            core::BufferUtilities::destroyBuffer(vkDevice, buf, mem);
        };

        if (requestMapped)
        {
            vkDevice.unmapMemory(requestBufferMemory);
            requestMapped = nullptr;
        }

        destroyBuf(requestBuffer, requestBufferMemory);
        destroyBuf(skeletonBuffer, skeletonBufferMemory);
        destroyBuf(clipHeaderBuffer, clipHeaderBufferMemory);
        destroyBuf(channelHeaderBuffer, channelHeaderBufferMemory);
        destroyBuf(positionKeyBuffer, positionKeyBufferMemory);
        destroyBuf(rotationKeyBuffer, rotationKeyBufferMemory);
        destroyBuf(scaleKeyBuffer, scaleKeyBufferMemory);

        if (computePipeline)
        {
            vkDevice.destroyPipeline(computePipeline);
            computePipeline = nullptr;
        }
        if (pipelineLayout)
        {
            vkDevice.destroyPipelineLayout(pipelineLayout);
            pipelineLayout = nullptr;
        }
        if (descriptorPool)
        {
            vkDevice.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }
        if (descriptorSetLayout)
        {
            vkDevice.destroyDescriptorSetLayout(descriptorSetLayout);
            descriptorSetLayout = nullptr;
        }

        shader.reset();
        initialized = false;
    }

    void AnimationComputePipeline::uploadAnimationData(const AnimationGPUUploadData& data)
    {
        if (!initialized)
            return;

        vk::Device vkDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();

        auto createAndUpload = [&](auto& buffer, auto& memory, const auto& vec, vk::DeviceSize elemSize) {
            if (vec.empty())
                return;

            core::BufferUtilities::destroyBuffer(vkDevice, buffer, memory);

            vk::DeviceSize size = vec.size() * elemSize;
            core::BufferInfoRequest bufReq(vkDevice, physicalDevice);
            bufReq.size = size;
            bufReq.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
            bufReq.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(bufReq, buffer, memory);

            vk::Buffer staging;
            vk::DeviceMemory stagingMem;
            core::BufferInfoRequest stagingReq(vkDevice, physicalDevice);
            stagingReq.size = size;
            stagingReq.usage = vk::BufferUsageFlagBits::eTransferSrc;
            stagingReq.properties = vk::MemoryPropertyFlagBits::eHostVisible |
                vk::MemoryPropertyFlagBits::eHostCoherent;
            core::BufferUtilities::createBuffer(stagingReq, staging, stagingMem);

            void* mapped = vkDevice.mapMemory(stagingMem, 0, size, vk::MemoryMapFlags{});
            std::memcpy(mapped, vec.data(), size);
            vkDevice.unmapMemory(stagingMem);

            vk::CommandPool cmdPool = device.getStagingCommandPool();
            vk::CommandBufferAllocateInfo cmdAllocInfo{};
            cmdAllocInfo.level = vk::CommandBufferLevel::ePrimary;
            cmdAllocInfo.commandPool = cmdPool;
            cmdAllocInfo.commandBufferCount = 1;
            vk::CommandBuffer cmd = vkDevice.allocateCommandBuffers(cmdAllocInfo)[0];

            vk::CommandBufferBeginInfo beginInfo{};
            beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
            cmd.begin(beginInfo);
            vk::BufferCopy copyRegion{0, 0, size};
            cmd.copyBuffer(staging, buffer, copyRegion);
            cmd.end();

            vk::SubmitInfo submitInfo{};
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &cmd;
            device.submitGraphics(submitInfo);
            device.waitGraphicsIdle();

            vkDevice.freeCommandBuffers(cmdPool, 1, &cmd);
            core::BufferUtilities::destroyBuffer(vkDevice, staging, stagingMem);
        };

        createAndUpload(skeletonBuffer, skeletonBufferMemory, data.skeletonBones, sizeof(GPUBoneInfo));
        createAndUpload(clipHeaderBuffer, clipHeaderBufferMemory, data.clipHeaders, sizeof(GPUAnimClipHeader));
        createAndUpload(channelHeaderBuffer, channelHeaderBufferMemory, data.channelHeaders, sizeof(GPUChannelHeader));
        createAndUpload(positionKeyBuffer, positionKeyBufferMemory, data.positionKeys, sizeof(GPUPositionKey));
        createAndUpload(rotationKeyBuffer, rotationKeyBufferMemory, data.rotationKeys, sizeof(GPURotationKey));
        createAndUpload(scaleKeyBuffer, scaleKeyBufferMemory, data.scaleKeys, sizeof(GPUScaleKey));

        descriptorsNeedUpdate = true;
    }

    void AnimationComputePipeline::updateRequests(const std::vector<GPUAnimEvalRequest>& requests)
    {
        if (!initialized || !requestMapped || requests.empty())
            return;

        size_t copySize = std::min(requests.size(), static_cast<size_t>(maxEntities)) * sizeof(GPUAnimEvalRequest);
        std::memcpy(requestMapped, requests.data(), copySize);
    }

    void AnimationComputePipeline::dispatch(vk::CommandBuffer cmd, uint32_t entityCount, vk::Buffer outputBoneBuffer)
    {
        if (!initialized || entityCount == 0)
            return;

        if (!skeletonBuffer || !clipHeaderBuffer || !channelHeaderBuffer ||
            !positionKeyBuffer || !rotationKeyBuffer || !scaleKeyBuffer)
        {
            vfLogError("AnimationComputePipeline: dispatch() called without uploadAnimationData()");
            return;
        }

        if (descriptorsNeedUpdate)
        {
            vk::Device vkDevice = device.getLogicalDevice();

            std::array<vk::DescriptorBufferInfo, 8> bufferInfos{};
            bufferInfos[0] = vk::DescriptorBufferInfo(requestBuffer, 0, VK_WHOLE_SIZE);
            bufferInfos[1] = vk::DescriptorBufferInfo(skeletonBuffer, 0, VK_WHOLE_SIZE);
            bufferInfos[2] = vk::DescriptorBufferInfo(clipHeaderBuffer, 0, VK_WHOLE_SIZE);
            bufferInfos[3] = vk::DescriptorBufferInfo(channelHeaderBuffer, 0, VK_WHOLE_SIZE);
            bufferInfos[4] = vk::DescriptorBufferInfo(positionKeyBuffer, 0, VK_WHOLE_SIZE);
            bufferInfos[5] = vk::DescriptorBufferInfo(rotationKeyBuffer, 0, VK_WHOLE_SIZE);
            bufferInfos[6] = vk::DescriptorBufferInfo(scaleKeyBuffer, 0, VK_WHOLE_SIZE);
            bufferInfos[7] = vk::DescriptorBufferInfo(outputBoneBuffer, 0, VK_WHOLE_SIZE);

            std::array<vk::WriteDescriptorSet, 8> writes{};
            for (uint32_t i = 0; i < 8; ++i)
            {
                writes[i].dstSet = descriptorSet;
                writes[i].dstBinding = i;
                writes[i].dstArrayElement = 0;
                writes[i].descriptorType = vk::DescriptorType::eStorageBuffer;
                writes[i].descriptorCount = 1;
                writes[i].pBufferInfo = &bufferInfos[i];
            }

            vkDevice.updateDescriptorSets(writes, {});
            descriptorsNeedUpdate = false;
        }

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 0, descriptorSet, {});

        cmd.dispatch(entityCount, 1, 1);
    }

    void AnimationComputePipeline::insertBarriersAfterDispatch(vk::CommandBuffer cmd, vk::Buffer outputBoneBuffer)
    {
        vk::BufferMemoryBarrier barrier{};
        barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = outputBoneBuffer;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eMeshShaderEXT | vk::PipelineStageFlagBits::eVertexShader,
            {},
            {},
            barrier,
            {}
        );
    }
}
