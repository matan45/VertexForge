#include "ProbeUpdatePipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "../../core/PipelineUtilities.hpp"
#include "print/Log.hpp"

namespace render::gi
{
    ProbeUpdatePipeline::ProbeUpdatePipeline(core::Device& device)
        : device(device)
    {
    }

    ProbeUpdatePipeline::~ProbeUpdatePipeline()
    {
        cleanup();
    }

    void ProbeUpdatePipeline::init(vk::DescriptorSetLayout probeDataLayout,
                                    vk::DescriptorSetLayout cascadeInfoLayout)
    {
        if (initialized)
        {
            return;
        }

        loadShader();
        createPipelineLayout(probeDataLayout, cascadeInfoLayout);
        createComputePipeline();

        initialized = true;
        vfLogInfo("ProbeUpdatePipeline: Initialized");
    }

    void ProbeUpdatePipeline::cleanup()
    {
        if (!initialized)
        {
            return;
        }

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

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

        updateShader.reset();
        initialized = false;
    }

    void ProbeUpdatePipeline::loadShader()
    {
        updateShader = std::make_unique<core::Shader>(device);
        updateShader->readShader("../../resources/shaders/gi/probe_update.glsl");
    }

    void ProbeUpdatePipeline::createPipelineLayout(vk::DescriptorSetLayout probeDataLayout,
                                                     vk::DescriptorSetLayout cascadeInfoLayout)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayout, 2> setLayouts = {probeDataLayout, cascadeInfoLayout};

        vk::PushConstantRange pushRange{};
        pushRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
        pushRange.offset = 0;
        pushRange.size = sizeof(GIComputePushConstants);

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutInfo.pSetLayouts = setLayouts.data();
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        pipelineLayout = vkDevice.createPipelineLayout(layoutInfo);
    }

    void ProbeUpdatePipeline::createComputePipeline()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        const auto& stages = updateShader->getShaderStages();
        if (stages.empty())
        {
            vfLogWarning("ProbeUpdatePipeline: No shader stages available");
            return;
        }

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stages[0];
        pipelineInfo.layout = pipelineLayout;

        computePipeline = core::PipelineUtilities::createComputePipeline(vkDevice, pipelineInfo);
    }

    void ProbeUpdatePipeline::dispatch(vk::CommandBuffer cmd,
                                        vk::DescriptorSet probeWriteDescSet,
                                        vk::DescriptorSet cascadeInfoDescSet,
                                        const GIComputePushConstants& pushConstants)
    {
        if (!initialized || !computePipeline)
        {
            return;
        }

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline);

        std::array<vk::DescriptorSet, 2> descSets = {probeWriteDescSet, cascadeInfoDescSet};
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout,
                               0, static_cast<uint32_t>(descSets.size()),
                               descSets.data(), 0, nullptr);

        cmd.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eCompute,
                          0, sizeof(GIComputePushConstants), &pushConstants);

        uint32_t workgroupCount = (pushConstants.probeCount + 63) / 64;
        cmd.dispatch(workgroupCount, 1, 1);
    }
}
