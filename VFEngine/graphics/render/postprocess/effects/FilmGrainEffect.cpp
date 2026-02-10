#include "FilmGrainEffect.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/PipelineUtilities.hpp"
#include <chrono>
#include <cmath>

namespace render::postprocess
{
    struct FilmGrainPushConstants
    {
        float intensity;
        float size;
        float time;
    };

    FilmGrainEffect::FilmGrainEffect(core::Device& device)
        : device{device}
    {
        enabled = false;
    }

    void FilmGrainEffect::init(vk::RenderPass renderPass, vk::Extent2D extent)
    {
        loadShader();
        createDescriptorSetLayout();
        createPipeline(renderPass, extent);
        initialized = true;
    }

    void FilmGrainEffect::cleanup()
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

    void FilmGrainEffect::recreate(vk::RenderPass renderPass, vk::Extent2D extent)
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

    void FilmGrainEffect::record(const vk::CommandBuffer& commandBuffer,
                                  vk::DescriptorSet inputDescriptorSet)
    {
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                          0, inputDescriptorSet, nullptr);

        // Compute animated time seed from steady clock
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        float timeMs = std::chrono::duration<float, std::milli>(now).count();
        float time = std::fmod(timeMs, 100000.0f);

        FilmGrainPushConstants pc{};
        pc.intensity = currentIntensity;
        pc.size = currentSize;
        pc.time = time;

        commandBuffer.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eFragment,
                                     0, sizeof(FilmGrainPushConstants), &pc);

        commandBuffer.draw(3, 1, 0, 0);
    }

    void FilmGrainEffect::updateParameters(const ::postprocess::PostProcessSettings& settings)
    {
        const auto& fg = settings.filmGrain;
        enabled = fg.enabled;
        currentIntensity = fg.intensity;
        currentSize = fg.size;
    }

    void FilmGrainEffect::loadShader()
    {
        shader = std::make_shared<core::Shader>(device);
        shader->readShader("../../resources/shaders/postprocess/film_grain.glsl");
    }

    void FilmGrainEffect::createDescriptorSetLayout()
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

    void FilmGrainEffect::createPipeline(vk::RenderPass renderPass, vk::Extent2D extent)
    {
        core::GraphicsPipelineConfig config{};
        config.device = device.getLogicalDevice();
        config.renderPass = renderPass;
        config.extent = extent;
        config.shaderStages = shader->getShaderStages();
        config.descriptorSetLayouts = {inputDescriptorSetLayout};
        config.pushConstantSize = sizeof(FilmGrainPushConstants);
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
