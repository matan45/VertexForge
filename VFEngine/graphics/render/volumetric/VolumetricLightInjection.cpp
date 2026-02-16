#include "VolumetricLightInjection.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "print/Logger.hpp"
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
        vk::DescriptorSetLayout shadowTextureDescLayout)
    {
        if (initialized)
        {
            loggerWarning("VolumetricLightInjection: Already initialized");
            return;
        }

        dims = dimensions;
        volumetricGridLayout = volumetricGridDescLayout;
        clusterGridLayout = clusterGridDescLayout;
        lightBufferLayout = lightBufferDescLayout;
        lightCullingLayout = lightCullingDescLayout;
        shadowDataLayout = shadowDataDescLayout;
        shadowTextureLayout = shadowTextureDescLayout;

        createPipelineLayout();
        createComputePipeline();

        initialized = true;
        loggerInfo("VolumetricLightInjection: Initialized ({}x{}x{})", dims.width, dims.height, dims.depth);
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

        initialized = false;
        loggerInfo("VolumetricLightInjection: Cleaned up");
    }

    void VolumetricLightInjection::createPipelineLayout()
    {
        vk::PushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(uint32_t); // frameIndex

        std::array<vk::DescriptorSetLayout, 6> setLayouts = {
            volumetricGridLayout,   // Set 0: Volumetric grid params + scattering volume
            clusterGridLayout,       // Set 1: Cluster grid data
            lightBufferLayout,       // Set 2: Light buffers
            lightCullingLayout,      // Set 3: Light culling output
            shadowDataLayout,        // Set 4: Shadow data buffer
            shadowTextureLayout      // Set 5: Shadow textures (atlas, cascades, cubes)
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
            loggerError("VolumetricLightInjection: Failed to load shader: {}", shader->getLastCompilationError());
            return;
        }

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stages[0];
        pipelineInfo.layout = pipelineLayout;

        auto result = device.getLogicalDevice().createComputePipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess)
        {
            loggerError("VolumetricLightInjection: Failed to create compute pipeline");
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
        uint32_t frameIndex)
    {
        if (!initialized || !computePipeline)
            return;

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline);

        // Bind core sets (0-3) always; bind shadow sets (4-5) only when available
        bool hasShadows = shadowDataDescSet && shadowTextureDescSet;
        if (hasShadows)
        {
            std::array<vk::DescriptorSet, 6> descSets = {
                volumetricGridDescSet,
                clusterGridDescSet,
                lightBufferDescSet,
                lightCullingDescSet,
                shadowDataDescSet,
                shadowTextureDescSet
            };
            cmd.bindDescriptorSets(
                vk::PipelineBindPoint::eCompute, pipelineLayout, 0,
                static_cast<uint32_t>(descSets.size()), descSets.data(),
                0, nullptr);
        }
        else
        {
            std::array<vk::DescriptorSet, 4> descSets = {
                volumetricGridDescSet,
                clusterGridDescSet,
                lightBufferDescSet,
                lightCullingDescSet
            };
            cmd.bindDescriptorSets(
                vk::PipelineBindPoint::eCompute, pipelineLayout, 0,
                static_cast<uint32_t>(descSets.size()), descSets.data(),
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
