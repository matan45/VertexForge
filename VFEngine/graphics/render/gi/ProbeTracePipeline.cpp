#include "ProbeTracePipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/Shader.hpp"
#include "print/Log.hpp"

namespace render::gi
{
    ProbeTracePipeline::ProbeTracePipeline(core::Device& device)
        : device(device)
    {
    }

    ProbeTracePipeline::~ProbeTracePipeline()
    {
        cleanup();
    }

    void ProbeTracePipeline::init(vk::DescriptorSetLayout probeDataLayout,
                                   vk::DescriptorSetLayout cascadeInfoLayout,
                                   vk::DescriptorSetLayout tlasLayout,
                                   vk::DescriptorSetLayout lightDataLayout)
    {
        if (initialized)
        {
            return;
        }

        rayQueryEnabled = (tlasLayout != nullptr);
        hasLightData = (lightDataLayout != nullptr);
        loadShader();
        createPipelineLayout(probeDataLayout, cascadeInfoLayout, tlasLayout, lightDataLayout);
        createComputePipeline();

        initialized = true;
        vfLogInfo("ProbeTracePipeline: Initialized (rayQuery={}, lightData={})", rayQueryEnabled, hasLightData);
    }

    void ProbeTracePipeline::cleanup()
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

        traceShader.reset();
        initialized = false;
    }

    void ProbeTracePipeline::loadShader()
    {
        traceShader = std::make_unique<core::Shader>(device);
        if (rayQueryEnabled)
        {
            traceShader->addMacroDefinition("USE_RAY_QUERY");
        }
        if (hasLightData)
        {
            traceShader->addMacroDefinition("USE_LIGHT_DATA");
        }
        traceShader->readShader("../../resources/shaders/gi/probe_trace.glsl");
    }

    void ProbeTracePipeline::createPipelineLayout(vk::DescriptorSetLayout probeDataLayout,
                                                    vk::DescriptorSetLayout cascadeInfoLayout,
                                                    vk::DescriptorSetLayout tlasLayout,
                                                    vk::DescriptorSetLayout lightDataLayout)
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Set 0: probe data (read + write SSBOs)
        // Set 1: cascade info UBO
        // Set 2 (optional): TLAS for ray queries
        // Set 3 (optional): light data (directional, point, spot SSBOs + counts UBO)
        std::vector<vk::DescriptorSetLayout> setLayouts = {probeDataLayout, cascadeInfoLayout};
        if (tlasLayout)
        {
            setLayouts.push_back(tlasLayout);
        }
        if (lightDataLayout)
        {
            setLayouts.push_back(lightDataLayout);
        }

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

    void ProbeTracePipeline::createComputePipeline()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        const auto& stages = traceShader->getShaderStages();
        if (stages.empty())
        {
            vfLogWarning("ProbeTracePipeline: No shader stages available");
            return;
        }

        vk::ComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.stage = stages[0];
        pipelineInfo.layout = pipelineLayout;

        auto result = vkDevice.createComputePipeline(nullptr, pipelineInfo);
        computePipeline = result.value;
    }

    void ProbeTracePipeline::dispatch(vk::CommandBuffer cmd,
                                       vk::DescriptorSet probeWriteDescSet,
                                       vk::DescriptorSet cascadeInfoDescSet,
                                       vk::DescriptorSet probeReadDescSet,
                                       const GIComputePushConstants& pushConstants,
                                       vk::DescriptorSet tlasDescSet,
                                       vk::DescriptorSet lightDataDescSet)
    {
        if (!initialized || !computePipeline)
        {
            return;
        }

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline);

        std::vector<vk::DescriptorSet> descSets = {probeWriteDescSet, cascadeInfoDescSet};
        if (rayQueryEnabled && tlasDescSet)
        {
            descSets.push_back(tlasDescSet);
        }
        if (hasLightData && lightDataDescSet)
        {
            descSets.push_back(lightDataDescSet);
        }
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout,
                               0, static_cast<uint32_t>(descSets.size()),
                               descSets.data(), 0, nullptr);

        cmd.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eCompute,
                          0, sizeof(GIComputePushConstants), &pushConstants);

        // Dispatch one workgroup per probe
        uint32_t workgroupCount = (pushConstants.probeCount + 63) / 64;
        cmd.dispatch(workgroupCount, 1, 1);
    }
}
