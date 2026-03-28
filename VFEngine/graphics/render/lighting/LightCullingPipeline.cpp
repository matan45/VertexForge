#include "LightCullingPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "print/Log.hpp"
#include <array>

// Windows defines MemoryBarrier as a macro - undefine it to use vk::MemoryBarrier
#ifdef MemoryBarrier
#undef MemoryBarrier
#endif

namespace render::lighting
{
    LightCullingPipeline::LightCullingPipeline(core::Device& device)
        : device(device)
    {
    }

    LightCullingPipeline::~LightCullingPipeline()
    {
        cleanup();
    }

    void LightCullingPipeline::init(
        uint32_t clusterCount,
        vk::DescriptorSetLayout clusterGridDescLayout,
        vk::DescriptorSetLayout lightBufferDescLayout)
    {
        if (initialized)
        {
            vfLogWarning("LightCullingPipeline: Already initialized");
            return;
        }

        if (clusterCount == 0)
        {
            vfLogError("LightCullingPipeline: Cannot initialize with 0 clusters");
            return;
        }

        if (!clusterGridDescLayout || !lightBufferDescLayout)
        {
            vfLogError("LightCullingPipeline: Invalid external descriptor set layouts");
            return;
        }

        totalClusters = clusterCount;
        clusterGridLayout = clusterGridDescLayout;
        lightBufferLayout = lightBufferDescLayout;

        vfLogInfo("LightCullingPipeline: Initializing for {} clusters", totalClusters);

        createBuffers();
        createDescriptorSetLayout();
        createPipelineLayout();
        createComputePipeline();
        createDescriptorPool();
        allocateDescriptorSet();
        writeDescriptors();

        initialized = true;
    }

    void LightCullingPipeline::cleanup()
    {
        if (!initialized)
        {
            return;
        }

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        if (descriptorPool)
        {
            vkDevice.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }

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

        if (descriptorSetLayout)
        {
            vkDevice.destroyDescriptorSetLayout(descriptorSetLayout);
            descriptorSetLayout = nullptr;
        }

        if (shader)
        {
            shader->cleanUp();
            shader.reset();
        }

        destroyBuffers();

        initialized = false;
    }

    void LightCullingPipeline::createBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();
        const auto& physicalDevice = device.getPhysicalDevice();

