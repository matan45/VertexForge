#include "ToneMappingEffect.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/PipelineUtilities.hpp"

namespace render::postprocess
{
    struct ToneMappingPushConstants
    {
        float exposure;
        float gamma;
        uint32_t mode;
    };

    ToneMappingEffect::ToneMappingEffect(core::Device& device)
        : device{device}
    {
        enabled = true;
    }

    void ToneMappingEffect::init(vk::RenderPass renderPass, vk::Extent2D extent)
    {
        loadShader();
        createDescriptorSetLayout();
        createPipeline(renderPass, extent);
        initialized = true;
    }

    void ToneMappingEffect::cleanup()
    {
        auto& dev = device.getLogicalDevice();

        if (graphicsPipeline)
        {
            dev.destroyPipeline(graphicsPipeline);
            graphicsPipeline = nullptr;
        }

        if (pipelineLayout)
        {
            dev.destroyPipelineLayout(pipelineLayout);
            pipelineLayout = nullptr;
        }

        if (inputDescriptorSetLayout)
        {
            dev.destroyDescriptorSetLayout(inputDescriptorSetLayout);
            inputDescriptorSetLayout = nullptr;
        }

        if (shader)
        {
            shader->cleanUp();
            shader.reset();
        }

        initialized = false;
    }

    void ToneMappingEffect::recreate(vk::RenderPass renderPass, vk::Extent2D extent)
    {
        auto& dev = device.getLogicalDevice();

        if (graphicsPipeline)
        {
            dev.destroyPipeline(graphicsPipeline);
            graphicsPipeline = nullptr;
        }

        if (pipelineLayout)
        {
            dev.destroyPipelineLayout(pipelineLayout);
            pipelineLayout = nullptr;
        }

        createPipeline(renderPass, extent);
    }

    void ToneMappingEffect::record(const vk::CommandBuffer& commandBuffer,
                                    vk::DescriptorSet inputDescriptorSet)
    {
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                          0, inputDescriptorSet, nullptr);

        ToneMappingPushConstants pc{};
        pc.exposure = currentExposure;
        pc.gamma = currentGamma;
        pc.mode = static_cast<uint32_t>(currentMode);

        commandBuffer.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eFragment,
                                     0, sizeof(ToneMappingPushConstants), &pc);

        commandBuffer.draw(3, 1, 0, 0);
    }

    void ToneMappingEffect::updateParameters(const ::postprocess::PostProcessSettings& settings)
    {
        const auto& tm = settings.toneMapping;
        enabled = tm.enabled;
        currentExposure = tm.exposure;
        currentGamma = tm.gamma;
        currentMode = tm.mode;
    }

    void ToneMappingEffect::loadShader()
    {
        shader = std::make_shared<core::Shader>(device);
        shader->readShader("../../resources/shaders/postprocess/tonemapping.glsl");
    }

    void ToneMappingEffect::createDescriptorSetLayout()
    {
        vk::DescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        binding.descriptorCount = 1;
        binding.stageFlags = vk::ShaderStageFlagBits::eFragment;

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;

        inputDescriptorSetLayout = device.getLogicalDevice().createDescriptorSetLayout(layoutInfo);
    }

    void ToneMappingEffect::createPipeline(vk::RenderPass renderPass, vk::Extent2D extent)
    {
        core::GraphicsPipelineConfig config{};
        config.device = device.getLogicalDevice();
        config.renderPass = renderPass;
        config.extent = extent;
        config.shaderStages = shader->getShaderStages();
        config.descriptorSetLayouts = {inputDescriptorSetLayout};
        config.pushConstantSize = sizeof(ToneMappingPushConstants);
        config.pushConstantStages = vk::ShaderStageFlagBits::eFragment;
        config.depthTestEnable = false;
        config.depthWriteEnable = false;
        config.blendEnable = false;
        config.cullMode = vk::CullModeFlagBits::eNone;

        auto result = core::PipelineUtilities::createGraphicsPipeline(config);
        graphicsPipeline = result.pipeline;
        pipelineLayout = result.pipelineLayout;
    }
}
