#include "ChromaticAberrationEffect.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/PipelineUtilities.hpp"

namespace render::postprocess
{
    struct ChromaticAberrationPushConstants
    {
        float intensity;
    };

    ChromaticAberrationEffect::ChromaticAberrationEffect(core::Device& device)
        : device{device}
    {
        enabled = false;
    }

    void ChromaticAberrationEffect::init(vk::Format colorFormat, vk::Extent2D extent)
    {
        loadShader();
        createDescriptorSetLayout();
        createPipeline(renderPass, extent);
        initialized = true;
    }

    void ChromaticAberrationEffect::cleanup()
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

    void ChromaticAberrationEffect::recreate(vk::Format colorFormat, vk::Extent2D extent)
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

    void ChromaticAberrationEffect::record(const vk::CommandBuffer& commandBuffer,
                                            vk::DescriptorSet inputDescriptorSet)
    {
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                          0, inputDescriptorSet, nullptr);

        ChromaticAberrationPushConstants pc{};
        pc.intensity = currentIntensity;

        commandBuffer.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eFragment,
                                     0, sizeof(ChromaticAberrationPushConstants), &pc);

        commandBuffer.draw(3, 1, 0, 0);
    }

    void ChromaticAberrationEffect::updateParameters(const ::postprocess::PostProcessSettings& settings)
    {
        const auto& ca = settings.chromaticAberration;
        enabled = ca.enabled;
        currentIntensity = ca.intensity;
    }

    void ChromaticAberrationEffect::loadShader()
    {
        shader = std::make_shared<core::Shader>(device);
        shader->readShader("../../resources/shaders/postprocess/chromatic_aberration.glsl");
    }

    void ChromaticAberrationEffect::createDescriptorSetLayout()
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

    void ChromaticAberrationEffect::createPipeline(vk::Format colorFormat, vk::Extent2D extent)
    {
        core::GraphicsPipelineConfig config{};
        config.device = device.getLogicalDevice();
        config.renderPass = nullptr;
        config.colorAttachmentFormats = {colorFormat};
        config.extent = extent;
        config.shaderStages = shader->getShaderStages();
        config.descriptorSetLayouts = {inputDescriptorSetLayout};
        config.pushConstantSize = sizeof(ChromaticAberrationPushConstants);
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
