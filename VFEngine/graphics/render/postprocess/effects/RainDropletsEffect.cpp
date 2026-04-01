#include "RainDropletsEffect.hpp"
#include "../PostProcessPipeline.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/PipelineUtilities.hpp"

namespace render::postprocess
{
    struct RainDropletsPushConstants
    {
        float intensity;
        float time;
        float cameraPitchDot;
        float dropletScale;
        float trailSpeed;
    };

    RainDropletsEffect::RainDropletsEffect(core::Device& device, PostProcessPipeline& pipeline)
        : device{device}, pipeline{pipeline}
    {
        enabled = false;
    }

    void RainDropletsEffect::init(vk::RenderPass renderPass, vk::Extent2D extent)
    {
        loadShader();
        createDescriptorSetLayout();
        createPipeline(renderPass, extent);
        initialized = true;
    }

    void RainDropletsEffect::cleanup()
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

    void RainDropletsEffect::recreate(vk::RenderPass renderPass, vk::Extent2D extent)
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

    void RainDropletsEffect::record(const vk::CommandBuffer& commandBuffer,
                                     vk::DescriptorSet inputDescriptorSet)
    {
        auto camData = pipeline.getCameraData();

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                          0, inputDescriptorSet, nullptr);

        RainDropletsPushConstants pc{};
        pc.intensity = currentSettings.intensity;
        pc.time = camData.time;
        // Camera pitch: dot(cameraForward, up) - extracted from view matrix
        glm::vec3 cameraForward = -glm::vec3(camData.viewMatrix[0][2], camData.viewMatrix[1][2], camData.viewMatrix[2][2]);
        pc.cameraPitchDot = glm::dot(cameraForward, glm::vec3(0.0f, 1.0f, 0.0f));
        pc.dropletScale = currentSettings.dropletScale;
        pc.trailSpeed = currentSettings.trailSpeed;

        commandBuffer.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eFragment,
                                     0, sizeof(RainDropletsPushConstants), &pc);

        commandBuffer.draw(3, 1, 0, 0);
    }

    void RainDropletsEffect::updateParameters(const ::postprocess::PostProcessSettings& settings)
    {
        const auto& r = settings.rainDroplets;
        enabled = r.enabled && r.intensity > 0.001f;
        currentSettings = r;
    }

    void RainDropletsEffect::loadShader()
    {
        shader = std::make_shared<core::Shader>(device);
        shader->readShader("../../resources/shaders/postprocess/rain_droplets.glsl");
    }

    void RainDropletsEffect::createDescriptorSetLayout()
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

    void RainDropletsEffect::createPipeline(vk::RenderPass renderPass, vk::Extent2D extent)
    {
        core::GraphicsPipelineConfig config{};
        config.device = device.getLogicalDevice();
        config.renderPass = renderPass;
        config.extent = extent;
        config.shaderStages = shader->getShaderStages();
        config.descriptorSetLayouts = {inputDescriptorSetLayout};
        config.pushConstantSize = sizeof(RainDropletsPushConstants);
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
