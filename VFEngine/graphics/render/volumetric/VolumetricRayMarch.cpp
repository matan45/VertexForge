#include "VolumetricRayMarch.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "print/Log.hpp"

namespace render::volumetric
{
    VolumetricRayMarch::VolumetricRayMarch(core::Device& device)
        : device(device)
    {
    }

    VolumetricRayMarch::~VolumetricRayMarch()
    {
        cleanup();
    }

    void VolumetricRayMarch::init(
        const VolumetricGridDimensions& dimensions,
        vk::DescriptorSetLayout volumetricGridDescLayout)
    {
        if (initialized)
        {
            vfLogWarning("VolumetricRayMarch: Already initialized");
            return;
        }

        dims = dimensions;
        volumetricGridLayout = volumetricGridDescLayout;

        createPipelineLayout();
        createComputePipeline();

        initialized = true;
        vfLogInfo("VolumetricRayMarch: Initialized ({}x{}x{})", dims.width, dims.height, dims.depth);
    }

    void VolumetricRayMarch::cleanup()
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
    }

    void VolumetricRayMarch::createPipelineLayout()
    {
        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &volumetricGridLayout;

        pipelineLayout = device.getLogicalDevice().createPipelineLayout(layoutInfo);
    }

    void VolumetricRayMarch::createComputePipeline()
    {
        shader = std::make_unique<core::Shader>(device);
        shader->readShader("../../resources/shaders/volumetric/volumetric_raymarch.glsl");

        const auto& stages = shader->getShaderStages();
        if (stages.empty())
        {
            vfLogError("VolumetricRayMarch: Failed to load shader: {}", shader->getLastCompilationError());
            return;
        }

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stages[0];
        pipelineInfo.layout = pipelineLayout;

        auto result = device.getLogicalDevice().createComputePipeline(nullptr, pipelineInfo);
        if (result.result != vk::Result::eSuccess)
        {
            vfLogError("VolumetricRayMarch: Failed to create compute pipeline");
            return;
        }

        computePipeline = result.value;
    }

    void VolumetricRayMarch::dispatch(
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

        // 2D dispatch: each thread processes one XY column through all Z slices
        uint32_t groupsX = (dims.width + 7) / 8;
        uint32_t groupsY = (dims.height + 7) / 8;

        cmd.dispatch(groupsX, groupsY, 1);
    }
}
