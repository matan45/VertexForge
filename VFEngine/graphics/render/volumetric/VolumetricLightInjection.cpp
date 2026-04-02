#include "VolumetricLightInjection.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "../../core/BufferUtilities.hpp"
#include "../../core/VulkanMemoryManager.hpp"
#include "print/Log.hpp"
#include <array>
#include <cstring>

namespace render::volumetric
{
    VolumetricLightInjection::VolumetricLightInjection(core::Device& device)
        : device(device)
    {
    }

    VolumetricLightInjection::~VolumetricLightInjection()
    {
        cleanup();
    }

    void VolumetricLightInjection::init(
        const VolumetricGridDimensions& dimensions,
        vk::DescriptorSetLayout volumetricGridDescLayout,
        vk::DescriptorSetLayout clusterGridDescLayout,
        vk::DescriptorSetLayout lightBufferDescLayout,
        vk::DescriptorSetLayout lightCullingDescLayout,
        vk::DescriptorSetLayout shadowDataDescLayout,
        vk::DescriptorSetLayout shadowTextureDescLayout,
        vk::DescriptorSetLayout fogVolumeDescLayout,
        vk::DescriptorSetLayout giSamplingDescLayout)
    {
        if (initialized)
        {
            vfLogWarning("VolumetricLightInjection: Already initialized");
            return;
        }

        dims = dimensions;
        volumetricGridLayout = volumetricGridDescLayout;
        clusterGridLayout = clusterGridDescLayout;
        lightBufferLayout = lightBufferDescLayout;
        lightCullingLayout = lightCullingDescLayout;
        shadowDataLayout = shadowDataDescLayout;
        shadowTextureLayout = shadowTextureDescLayout;
        fogVolumeLayout = fogVolumeDescLayout;

        // If GI is not available, create a dummy layout with matching bindings
        if (giSamplingDescLayout)
        {
            giSamplingLayout = giSamplingDescLayout;
        }
        else
        {
            auto& dev = device.getLogicalDevice();

            // Create dummy layout matching GI sampling bindings
            std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
            bindings[0] = {0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute};
            bindings[1] = {1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute};
            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();
            ownedGiDummyLayout = dev.createDescriptorSetLayout(layoutInfo);
            giSamplingLayout = ownedGiDummyLayout;

            // Create descriptor pool for 1 set with 2 storage buffer descriptors
            vk::DescriptorPoolSize poolSize{vk::DescriptorType::eStorageBuffer, 2};
            vk::DescriptorPoolCreateInfo poolInfo{};
            poolInfo.maxSets = 1;
            poolInfo.poolSizeCount = 1;
            poolInfo.pPoolSizes = &poolSize;
            ownedGiDummyPool = dev.createDescriptorPool(poolInfo);

            // Allocate dummy descriptor set
            vk::DescriptorSetAllocateInfo allocInfo{};
            allocInfo.descriptorPool = ownedGiDummyPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &ownedGiDummyLayout;
            giDummyDescSet = dev.allocateDescriptorSets(allocInfo)[0];

            // Create small zeroed buffer for both bindings (giCascadeCount will be 0)
            constexpr vk::DeviceSize dummyBufferSize = 256;
            giDummyAllocation = std::make_unique<core::VulkanAllocation>();
            core::BufferInfoRequest bufReq(dev, device.getPhysicalDevice());
            bufReq.size = dummyBufferSize;
            bufReq.usage = vk::BufferUsageFlagBits::eStorageBuffer;
            bufReq.properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
            core::BufferUtilities::createBuffer(bufReq, giDummyBuffer, *giDummyAllocation, device.getMemoryManager());

            // Zero out the buffer (ensures giCascadeCount == 0 in shader)
            if (giDummyAllocation->mappedPtr)
            {
                std::memset(giDummyAllocation->mappedPtr, 0, dummyBufferSize);
            }

            // Write both bindings to point at the zeroed buffer
            std::array<vk::DescriptorBufferInfo, 2> bufInfos = {
                vk::DescriptorBufferInfo{giDummyBuffer, 0, dummyBufferSize},
                vk::DescriptorBufferInfo{giDummyBuffer, 0, dummyBufferSize}
            };

            std::array<vk::WriteDescriptorSet, 2> writes{};
            for (uint32_t i = 0; i < 2; ++i)
            {
                writes[i].dstSet = giDummyDescSet;
                writes[i].dstBinding = i;
                writes[i].dstArrayElement = 0;
                writes[i].descriptorCount = 1;
                writes[i].descriptorType = vk::DescriptorType::eStorageBuffer;
                writes[i].pBufferInfo = &bufInfos[i];
            }
            dev.updateDescriptorSets(writes, {});
        }

        createPipelineLayout();
        createComputePipeline();

        initialized = true;
        vfLogInfo("VolumetricLightInjection: Initialized ({}x{}x{})", dims.width, dims.height, dims.depth);
    }