        // ClusterLightGrid buffer (DEVICE_LOCAL, for shader read/write)
        // One GPUClusterLightData (8 bytes) per cluster
        {
            vk::DeviceSize bufferSize = totalClusters * sizeof(GPUClusterLightData);

            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = bufferSize;
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, clusterLightGridBuffer, clusterLightGridAllocation, device.getMemoryManager());
        }

        // ClusterLightIndexList buffer (DEVICE_LOCAL)
        // Fixed allocation: each cluster gets MAX_LIGHTS_PER_CLUSTER slots
        // Offset for cluster i = i * MAX_LIGHTS_PER_CLUSTER
        {
            vk::DeviceSize bufferSize = computeLightIndexListSize(totalClusters) * sizeof(uint32_t);

            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = bufferSize;
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, clusterLightIndexListBuffer, clusterLightIndexListAllocation, device.getMemoryManager());

        }

        // Globals buffer (DEVICE_LOCAL, for atomic operations)
        {
            vk::DeviceSize bufferSize = sizeof(LightCullingGlobals);

            core::BufferInfoRequest request(logicalDevice, physicalDevice);
            request.size = bufferSize;
            request.usage = vk::BufferUsageFlagBits::eStorageBuffer;
            request.properties = vk::MemoryPropertyFlagBits::eDeviceLocal;
            core::BufferUtilities::createBuffer(request, globalsBuffer, globalsAllocation, device.getMemoryManager());

        }
    }

    void LightCullingPipeline::destroyBuffers()
    {
        const auto& logicalDevice = device.getLogicalDevice();

        core::BufferUtilities::destroyBuffer(logicalDevice, globalsBuffer, globalsAllocation, device.getMemoryManager());
        core::BufferUtilities::destroyBuffer(logicalDevice, clusterLightIndexListBuffer, clusterLightIndexListAllocation, device.getMemoryManager());
        core::BufferUtilities::destroyBuffer(logicalDevice, clusterLightGridBuffer, clusterLightGridAllocation, device.getMemoryManager());
    }

    void LightCullingPipeline::createDescriptorSetLayout()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // This layout is for our output buffers (set 2 in the shader)
        // The shader also uses external descriptor sets:
        //   Set 0: Cluster grid (ClusterGridManager's descriptor set)
        //   Set 1: Light data (GPULightBufferManager's descriptor set)
        //   Set 2: Our output buffers (this layout)

        std::array<vk::DescriptorSetLayoutBinding, 3> bindings{};

        // Binding 0: ClusterLightGrid SSBO (read/write)
        bindings[0].binding = 0;
        bindings[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = vk::ShaderStageFlagBits::eCompute |
                                 vk::ShaderStageFlagBits::eFragment |
                                 vk::ShaderStageFlagBits::eMeshEXT;
        bindings[0].pImmutableSamplers = nullptr;

        // Binding 1: ClusterLightIndexList SSBO (read/write)
        bindings[1].binding = 1;
        bindings[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = vk::ShaderStageFlagBits::eCompute |
                                 vk::ShaderStageFlagBits::eFragment |
                                 vk::ShaderStageFlagBits::eMeshEXT;
        bindings[1].pImmutableSamplers = nullptr;

        // Binding 2: Globals SSBO (atomics)
        bindings[2].binding = 2;
        bindings[2].descriptorType = vk::DescriptorType::eStorageBuffer;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = vk::ShaderStageFlagBits::eCompute;
        bindings[2].pImmutableSamplers = nullptr;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        descriptorSetLayout = vkDevice.createDescriptorSetLayout(layoutInfo);
    }

    void LightCullingPipeline::createPipelineLayout()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Push constant range for LightCullingPushConstants
        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(LightCullingPushConstants);

        // Combined pipeline layout with all 3 descriptor set layouts:
        // Set 0: ClusterGridManager (cluster params UBO + cluster AABBs SSBO)
        // Set 1: GPULightBufferManager (light counts UBO + lights SSBOs)
        // Set 2: Our output buffers (cluster light grid + light index list + globals)
        std::array<vk::DescriptorSetLayout, 3> setLayouts = {
            clusterGridLayout,
            lightBufferLayout,
            descriptorSetLayout
        };

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutInfo.pSetLayouts = setLayouts.data();
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        pipelineLayout = vkDevice.createPipelineLayout(layoutInfo);
    }

    void LightCullingPipeline::createComputePipeline()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        shader = std::make_unique<core::Shader>(device);
        shader->readShader("../../resources/shaders/lighting/light_culling.glsl");

        const auto& stages = shader->getShaderStages();
        if (stages.empty())
        {
            vfLogError("LightCullingPipeline: Failed to load shader: {}", shader->getLastCompilationError());
            return;
        }

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stages[0];
        pipelineInfo.layout = pipelineLayout;

        computePipeline = core::PipelineUtilities::createComputePipeline(vkDevice, pipelineInfo);
        if (!computePipeline)
        {
            vfLogError("LightCullingPipeline: Failed to create compute pipeline");
            return;
        }
    }

    void LightCullingPipeline::createDescriptorPool()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorPoolSize poolSize{};
        poolSize.type = vk::DescriptorType::eStorageBuffer;
        poolSize.descriptorCount = 3;

        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;

        descriptorPool = vkDevice.createDescriptorPool(poolInfo);
    }

    void LightCullingPipeline::allocateDescriptorSet()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &descriptorSetLayout;

        auto sets = vkDevice.allocateDescriptorSets(allocInfo);
        descriptorSet = sets[0];

    }

    void LightCullingPipeline::writeDescriptors()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorBufferInfo, 3> bufferInfos{};

        bufferInfos[0].buffer = clusterLightGridBuffer;
        bufferInfos[0].offset = 0;
        bufferInfos[0].range = VK_WHOLE_SIZE;

        bufferInfos[1].buffer = clusterLightIndexListBuffer;
        bufferInfos[1].offset = 0;
        bufferInfos[1].range = VK_WHOLE_SIZE;

        bufferInfos[2].buffer = globalsBuffer;
        bufferInfos[2].offset = 0;
        bufferInfos[2].range = sizeof(LightCullingGlobals);

        std::array<vk::WriteDescriptorSet, 3> descriptorWrites{};

        descriptorWrites[0].dstSet = descriptorSet;
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = vk::DescriptorType::eStorageBuffer;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfos[0];

        descriptorWrites[1].dstSet = descriptorSet;
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = vk::DescriptorType::eStorageBuffer;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pBufferInfo = &bufferInfos[1];

        descriptorWrites[2].dstSet = descriptorSet;
        descriptorWrites[2].dstBinding = 2;
        descriptorWrites[2].dstArrayElement = 0;
        descriptorWrites[2].descriptorType = vk::DescriptorType::eStorageBuffer;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].pBufferInfo = &bufferInfos[2];

        vkDevice.updateDescriptorSets(static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }

    void LightCullingPipeline::updateExternalDescriptors(
        vk::DescriptorSet clusterGridDescSet,
        vk::DescriptorSet lightBufferDescSet)
    {
        if (!clusterGridDescSet || !lightBufferDescSet)
        {
            vfLogWarning("LightCullingPipeline: Invalid external descriptor sets");
            return;
        }

        cachedClusterGridDescSet = clusterGridDescSet;
        cachedLightBufferDescSet = lightBufferDescSet;
        descriptorsNeedUpdate = false;
    }

    void LightCullingPipeline::dispatch(
        vk::CommandBuffer cmd,
        const glm::mat4& viewMatrix,
        uint32_t pointLightCount,
        uint32_t spotLightCount)
    {
        if (!initialized)
        {
            return;
        }

        if (!cachedClusterGridDescSet || !cachedLightBufferDescSet)
        {
            vfLogWarning("LightCullingPipeline: External descriptor sets not set, skipping dispatch");
            return;
        }

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline);

        std::array<vk::DescriptorSet, 3> descSets = {
            cachedClusterGridDescSet,
            cachedLightBufferDescSet,
            descriptorSet
        };
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 0,
                               static_cast<uint32_t>(descSets.size()), descSets.data(),
                               0, nullptr);

        dispatchReset(cmd);
        insertBarrier(cmd);

        if (pointLightCount > 0 || spotLightCount > 0)
        {
            dispatchLightCulling(cmd, viewMatrix, pointLightCount, spotLightCount);
            insertBarrier(cmd);
        }
    }

    void LightCullingPipeline::dispatchReset(vk::CommandBuffer cmd)
    {
        LightCullingPushConstants pushConstants{};
        pushConstants.totalClusters = totalClusters;
        pushConstants.phase = LightCullingConstants::PHASE_RESET;

        cmd.pushConstants<LightCullingPushConstants>(
            pipelineLayout,
            vk::ShaderStageFlagBits::eCompute,
            0,
            pushConstants);

        uint32_t groupCount = (totalClusters + LightCullingConstants::LIGHT_CULL_WORKGROUP_SIZE - 1)
                            / LightCullingConstants::LIGHT_CULL_WORKGROUP_SIZE;
        cmd.dispatch(groupCount, 1, 1);
    }

    void LightCullingPipeline::dispatchLightCulling(
        vk::CommandBuffer cmd,
        const glm::mat4& viewMatrix,
        uint32_t pointLightCount,
        uint32_t spotLightCount)
    {
        LightCullingPushConstants pushConstants{};
        pushConstants.viewMatrix = viewMatrix;
        pushConstants.pointLightCount = pointLightCount;
        pushConstants.spotLightCount = spotLightCount;
        pushConstants.totalClusters = totalClusters;
        pushConstants.phase = LightCullingConstants::PHASE_CULL_LIGHTS;

        cmd.pushConstants<LightCullingPushConstants>(
            pipelineLayout,
            vk::ShaderStageFlagBits::eCompute,
            0,
            pushConstants);

        uint32_t groupCount = (totalClusters + LightCullingConstants::LIGHT_CULL_WORKGROUP_SIZE - 1)
                            / LightCullingConstants::LIGHT_CULL_WORKGROUP_SIZE;
        cmd.dispatch(groupCount, 1, 1);
    }

    void LightCullingPipeline::insertBarrier(vk::CommandBuffer cmd)
    {
        vk::MemoryBarrier memBarrier{};
        memBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        memBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eComputeShader |
            vk::PipelineStageFlagBits::eFragmentShader |
            vk::PipelineStageFlagBits::eMeshShaderEXT,
            vk::DependencyFlags{},
            memBarrier,
            {},
            {}
        );
    }
}
