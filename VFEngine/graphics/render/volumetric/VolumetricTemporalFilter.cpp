#include "VolumetricTemporalFilter.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "print/Logger.hpp"

namespace render::volumetric
{
    VolumetricTemporalFilter::VolumetricTemporalFilter(core::Device& device)
        : device(device)
    {
    }

    VolumetricTemporalFilter::~VolumetricTemporalFilter()
    {
        cleanup();
    }

    void VolumetricTemporalFilter::init(
        const VolumetricGridDimensions& dimensions,
        vk::DescriptorSetLayout volumetricGridDescLayout)
    {
        if (initialized)
        {
            loggerWarning("VolumetricTemporalFilter: Already initialized");
            return;
        }

        dims = dimensions;
        volumetricGridLayout = volumetricGridDescLayout;

        createPipelineLayout();
        createComputePipeline();

        initialized = true;
        loggerInfo("VolumetricTemporalFilter: Initialized ({}x{}x{})", dims.width, dims.height, dims.depth);
    }

    void VolumetricTemporalFilter::cleanup()
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
        loggerInfo("VolumetricTemporalFilter: Cleaned up");
    }

    void VolumetricTemporalFilter::createPipelineLayout()
    {
        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &volumetricGridLayout;

        pipelineLayout = device.getLogicalDevice().createPipelineLayout(layoutInfo);
    }

    void VolumetricTemporalFilter::createComputePipeline()
    {
        shader = std::make_unique<core::Shader>(device);
        shader->readShader("../../resources/shaders/volumetric/volumetric_temporal.glsl");

        const auto& stages = shader->getShaderStages();
        if (stages.empty())
        {
            loggerError("VolumetricTemporalFilter: Failed to load shader: {}", shader->getLastCompilationError());
            return;
        }

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stages[0];
        pipelineInfo.layout = pipelineLayout;

        auto result = device.getLogicalDevice().createComputePipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess)
        {
            loggerError("VolumetricTemporalFilter: Failed to create compute pipeline");
            return;
        }

        computePipeline = result.value;
    }

    void VolumetricTemporalFilter::dispatch(
        vk::CommandBuffer cmd,
        vk::DescriptorSet volumetricGridDescSet)
    {
        if (!initialized || !computePipeline)
            return;

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline);

        cmd.bindDescriptorSets(
            vk::PipelineBindPoint::eCompute, pipelineLayout, 0,
            1, &volumetricGridDescSet,
            0, nullptr);

        uint32_t groupsX = (dims.width + 7) / 8;
        uint32_t groupsY = (dims.height + 7) / 8;
        uint32_t groupsZ = dims.depth;

        cmd.dispatch(groupsX, groupsY, groupsZ);
    }
}
