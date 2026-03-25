#include "VolumetricLightInjection.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "print/Log.hpp"
#include <array>

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
            std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
            bindings[0] = {0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute};
            bindings[1] = {1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute};
            vk::DescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
            layoutInfo.pBindings = bindings.data();
            ownedGiDummyLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
            giSamplingLayout = ownedGiDummyLayout;
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

        auto result = device.getLogicalDevice().createComputePipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess)
        {
            vfLogError("VolumetricLightInjection: Failed to create compute pipeline");
            return;
        }

        computePipeline = result.value;
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

        // Bind GI (set 7) only when available
        if (giSamplingDescSet)
        {
            cmd.bindDescriptorSets(
                vk::PipelineBindPoint::eCompute, pipelineLayout, 7,
                1, &giSamplingDescSet,
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