    void VolumetricLightInjection::cleanup()
    {
        if (!initialized)
            return;

        auto& dev = device.getLogicalDevice();
        dev.waitIdle();

        if (computePipeline)
        {
            dev.destroyPipeline(computePipeline);
            computePipeline = nullptr;
        }

        if (pipelineLayout)
        {
            dev.destroyPipelineLayout(pipelineLayout);
            pipelineLayout = nullptr;
        }

        if (shader)
        {
            shader->cleanUp();
            shader.reset();
        }

        if (giDummyBuffer)
        {
            core::BufferUtilities::destroyBuffer(dev, giDummyBuffer, *giDummyAllocation, device.getMemoryManager());
            giDummyAllocation.reset();
        }

        if (ownedGiDummyPool)
        {
            dev.destroyDescriptorPool(ownedGiDummyPool);
            ownedGiDummyPool = nullptr;
            giDummyDescSet = nullptr;
        }

        if (ownedGiDummyLayout)
        {
            dev.destroyDescriptorSetLayout(ownedGiDummyLayout);
            ownedGiDummyLayout = nullptr;
        }

        initialized = false;
    }

    void VolumetricLightInjection::createPipelineLayout()
    {
        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(uint32_t);

        std::array<vk::DescriptorSetLayout, 8> setLayouts = {
            volumetricGridLayout,   // Set 0: Volumetric grid params + scattering volume
            clusterGridLayout,       // Set 1: Cluster grid data
            lightBufferLayout,       // Set 2: Light buffers
            lightCullingLayout,      // Set 3: Light culling output
            shadowDataLayout,        // Set 4: Shadow data buffer
            shadowTextureLayout,     // Set 5: Shadow textures (atlas, cascades, cubes)
            fogVolumeLayout,         // Set 6: Fog volume SSBO
            giSamplingLayout         // Set 7: GI probe data (compute-compatible)
        };

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutInfo.pSetLayouts = setLayouts.data();
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        pipelineLayout = device.getLogicalDevice().createPipelineLayout(layoutInfo);
    }

    void VolumetricLightInjection::createComputePipeline()
    {
        shader = std::make_unique<core::Shader>(device);
        shader->readShader("../../resources/shaders/volumetric/volumetric_inject.glsl");

        const auto& stages = shader->getShaderStages();
        if (stages.empty())
        {
            vfLogError("VolumetricLightInjection: Failed to load shader: {}", shader->getLastCompilationError());
            return;
        }

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stages[0];
        pipelineInfo.layout = pipelineLayout;

        computePipeline = core::PipelineUtilities::createComputePipeline(device.getLogicalDevice(), pipelineInfo);
        if (!computePipeline)
        {
            vfLogError("VolumetricLightInjection: Failed to create compute pipeline");
            return;
        }
    }

    void VolumetricLightInjection::dispatch(
        vk::CommandBuffer cmd,
        vk::DescriptorSet volumetricGridDescSet,
        vk::DescriptorSet clusterGridDescSet,
        vk::DescriptorSet lightBufferDescSet,
        vk::DescriptorSet lightCullingDescSet,
        vk::DescriptorSet shadowDataDescSet,
        vk::DescriptorSet shadowTextureDescSet,
        vk::DescriptorSet fogVolumeDescSet,
        vk::DescriptorSet giSamplingDescSet,
        uint32_t frameIndex)
    {
        if (!initialized || !computePipeline)
            return;

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline);

        // Bind sets 0-3 always (core sets)
        std::array<vk::DescriptorSet, 4> coreSets = {
            volumetricGridDescSet,
            clusterGridDescSet,
            lightBufferDescSet,
            lightCullingDescSet
        };
        cmd.bindDescriptorSets(
            vk::PipelineBindPoint::eCompute, pipelineLayout, 0,
            static_cast<uint32_t>(coreSets.size()), coreSets.data(),
            0, nullptr);

        // Bind shadow sets 4-5 only when available
        if (shadowDataDescSet && shadowTextureDescSet)
        {
            std::array<vk::DescriptorSet, 2> shadowSets = {shadowDataDescSet, shadowTextureDescSet};
            cmd.bindDescriptorSets(
                vk::PipelineBindPoint::eCompute, pipelineLayout, 4,
                static_cast<uint32_t>(shadowSets.size()), shadowSets.data(),
                0, nullptr);
        }

        // Bind fog volume (set 6)
        cmd.bindDescriptorSets(
            vk::PipelineBindPoint::eCompute, pipelineLayout, 6,
            1, &fogVolumeDescSet,
            0, nullptr);

        // Bind GI (set 7) - use dummy set with zeroed buffers when GI is not active
        vk::DescriptorSet giSet = giSamplingDescSet ? giSamplingDescSet : giDummyDescSet;
        if (giSet)
        {
            cmd.bindDescriptorSets(
                vk::PipelineBindPoint::eCompute, pipelineLayout, 7,
                1, &giSet,
                0, nullptr);
        }

        cmd.pushConstants(
            pipelineLayout,
            vk::ShaderStageFlagBits::eCompute,
            0, sizeof(uint32_t), &frameIndex);

        // Dispatch: one workgroup per 8x8 froxels in XY, one invocation per Z slice
        uint32_t groupsX = (dims.width + 7) / 8;
        uint32_t groupsY = (dims.height + 7) / 8;
        uint32_t groupsZ = dims.depth;

        cmd.dispatch(groupsX, groupsY, groupsZ);
    }
}
