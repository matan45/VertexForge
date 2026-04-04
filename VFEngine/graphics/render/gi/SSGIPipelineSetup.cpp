#include "SSGIPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/Shader.hpp"
#include "../../core/OffScreen.hpp"

namespace render::gi
{
    void SSGIPipeline::createTracePipeline()
    {
        auto& dev = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayout, 2> setLayouts = {traceSet0Layout, traceSet1Layout};

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutInfo.pSetLayouts = setLayouts.data();

        tracePipelineLayout = dev.createPipelineLayout(layoutInfo);
        tracePipeline = createFullscreenPipeline(tracePipelineLayout, SSGI_FORMAT,
                                                  traceExtent, traceShader, false);
    }

    void SSGIPipeline::createTemporalPipeline()
    {
        auto& dev = device.getLogicalDevice();

        std::array<vk::DescriptorSetLayout, 2> setLayouts = {temporalSet0Layout, temporalSet1Layout};

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutInfo.pSetLayouts = setLayouts.data();

        temporalPipelineLayout = dev.createPipelineLayout(layoutInfo);
        temporalPipeline = createFullscreenPipeline(temporalPipelineLayout, SSGI_FORMAT,
                                                     traceExtent, temporalShader, false);
    }

    void SSGIPipeline::createDenoisePipeline()
    {
        auto& dev = device.getLogicalDevice();

        vk::PushConstantRange pushRange{};
        pushRange.stageFlags = vk::ShaderStageFlagBits::eFragment;
        pushRange.offset = 0;
        pushRange.size = sizeof(DenoisePushConstants);

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &denoiseSet0Layout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        denoisePipelineLayout = dev.createPipelineLayout(layoutInfo);
        denoisePipeline = createFullscreenPipeline(denoisePipelineLayout, SSGI_FORMAT,
                                                    traceExtent, denoiseShader, false);
    }

    void SSGIPipeline::createCompositePipeline()
    {
        auto& dev = device.getLogicalDevice();

        vk::PushConstantRange pushRange{};
        pushRange.stageFlags = vk::ShaderStageFlagBits::eFragment;
        pushRange.offset = 0;
        pushRange.size = sizeof(CompositePushConstants);

        vk::PipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &compositeSet0Layout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        compositePipelineLayout = dev.createPipelineLayout(layoutInfo);
        compositeColorFormat = swapChain.getSceneColorFormat();
        compositePipeline = createFullscreenPipeline(compositePipelineLayout, compositeColorFormat,
                                                      currentExtent, compositeShader, true);
    }
}
