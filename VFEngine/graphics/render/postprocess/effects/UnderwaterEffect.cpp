#include "UnderwaterEffect.hpp"
#include "../PostProcessPipeline.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/PipelineUtilities.hpp"

namespace render::postprocess
{
    struct UnderwaterPushConstants
    {
        float waterHeight;
        float cameraDepth;
        float submersionFactor;
        float time;
        float fogDensity;
        float fogColorR, fogColorG, fogColorB;
        float absorptionR, absorptionG, absorptionB;
        float causticStrength;
        float causticScale;
        float causticSpeed;
        float meniscusWidth;
        float meniscusDistortion;
        float chromaticStrength;
        float maxFogDistance;
        float nearPlane;
        float farPlane;
    };

    UnderwaterEffect::UnderwaterEffect(core::Device& device, PostProcessPipeline& pipeline)
        : device{device}, pipeline{pipeline}
    {
        enabled = false;
    }

    void UnderwaterEffect::init(vk::Format colorFormat, vk::Extent2D extent)
    {
        loadShader();
        createDescriptorSetLayout();
        createPipeline(colorFormat, extent);
        initialized = true;
    }

    void UnderwaterEffect::cleanup()
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

    void UnderwaterEffect::recreate(vk::Format colorFormat, vk::Extent2D extent)
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

    void UnderwaterEffect::record(const vk::CommandBuffer& commandBuffer,
                                   vk::DescriptorSet inputDescriptorSet)
    {
        auto camData = pipeline.getCameraData();

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                                          0, inputDescriptorSet, nullptr);

        // Always draw — the shader handles early-out via submersionFactor=0 (passthrough)
        UnderwaterPushConstants pc{};
        pc.waterHeight = camData.waterHeight;
        pc.cameraDepth = std::max(0.0f, camData.waterHeight - camData.cameraPosition.y);
        pc.submersionFactor = camData.submersionFactor;
        pc.time = camData.time;
        pc.fogDensity = currentSettings.fogDensity;
        pc.fogColorR = currentSettings.fogColor[0];
        pc.fogColorG = currentSettings.fogColor[1];
        pc.fogColorB = currentSettings.fogColor[2];
        pc.absorptionR = currentSettings.absorptionR;
        pc.absorptionG = currentSettings.absorptionG;
        pc.absorptionB = currentSettings.absorptionB;
        pc.causticStrength = currentSettings.causticStrength;
        pc.causticScale = currentSettings.causticScale;
        pc.causticSpeed = currentSettings.causticSpeed;
        pc.meniscusWidth = currentSettings.meniscusWidth;
        pc.meniscusDistortion = currentSettings.meniscusDistortion;
        pc.chromaticStrength = currentSettings.chromaticStrength;
        pc.maxFogDistance = currentSettings.maxFogDistance;
        pc.nearPlane = camData.nearPlane;
        pc.farPlane = camData.farPlane;

        commandBuffer.pushConstants(pipelineLayout, vk::ShaderStageFlagBits::eFragment,
                                     0, sizeof(UnderwaterPushConstants), &pc);

        commandBuffer.draw(3, 1, 0, 0);
        render::FrameDrawStats::count();
    }

    void UnderwaterEffect::updateParameters(const ::postprocess::PostProcessSettings& settings)
    {
        const auto& u = settings.underwater;
        enabled = u.enabled;
        currentSettings = u;
    }

    void UnderwaterEffect::loadShader()
    {
        shader = std::make_shared<core::Shader>(device);
        shader->readShader("../../resources/shaders/postprocess/underwater.glsl");
    }

    void UnderwaterEffect::createDescriptorSetLayout()
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

    void UnderwaterEffect::createPipeline(vk::Format colorFormat, vk::Extent2D extent)
    {
        core::GraphicsPipelineConfig config{};
        config.device = device.getLogicalDevice();
        config.colorAttachmentFormats = {colorFormat};
        config.extent = extent;
        config.shaderStages = shader->getShaderStages();
        config.descriptorSetLayouts = {inputDescriptorSetLayout};
        config.pushConstantSize = sizeof(UnderwaterPushConstants);
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
