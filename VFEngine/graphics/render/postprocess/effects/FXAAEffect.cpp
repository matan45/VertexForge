#include "FXAAEffect.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/PipelineUtilities.hpp"

namespace render::postprocess
{
    FXAAEffect::FXAAEffect(core::Device& device)
        : device{device}
    {
        enabled = false;
    }

    void FXAAEffect::init(vk::Format colorFormat, vk::Extent2D extent)
    {
        loadShader();
        createDescriptorSetLayout();
        createPipeline(colorFormat, extent);
        initialized = true;
    }

    void FXAAEffect::cleanup()
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

    void FXAAEffect::recreate(vk::Format colorFormat, vk::Extent2D extent)
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

        createPipeline(colorFormat, extent);
    }

    void FXAAEffect::record(const vk::CommandBuffer& commandBuffer,
                            vk::DescriptorSet inputDescriptorSet)
    {
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                          0, inputDescriptorSet, nullptr);

        commandBuffer.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eFragment,
                                     0, sizeof(FXAAPushConstants), &params);

        commandBuffer.draw(3, 1, 0, 0);
        render::FrameDrawStats::count(render::DrawCategory::PostProcess);
    }

    void FXAAEffect::updateParameters(const ::postprocess::PostProcessSettings& settings)
    {
        // The values arrive range-checked (scene loader, render_settings_set, the editor and the script
        // setters clamp them); fxaa.glsl still guards the two that could break it (a zero contrast range,
        // a quality index past Ultra) against a plugin's unchecked ApplyPostProcessSettingsCommand.
        const auto& f = settings.fxaa;
        enabled = f.enabled;
        params.subpixel = f.subpixel;
        params.edgeThreshold = f.edgeThreshold;
        params.edgeThresholdMin = f.edgeThresholdMin;
        params.quality = static_cast<uint32_t>(f.quality);
    }

    void FXAAEffect::loadShader()
    {
        shader = std::make_shared<core::Shader>(device);
        shader->readShader("../../resources/shaders/postprocess/fxaa.glsl");
    }

    void FXAAEffect::createDescriptorSetLayout()
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

    void FXAAEffect::createPipeline(vk::Format colorFormat, vk::Extent2D extent)
    {
        core::GraphicsPipelineConfig config{};
        config.device = device.getLogicalDevice();
        config.colorAttachmentFormats = {colorFormat};
        config.extent = extent;
        config.shaderStages = shader->getShaderStages();
        config.descriptorSetLayouts = {inputDescriptorSetLayout};
        config.pushConstantSize = sizeof(FXAAPushConstants);
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
