#include "TerrainRaycastPipeline.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/BufferUtilities.hpp"
#include "../../../core/PipelineUtilities.hpp"
#include "../../../core/MappedMemoryGuard.hpp"
#include "print/Log.hpp"

#include <cstring>

using render::MappedMemoryGuard;

namespace render::gpudriven
{
    TerrainRaycastPipeline::TerrainRaycastPipeline(core::Device& device)
        : device(device)
    {
    }

    TerrainRaycastPipeline::~TerrainRaycastPipeline()
    {
        cleanup();
    }

    void TerrainRaycastPipeline::init()
    {
        if (initialized)
        {
            return;
        }

        createDescriptorSetLayout();
        createPipelineLayout();
        createComputePipeline();
        createDescriptorPool();
        allocateDescriptorSet();
        createResultBuffers();
        createDepthSampler();

        initialized = true;
    }

    void TerrainRaycastPipeline::cleanup()
    {
        if (!initialized)
        {
            return;
        }

        vk::Device vkDevice = device.getLogicalDevice();

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

        if (depthSampler)
        {
            vkDevice.destroySampler(depthSampler);
            depthSampler = nullptr;
        }

        if (resultBuffer)
        {
            vkDevice.destroyBuffer(resultBuffer);
            resultBuffer = nullptr;
        }

        if (resultMemory)
        {
            vkDevice.freeMemory(resultMemory);
            resultMemory = nullptr;
        }

        if (stagingBuffer)
        {
            vkDevice.destroyBuffer(stagingBuffer);
            stagingBuffer = nullptr;
        }

        if (stagingMemory)
        {
            vkDevice.freeMemory(stagingMemory);
            stagingMemory = nullptr;
        }

        shader.reset();

        initialized = false;
        hasValidCursor = false;
        descriptorsNeedUpdate = true;
        cachedDepthImageView = nullptr;

    }

    void TerrainRaycastPipeline::setCursorUV(const glm::vec2& uv)
    {
        cursorUV = uv;
        hasValidCursor = true;
    }

    void TerrainRaycastPipeline::clearCursor()
    {
        hasValidCursor = false;
    }

    void TerrainRaycastPipeline::updateDepthImageView(vk::ImageView depthImageView)
    {
        if (depthImageView != cachedDepthImageView)
        {
            cachedDepthImageView = depthImageView;
            descriptorsNeedUpdate = true;
        }
    }

