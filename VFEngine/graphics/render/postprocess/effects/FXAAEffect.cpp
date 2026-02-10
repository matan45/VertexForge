#include "FXAAEffect.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/PipelineUtilities.hpp"

namespace render::postprocess
{
    struct FXAAPushConstants
    {
        float texelSizeX;
        float texelSizeY;
        float edgeThresholdMin;
        float edgeThreshold;
        uint32_t quality;
    };

    FXAAEffect::FXAAEffect(core::Device& device)
        : device{device}
    {
        enabled = false;
    }

    void FXAAEffect::init(vk::RenderPass renderPass, vk::Extent2D extent)
    {
        currentExtent = extent;
        loadShader();
        createDescriptorSetLayout();
        createPipeline(renderPass, extent);
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

    void FXAAEffect::recreate(vk::RenderPass renderPass, vk::Extent2D extent)
    {
        currentExtent = extent;
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

    void FXAAEffect::record(const vk::CommandBuffer& commandBuffer,
                             vk::DescriptorSet inputDescriptorSet)
    {
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                          0, inputDescriptorSet, nullptr);

        FXAAPushConstants pc{};
        pc.texelSizeX = 1.0f / static_cast<float>(currentExtent.width);
        pc.texelSizeY = 1.0f / static_cast<float>(currentExtent.height);
        pc.edgeThresholdMin = currentEdgeThresholdMin;
        pc.edgeThreshold = currentEdgeThreshold;
        pc.quality = static_cast<uint32_t>(currentQuality);

        commandBuffer.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eFragment,
                                     0, sizeof(FXAAPushConstants), &pc);

        commandBuffer.draw(3, 1, 0, 0);
    }

    void FXAAEffect::updateParameters(const ::postprocess::PostProcessSettings& settings)
    {
        const auto& f = settings.fxaa;
        enabled = f.enabled;
        currentEdgeThresholdMin = f.edgeThresholdMin;
        currentEdgeThreshold = f.edgeThreshold;
        currentQuality = f.quality;
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

    void FXAAEffect::createPipeline(vk::RenderPass renderPass, vk::Extent2D extent)
    {
        core::GraphicsPipelineConfig config{};
        config.device = device.getLogicalDevice();
        config.renderPass = renderPass;
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