    void TerrainRaycastPipeline::createDescriptorSetLayout()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};

        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute;

        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        descriptorSetLayout = vkDevice.createDescriptorSetLayout(layoutInfo);
    }

    void TerrainRaycastPipeline::createPipelineLayout()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(RaycastPushConstants);

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descriptorSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        pipelineLayout = vkDevice.createPipelineLayout(layoutInfo);
    }

    void TerrainRaycastPipeline::createComputePipeline()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        shader = std::make_unique<core::Shader>(device);
        shader->readShader("../../resources/shaders/terrain/terrain_depth_sample.glsl");

        const auto& stages = shader->getShaderStages();
        if (stages.empty())
        {
            vfLogError("TerrainRaycastPipeline: Failed to load shader: {}",
                        shader->getLastCompilationError());
            return;
        }

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stages[0];
        pipelineInfo.layout = pipelineLayout;

        computePipeline = core::PipelineUtilities::createComputePipeline(vkDevice, pipelineInfo);
        if (!computePipeline)
        {
            vfLogError("TerrainRaycastPipeline: Failed to create compute pipeline");
            return;
        }
    }

    void TerrainRaycastPipeline::createDescriptorPool()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = vk::DescriptorType::eCombinedImageSampler;
        poolSizes[0].descriptorCount = 1;
        poolSizes[1].type = vk::DescriptorType::eStorageBuffer;
        poolSizes[1].descriptorCount = 1;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 1;

        descriptorPool = vkDevice.createDescriptorPool(poolInfo);
    }

    void TerrainRaycastPipeline::allocateDescriptorSet()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        auto result = vkDevice.allocateDescriptorSets(allocInfo);
        descriptorSet = result[0];
    }

    void TerrainRaycastPipeline::createResultBuffers()
    {
        core::BufferInfoRequest resultRequest(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            RESULT_BUFFER_SIZE,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eDeviceLocal
        );
        core::BufferUtilities::createBuffer(resultRequest, resultBuffer, resultMemory);

        core::BufferInfoRequest stagingRequest(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            RESULT_BUFFER_SIZE,
            vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent
        );
        core::BufferUtilities::createBuffer(stagingRequest, stagingBuffer, stagingMemory);
    }

    void TerrainRaycastPipeline::createDepthSampler()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eNearest;
        samplerInfo.minFilter = vk::Filter::eNearest;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.maxLod = 0.0f;

        depthSampler = vkDevice.createSampler(samplerInfo);
    }

    void TerrainRaycastPipeline::writeDescriptors()
    {
        if (!descriptorsNeedUpdate || !cachedDepthImageView)
        {
            return;
        }

        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorImageInfo imageInfo{};
        imageInfo.sampler = depthSampler;
        imageInfo.imageView = cachedDepthImageView;
        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::DescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = resultBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = RESULT_BUFFER_SIZE;

        std::array<vk::WriteDescriptorSet, 2> writes{};

        writes[0].dstSet = descriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        writes[0].pImageInfo = &imageInfo;

        writes[1].dstSet = descriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        writes[1].pBufferInfo = &bufferInfo;

        vkDevice.updateDescriptorSets(writes, {});

        descriptorsNeedUpdate = false;
    }

    void TerrainRaycastPipeline::dispatch(
        vk::CommandBuffer cmd,
        const glm::mat4& invViewProjection,
        uint32_t screenWidth, uint32_t screenHeight)
    {
        if (!initialized || !hasValidCursor || !cachedDepthImageView)
        {
            return;
        }

        writeDescriptors();

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout,
                               0, descriptorSet, {});

        RaycastPushConstants constants{};
        constants.invViewProjection = invViewProjection;
        constants.cursorUV = cursorUV;
        constants.texelSize = glm::vec2(1.0f / static_cast<float>(screenWidth),
                                        1.0f / static_cast<float>(screenHeight));

        cmd.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eCompute,
                          0, sizeof(RaycastPushConstants), &constants);

        cmd.dispatch(1, 1, 1);
    }

    void TerrainRaycastPipeline::copyResultsToStaging(vk::CommandBuffer cmd)
    {
        if (!initialized || !hasValidCursor)
        {
            return;
        }

        vk::BufferMemoryBarrier computeBarrier{};
        computeBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        computeBarrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
        computeBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        computeBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        computeBarrier.buffer = resultBuffer;
        computeBarrier.offset = 0;
        computeBarrier.size = RESULT_BUFFER_SIZE;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eTransfer,
            {}, {}, computeBarrier, {}
        );

        vk::BufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = RESULT_BUFFER_SIZE;
        cmd.copyBuffer(resultBuffer, stagingBuffer, copyRegion);

        vk::BufferMemoryBarrier hostBarrier{};
        hostBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        hostBarrier.dstAccessMask = vk::AccessFlagBits::eHostRead;
        hostBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostBarrier.buffer = stagingBuffer;
        hostBarrier.offset = 0;
        hostBarrier.size = RESULT_BUFFER_SIZE;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eHost,
            {}, {}, hostBarrier, {}
        );
    }

    void TerrainRaycastPipeline::readBackResults()
    {
        if (!initialized || !hasValidCursor)
        {
            lastResult = {};
            return;
        }

        struct GPURaycastResult
        {
            glm::vec4 hitPosition;
            glm::vec4 hitNormal;
        };

        GPURaycastResult gpuResult{};
        {
            MappedMemoryGuard mapped(device.getLogicalDevice(), stagingMemory, 0, RESULT_BUFFER_SIZE);
            std::memcpy(&gpuResult, mapped.data(), sizeof(GPURaycastResult));
        }

        lastResult.hit = gpuResult.hitPosition.w > 0.5f;
        lastResult.position = glm::vec3(gpuResult.hitPosition);
        lastResult.normal = glm::vec3(gpuResult.hitNormal);
    }
}
